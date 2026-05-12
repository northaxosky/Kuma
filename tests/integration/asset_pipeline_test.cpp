// ── First C++ integration test ──────────────────────────────────
// Exercises the seam between the Rust baker (kuma-bake) and the
// C++ engine's asset_format.h. Spawns kuma-bake on a fixture OBJ,
// reads the resulting .kmesh, and verifies the bytes match what
// the engine's load_mesh_binary expects.
//
// Headless on purpose: no Vulkan, no GPU upload. Those paths are
// covered by the sandbox smoke run.

#include <kuma/asset_format.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using kuma::asset_format::KMeshHeader;
using kuma::asset_format::Vertex;
using kuma::asset_format::kMagicKMesh;
using kuma::asset_format::kKMeshVersion;
using kuma::integration::fixture;
using kuma::integration::read_binary;
using kuma::integration::run_kuma_bake;

namespace {

fs::path temp_output_path(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "kuma-integration";
    fs::create_directories(dir);
    return dir / (name + ".kmesh");
}

}  // namespace

TEST(AssetPipelineMesh, BakedTriangleHasValidHeader) {
    const fs::path input = fixture("mesh/triangle.obj");
    const fs::path output = temp_output_path("BakedTriangleHasValidHeader");
    fs::remove(output);

    ASSERT_EQ(run_kuma_bake({"mesh", input.string(), output.string()}), 0)
        << "kuma-bake exited with non-zero status";
    ASSERT_TRUE(fs::exists(output)) << "output .kmesh not produced";

    const std::vector<uint8_t> bytes = read_binary(output);
    ASSERT_GE(bytes.size(), sizeof(KMeshHeader));

    KMeshHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));

    EXPECT_EQ(hdr.magic, kMagicKMesh);
    EXPECT_EQ(hdr.version, kKMeshVersion);
    EXPECT_EQ(hdr.vertex_count, 3u) << "triangle has 3 unique vertices";
    EXPECT_EQ(hdr.index_count, 3u);
    EXPECT_EQ(hdr.vertex_offset, sizeof(KMeshHeader));
}

TEST(AssetPipelineMesh, BakedTriangleSlicesAreInBounds) {
    const fs::path input = fixture("mesh/triangle.obj");
    const fs::path output = temp_output_path("BakedTriangleSlicesAreInBounds");
    fs::remove(output);

    ASSERT_EQ(run_kuma_bake({"mesh", input.string(), output.string()}), 0);
    const std::vector<uint8_t> bytes = read_binary(output);

    KMeshHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));

    const size_t vertex_bytes = hdr.vertex_count * sizeof(Vertex);
    const size_t index_bytes = hdr.index_count * sizeof(uint16_t);

    // vertex_offset + vertex_bytes must fit before index_offset.
    EXPECT_LE(hdr.vertex_offset + vertex_bytes, hdr.index_offset);

    // index slice must fit inside the file.
    EXPECT_LE(hdr.index_offset + index_bytes, bytes.size());

    // No padding tolerated past the index array.
    EXPECT_EQ(hdr.index_offset + index_bytes, bytes.size())
        << "trailing bytes detected after index array";
}

TEST(AssetPipelineMesh, BakeFailsLoudlyOnMissingInput) {
    const fs::path output = temp_output_path("BakeFailsLoudlyOnMissingInput");
    fs::remove(output);

    const int code = run_kuma_bake({"mesh", "definitely/does/not/exist.obj", output.string()});
    EXPECT_NE(code, 0) << "kuma-bake should exit non-zero when input is missing";
    EXPECT_FALSE(fs::exists(output)) << "no output should be produced on failure";
}
