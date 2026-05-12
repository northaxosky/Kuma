// Tests for the C++ side of the kuma-bake binary asset format.
//
// These tests are TINY by design - they verify the C++ struct
// layouts and magic constants match what kuma-bake (Rust) writes.
// The Rust side has its own tests for the same contract; if BOTH
// sides pass, the file format is byte-compatible end-to-end.

#include <kuma/asset_format.h>

#include <gtest/gtest.h>

#include <cstring>

using kuma::asset_format::KMeshHeader;
using kuma::asset_format::KTexHeader;
using kuma::asset_format::Vertex;
using kuma::asset_format::kMagicKMesh;
using kuma::asset_format::kMagicKTex;

// ── Sizes (mirrored on the Rust side) ──────────────────────────

TEST(AssetFormat, VertexIs32Bytes) {
    EXPECT_EQ(sizeof(Vertex), 32u);
}

TEST(AssetFormat, KMeshHeaderIs32Bytes) {
    EXPECT_EQ(sizeof(KMeshHeader), 32u);
}

TEST(AssetFormat, KTexHeaderIs32Bytes) {
    EXPECT_EQ(sizeof(KTexHeader), 32u);
}

// ── Magic constants spell ASCII when written as little-endian ──

TEST(AssetFormat, KMeshMagicSpellsKMSH) {
    // 'KMSH' as little-endian u32 = 0x48534D4B. When the engine
    // memcpy's the first 4 bytes of a .kmesh into a uint32_t, it
    // must equal kMagicKMesh.
    const char expected[4] = {'K', 'M', 'S', 'H'};
    uint32_t as_u32 = 0;
    std::memcpy(&as_u32, expected, 4);
    EXPECT_EQ(as_u32, kMagicKMesh);
}

TEST(AssetFormat, KTexMagicSpellsKTEX) {
    const char expected[4] = {'K', 'T', 'E', 'X'};
    uint32_t as_u32 = 0;
    std::memcpy(&as_u32, expected, 4);
    EXPECT_EQ(as_u32, kMagicKTex);
}

// ── Field offsets (padding-free layout invariant) ──────────────

TEST(AssetFormat, KMeshHeaderHasNoPadding) {
    // Verify each field sits at the offset we expect. If a
    // misaligned field forced the compiler to insert padding,
    // these would shift.
    EXPECT_EQ(offsetof(KMeshHeader, magic),         0u);
    EXPECT_EQ(offsetof(KMeshHeader, version),       4u);
    EXPECT_EQ(offsetof(KMeshHeader, vertex_count),  8u);
    EXPECT_EQ(offsetof(KMeshHeader, index_count),   12u);
    EXPECT_EQ(offsetof(KMeshHeader, vertex_offset), 16u);
    EXPECT_EQ(offsetof(KMeshHeader, index_offset),  20u);
    EXPECT_EQ(offsetof(KMeshHeader, reserved),      24u);
}

TEST(AssetFormat, KTexHeaderHasNoPadding) {
    EXPECT_EQ(offsetof(KTexHeader, magic),        0u);
    EXPECT_EQ(offsetof(KTexHeader, version),      4u);
    EXPECT_EQ(offsetof(KTexHeader, width),        8u);
    EXPECT_EQ(offsetof(KTexHeader, height),       12u);
    EXPECT_EQ(offsetof(KTexHeader, format),       16u);
    EXPECT_EQ(offsetof(KTexHeader, pixel_offset), 20u);
    EXPECT_EQ(offsetof(KTexHeader, reserved),     24u);
}

TEST(AssetFormat, VertexHasNoPadding) {
    EXPECT_EQ(offsetof(Vertex, pos),    0u);
    EXPECT_EQ(offsetof(Vertex, uv),     12u);
    EXPECT_EQ(offsetof(Vertex, normal), 20u);
}
