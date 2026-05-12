// ── Asset pipeline integration tests ────────────────────────────
// Exercises the seam between the Rust baker (kuma-bake) and the
// C++ engine's asset_format parsers. Two flavors:
//
//   * Header-shape tests verify the bytes kuma-bake writes match
//     what parse_kmesh_header / parse_ktex_header expect (size,
//     version, in-bounds slices).
//
//   * Value-roundtrip tests verify the SEMANTIC contract: vertex
//     positions/UVs/normals + texture pixels actually mean what
//     the source asset said they should mean. The header tests
//     would catch layout drift; these catch semantic drift (e.g.
//     someone "fixing" the V-flip on the wrong side of the bake).
//
// Headless on purpose: no Vulkan, no GPU upload. Those paths are
// covered by the sandbox smoke run.

#include <kuma/asset_format.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using kuma::asset_format::KMeshHeader;
using kuma::asset_format::KTexHeader;
using kuma::asset_format::ParseResult;
using kuma::asset_format::Vertex;
using kuma::asset_format::kKMeshVersion;
using kuma::asset_format::kMagicKMesh;
using kuma::asset_format::parse_kmesh_header;
using kuma::asset_format::parse_ktex_header;
using kuma::integration::fixture;
using kuma::integration::read_binary;
using kuma::integration::run_kuma_bake;

namespace {

fs::path temp_output_path(const std::string& name, const char* ext = ".kmesh") {
    fs::path dir = fs::temp_directory_path() / "kuma-integration";
    fs::create_directories(dir);
    return dir / (name + ext);
}

// True if two [r,g,b,a] arrays match exactly. Tiny helper so the
// pixel-roundtrip test reads cleanly.
bool pixel_equals(const uint8_t* p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

}  // namespace

TEST(IntegrationAssetPipelineMesh, BakedTriangleHasValidHeader) {
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

TEST(IntegrationAssetPipelineMesh, BakedTriangleSlicesAreInBounds) {
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

TEST(IntegrationAssetPipelineMesh, BakeFailsLoudlyOnMissingInput) {
    const fs::path output = temp_output_path("BakeFailsLoudlyOnMissingInput");
    fs::remove(output);

    const int code = run_kuma_bake({"mesh", "definitely/does/not/exist.obj", output.string()});
    EXPECT_NE(code, 0) << "kuma-bake should exit non-zero when input is missing";
    EXPECT_FALSE(fs::exists(output)) << "no output should be produced on failure";
}

// ── Value roundtrip: bake -> parse -> verify ────────────────────
// Header tests above only check layout. These tests pull the
// vertex/pixel bytes out of the parsed blob and assert they equal
// the values the source asset declared - catching semantic drift
// (e.g. accidentally double-flipping V, swapping pos/uv field
// order on one side) that header tests would miss.

TEST(IntegrationAssetPipelineMesh, VerticesRoundTripThroughBake) {
    // The fixture triangle.obj declares:
    //   v 0 0 0,  v 1 0 0,  v 0 1 0
    //   vt 0 0,   vt 1 0,   vt 0 1   (V flipped on bake -> 1, 1, 0)
    //   vn 0 0 1 (all three vertices share this normal)
    //   f 1/1/1 2/2/1 3/3/1
    //
    // After bake, the kmesh holds 3 unique vertices. We don't care
    // which slot got which vertex (dedup ordering is not part of
    // the format spec), so we sort by position before comparing.
    const fs::path input = fixture("mesh/triangle.obj");
    const fs::path output = temp_output_path("VerticesRoundTripThroughBake");
    fs::remove(output);
    ASSERT_EQ(run_kuma_bake({"mesh", input.string(), output.string()}), 0);

    const std::vector<uint8_t> bytes = read_binary(output);
    KMeshHeader hdr{};
    ASSERT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    ASSERT_EQ(hdr.vertex_count, 3u);

    const auto* verts = reinterpret_cast<const Vertex*>(bytes.data() + hdr.vertex_offset);

    // Find each expected vertex by position. The dedup HashMap
    // doesn't guarantee insertion order so we look up rather than
    // index by slot.
    auto find_vertex = [&](float px, float py, float pz) -> const Vertex* {
        for (uint32_t i = 0; i < hdr.vertex_count; ++i) {
            if (verts[i].pos[0] == px && verts[i].pos[1] == py && verts[i].pos[2] == pz) {
                return &verts[i];
            }
        }
        return nullptr;
    };

    // Vertex (0, 0, 0) had vt 0 0 in the OBJ; baker flips V so uv = (0, 1).
    const Vertex* v0 = find_vertex(0.0f, 0.0f, 0.0f);
    ASSERT_NE(v0, nullptr) << "missing position (0,0,0)";
    EXPECT_FLOAT_EQ(v0->uv[0], 0.0f);
    EXPECT_FLOAT_EQ(v0->uv[1], 1.0f) << "V should be flipped on bake (Vulkan convention)";

    // Vertex (1, 0, 0): vt 1 0 -> uv = (1, 1).
    const Vertex* v1 = find_vertex(1.0f, 0.0f, 0.0f);
    ASSERT_NE(v1, nullptr) << "missing position (1,0,0)";
    EXPECT_FLOAT_EQ(v1->uv[0], 1.0f);
    EXPECT_FLOAT_EQ(v1->uv[1], 1.0f);

    // Vertex (0, 1, 0): vt 0 1 -> uv = (0, 0).
    const Vertex* v2 = find_vertex(0.0f, 1.0f, 0.0f);
    ASSERT_NE(v2, nullptr) << "missing position (0,1,0)";
    EXPECT_FLOAT_EQ(v2->uv[0], 0.0f);
    EXPECT_FLOAT_EQ(v2->uv[1], 0.0f);

    // All three vertices share normal (0, 0, 1) per the OBJ.
    for (uint32_t i = 0; i < hdr.vertex_count; ++i) {
        EXPECT_FLOAT_EQ(verts[i].normal[0], 0.0f) << "vertex " << i;
        EXPECT_FLOAT_EQ(verts[i].normal[1], 0.0f) << "vertex " << i;
        EXPECT_FLOAT_EQ(verts[i].normal[2], 1.0f) << "vertex " << i;
    }
}

TEST(IntegrationAssetPipelineMesh, IndicesPointAtValidVertices) {
    const fs::path input = fixture("mesh/triangle.obj");
    const fs::path output = temp_output_path("IndicesPointAtValidVertices");
    fs::remove(output);
    ASSERT_EQ(run_kuma_bake({"mesh", input.string(), output.string()}), 0);

    const std::vector<uint8_t> bytes = read_binary(output);
    KMeshHeader hdr{};
    ASSERT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);

    const auto* indices = reinterpret_cast<const uint16_t*>(bytes.data() + hdr.index_offset);
    for (uint32_t i = 0; i < hdr.index_count; ++i) {
        EXPECT_LT(indices[i], hdr.vertex_count)
            << "index " << i << " (= " << indices[i]
            << ") is out of range for " << hdr.vertex_count << " vertices";
    }
}

TEST(IntegrationAssetPipelineTex, PixelsRoundTripThroughBake) {
    // The fixture corners.png is 4x4 RGBA8 with these corner pixels:
    //   top-left      (255, 0,   0,   255)  red
    //   top-right     (0,   255, 0,   255)  green
    //   bottom-left   (0,   0,   255, 255)  blue
    //   bottom-right  (255, 255, 255, 255)  white
    //   all other     (128, 128, 128, 255)  gray
    //
    // After bake, the .ktex stores RGBA8 pixels in row-major top-
    // left origin order. Verify the four corners and one interior
    // pixel survive encode -> decode -> bake.
    const fs::path input = fixture("texture/corners.png");
    const fs::path output = temp_output_path("PixelsRoundTripThroughBake", ".ktex");
    fs::remove(output);
    ASSERT_EQ(run_kuma_bake({"tex", input.string(), output.string()}), 0);

    const std::vector<uint8_t> bytes = read_binary(output);
    KTexHeader hdr{};
    ASSERT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    ASSERT_EQ(hdr.width, 4u);
    ASSERT_EQ(hdr.height, 4u);

    const uint8_t* pixels = bytes.data() + hdr.pixel_offset;
    auto pixel_at = [&](uint32_t x, uint32_t y) -> const uint8_t* {
        return pixels + (y * hdr.width + x) * 4;
    };

    EXPECT_TRUE(pixel_equals(pixel_at(0, 0),       255, 0,   0,   255)) << "top-left red";
    EXPECT_TRUE(pixel_equals(pixel_at(3, 0),       0,   255, 0,   255)) << "top-right green";
    EXPECT_TRUE(pixel_equals(pixel_at(0, 3),       0,   0,   255, 255)) << "bottom-left blue";
    EXPECT_TRUE(pixel_equals(pixel_at(3, 3),       255, 255, 255, 255)) << "bottom-right white";
    EXPECT_TRUE(pixel_equals(pixel_at(1, 1),       128, 128, 128, 255)) << "interior gray";
}
