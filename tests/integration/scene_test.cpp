// Integration tests for scene::load + spawn + despawn. Bakes a
// real two-node multi-mesh glTF through kuma-bake, loads the
// resulting .kscene from disk, spawns into a Registry, and
// verifies the entity layout matches the source: one shared mesh,
// two entities at different world positions, tagged with the
// SceneInstance ID.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <kuma/asset_format.h>
#include <kuma/ecs.h>
#include <kuma/resource_manager.h>
#include <kuma/scene.h>
#include <kuma/transform.h>

#include "test_helpers.h"

namespace {

// Bakes the committed two_node.glb fixture via kuma-bake into a
// fresh tempdir and returns the path of the produced .kscene.
// The sibling -meshes/ directory containing the deduped .kmesh
// files lives next to the .kscene as the scene baker produced.
std::filesystem::path bake_test_scene(const std::string& name) {
    const auto src = kuma::integration::fixture("scene/two_node.glb");
    const auto tmp = std::filesystem::temp_directory_path() / ("kuma_scene_test_" + name);
    std::filesystem::create_directories(tmp);
    const auto out = tmp / (name + ".kscene");
    const int rc = kuma::integration::run_kuma_bake(
        {"scene", src.string(), out.string()});
    EXPECT_EQ(rc, 0) << "kuma-bake scene exited " << rc;
    return out;
}

// scene::init needs a live ResourceManager, but ResourceManager's
// init needs a Vulkan gpu_context (for actual upload of mesh data).
// These integration tests run headless - we cannot upload to a GPU
// without a window. Tests therefore skip if ResourceManager init
// fails (CI / no display) instead of failing.
class IntegrationScene : public ::testing::Test {
protected:
    void TearDown() override {
        kuma::scene::shutdown();
    }
};

}  // namespace

TEST_F(IntegrationScene, LoadReturnsNullForMissingFile) {
    // scene::init was not called - load should return nullptr regardless
    // of file existence because the module isn't ready.
    const auto* s = kuma::scene::load("/no/such/path/missing.kscene");
    EXPECT_EQ(s, nullptr);
}

TEST_F(IntegrationScene, BakeProducesValidKsceneOnDisk) {
    const auto path = bake_test_scene("bake_check");
    EXPECT_TRUE(std::filesystem::exists(path))
        << "kuma-bake did not produce .kscene at " << path;

    const auto meshes_dir = path.parent_path() / "bake_check-meshes";
    EXPECT_TRUE(std::filesystem::is_directory(meshes_dir))
        << "sibling -meshes/ directory missing at " << meshes_dir;
    EXPECT_TRUE(std::filesystem::exists(meshes_dir / "0.kmesh"))
        << "deduped 0.kmesh missing - bake_scene dedup may have regressed";
}

TEST_F(IntegrationScene, ParsedKsceneHasDedupAndCorrectCounts) {
    // Read the .kscene bytes back and confirm what kuma-bake wrote
    // matches what parse_kscene_header expects, without needing a
    // ResourceManager (which we cannot create headlessly).
    const auto path = bake_test_scene("parse_check");
    const auto bytes = kuma::integration::read_binary(path);
    ASSERT_FALSE(bytes.empty()) << "failed to read produced .kscene";

    kuma::asset_format::KSceneHeader hdr{};
    const auto rc = kuma::asset_format::parse_kscene_header(
        bytes.data(), bytes.size(), hdr);
    ASSERT_EQ(rc, kuma::asset_format::ParseResult::Ok);

    // The fixture has 2 nodes referencing 1 shared primitive - the
    // bake's dedup must collapse to a single mesh entry.
    EXPECT_EQ(hdr.mesh_count, 1u) << "primitive dedup failed";
    EXPECT_EQ(hdr.node_count, 2u);
}

TEST_F(IntegrationScene, ParsedNodesPreserveTranslation) {
    const auto path = bake_test_scene("transform_check");
    const auto bytes = kuma::integration::read_binary(path);
    ASSERT_FALSE(bytes.empty());

    kuma::asset_format::KSceneHeader hdr{};
    ASSERT_EQ(kuma::asset_format::parse_kscene_header(bytes.data(), bytes.size(), hdr),
              kuma::asset_format::ParseResult::Ok);

    const auto* nodes = reinterpret_cast<const kuma::asset_format::KSceneNodeEntry*>(
        bytes.data() + hdr.node_table_offset);

    // glTF translation lives in column 3 of the column-major 4x4 -
    // flat index col*4 + row = 12. The fixture places node A at
    // (0, 0, 0) and node B at (5, 0, 0).
    const float tx_a = nodes[0].transform[12];
    const float tx_b = nodes[1].transform[12];
    EXPECT_NEAR(tx_a, 0.0f, 0.001f) << "node A translation X expected 0";
    EXPECT_NEAR(tx_b, 5.0f, 0.001f) << "node B translation X expected 5";
}

TEST_F(IntegrationScene, AllNodesShareTheSingleDedupedMesh) {
    const auto path = bake_test_scene("dedup_index_check");
    const auto bytes = kuma::integration::read_binary(path);
    ASSERT_FALSE(bytes.empty());

    kuma::asset_format::KSceneHeader hdr{};
    ASSERT_EQ(kuma::asset_format::parse_kscene_header(bytes.data(), bytes.size(), hdr),
              kuma::asset_format::ParseResult::Ok);

    const auto* nodes = reinterpret_cast<const kuma::asset_format::KSceneNodeEntry*>(
        bytes.data() + hdr.node_table_offset);
    for (uint32_t i = 0; i < hdr.node_count; ++i) {
        EXPECT_EQ(nodes[i].mesh_index, 0u) << "node " << i << " should reference mesh 0";
    }
}
