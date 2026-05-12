// ── Asset format parser corruption matrix ───────────────────────
// Drives parse_kmesh_header / parse_ktex_header with hand-crafted
// byte buffers covering every failure category the parsers can
// produce. These are the bug classes integration tests are
// uniquely positioned to catch: cross-tool agreement on what
// "valid" means for a binary file format.
//
// Headless on purpose - no kuma-bake spawn needed, no Vulkan,
// no fixtures. Buffers are built in-memory.

#include <kuma/asset_format.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using kuma::asset_format::KMeshHeader;
using kuma::asset_format::KTexHeader;
using kuma::asset_format::ParseResult;
using kuma::asset_format::Vertex;
using kuma::asset_format::kFormatRGBA8;
using kuma::asset_format::kKMeshVersion;
using kuma::asset_format::kKTexVersion;
using kuma::asset_format::kMagicKMesh;
using kuma::asset_format::kMagicKTex;
using kuma::asset_format::parse_kmesh_header;
using kuma::asset_format::parse_ktex_header;

namespace {

// Build a known-valid KMesh blob with `vertex_count` vertices and
// `index_count` indices. Returns the raw bytes; caller can mutate
// before parsing to construct corruption cases.
std::vector<char> make_valid_kmesh_bytes(uint32_t vertex_count, uint32_t index_count) {
    const size_t header_size  = sizeof(KMeshHeader);
    const size_t vertex_bytes = static_cast<size_t>(vertex_count) * sizeof(Vertex);
    const size_t index_bytes  = static_cast<size_t>(index_count) * sizeof(uint16_t);

    std::vector<char> bytes(header_size + vertex_bytes + index_bytes, 0);
    KMeshHeader hdr{};
    hdr.magic         = kMagicKMesh;
    hdr.version       = kKMeshVersion;
    hdr.vertex_count  = vertex_count;
    hdr.index_count   = index_count;
    hdr.vertex_offset = static_cast<uint32_t>(header_size);
    hdr.index_offset  = static_cast<uint32_t>(header_size + vertex_bytes);
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    return bytes;
}

// Same shape for KTex.
std::vector<char> make_valid_ktex_bytes(uint32_t width, uint32_t height) {
    const size_t header_size = sizeof(KTexHeader);
    const size_t pixel_bytes = static_cast<size_t>(width) * height * 4;

    std::vector<char> bytes(header_size + pixel_bytes, 0);
    KTexHeader hdr{};
    hdr.magic        = kMagicKTex;
    hdr.version      = kKTexVersion;
    hdr.width        = width;
    hdr.height       = height;
    hdr.format       = kFormatRGBA8;
    hdr.pixel_offset = static_cast<uint32_t>(header_size);
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    return bytes;
}

}  // namespace

// ── KMesh corruption matrix ─────────────────────────────────────

TEST(IntegrationAssetFormatKMesh, ValidBytesParseOk) {
    const auto bytes = make_valid_kmesh_bytes(3, 3);
    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    EXPECT_EQ(hdr.magic, kMagicKMesh);
    EXPECT_EQ(hdr.vertex_count, 3u);
    EXPECT_EQ(hdr.index_count, 3u);
}

TEST(IntegrationAssetFormatKMesh, EmptyBufferReportsTooSmall) {
    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(nullptr, 0, hdr), ParseResult::TooSmall);
}

TEST(IntegrationAssetFormatKMesh, BufferShorterThanHeaderReportsTooSmall) {
    char tiny[8] = {};
    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(tiny, sizeof(tiny), hdr), ParseResult::TooSmall);
}

TEST(IntegrationAssetFormatKMesh, BadMagicReportsBadMagic) {
    auto bytes = make_valid_kmesh_bytes(3, 3);
    // Overwrite first 4 bytes with KTEX (different magic).
    const uint32_t wrong = kMagicKTex;
    std::memcpy(bytes.data(), &wrong, sizeof(wrong));

    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr), ParseResult::BadMagic);
}

TEST(IntegrationAssetFormatKMesh, WrongVersionReportsVersionMismatch) {
    auto bytes = make_valid_kmesh_bytes(3, 3);
    const uint32_t bad_version = kKMeshVersion + 99;
    std::memcpy(bytes.data() + sizeof(uint32_t), &bad_version, sizeof(bad_version));

    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr),
              ParseResult::VersionMismatch);
}

TEST(IntegrationAssetFormatKMesh, TruncatedVertexPayloadReportsOffsetOutOfBounds) {
    auto bytes = make_valid_kmesh_bytes(3, 3);
    // Chop off everything past the header so the vertex slice runs off the end.
    bytes.resize(sizeof(KMeshHeader));

    KMeshHeader hdr{};
    EXPECT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), hdr),
              ParseResult::OffsetOutOfBounds);
}

TEST(IntegrationAssetFormatKMesh, OverlappingVertexAndIndexSlicesReportsOverlap) {
    auto bytes = make_valid_kmesh_bytes(3, 3);
    // Make index_offset point into the middle of the vertex slice.
    KMeshHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.index_offset = hdr.vertex_offset + sizeof(Vertex);  // overlaps verts 1+2
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));

    KMeshHeader parsed{};
    EXPECT_EQ(parse_kmesh_header(bytes.data(), bytes.size(), parsed),
              ParseResult::OffsetOverlap);
}

TEST(IntegrationAssetFormatKMesh, OverflowingVertexCountReportsPayloadOverflow) {
    // Set vertex_count to a value where count * sizeof(Vertex) overflows
    // size_t, exercising the would_overflow guard in the parser.
    auto bytes = make_valid_kmesh_bytes(0, 0);
    KMeshHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.vertex_count = 0xFFFFFFFFu;  // 4 billion verts * 32 bytes overflows size_t on 32-bit;
                                     // on 64-bit it's just enormous and OffsetOutOfBounds
                                     // catches it instead - either is acceptable.
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));

    KMeshHeader parsed{};
    const ParseResult pr = parse_kmesh_header(bytes.data(), bytes.size(), parsed);
    EXPECT_TRUE(pr == ParseResult::PayloadOverflow ||
                pr == ParseResult::OffsetOutOfBounds)
        << "got " << static_cast<int>(pr);
}

// ── KTex corruption matrix ──────────────────────────────────────

TEST(IntegrationAssetFormatKTex, ValidBytesParseOk) {
    const auto bytes = make_valid_ktex_bytes(4, 4);
    KTexHeader hdr{};
    EXPECT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    EXPECT_EQ(hdr.width, 4u);
    EXPECT_EQ(hdr.height, 4u);
}

TEST(IntegrationAssetFormatKTex, BadMagicReportsBadMagic) {
    auto bytes = make_valid_ktex_bytes(4, 4);
    const uint32_t wrong = kMagicKMesh;
    std::memcpy(bytes.data(), &wrong, sizeof(wrong));

    KTexHeader hdr{};
    EXPECT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr), ParseResult::BadMagic);
}

TEST(IntegrationAssetFormatKTex, WrongVersionReportsVersionMismatch) {
    auto bytes = make_valid_ktex_bytes(4, 4);
    const uint32_t bad_version = kKTexVersion + 99;
    std::memcpy(bytes.data() + sizeof(uint32_t), &bad_version, sizeof(bad_version));

    KTexHeader hdr{};
    EXPECT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr),
              ParseResult::VersionMismatch);
}

TEST(IntegrationAssetFormatKTex, UnsupportedFormatReportsUnsupportedFormat) {
    auto bytes = make_valid_ktex_bytes(4, 4);
    // Change format field (offset 16) to an unknown id.
    const uint32_t bogus_format = 9999;
    std::memcpy(bytes.data() + offsetof(KTexHeader, format), &bogus_format,
                sizeof(bogus_format));

    KTexHeader hdr{};
    EXPECT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr),
              ParseResult::UnsupportedFormat);
}

TEST(IntegrationAssetFormatKTex, TruncatedPixelPayloadReportsOffsetOutOfBounds) {
    auto bytes = make_valid_ktex_bytes(4, 4);
    bytes.resize(sizeof(KTexHeader));  // chop the pixels

    KTexHeader hdr{};
    EXPECT_EQ(parse_ktex_header(bytes.data(), bytes.size(), hdr),
              ParseResult::OffsetOutOfBounds);
}

TEST(IntegrationAssetFormatKTex, OverflowingDimensionsReportsPayloadOverflow) {
    auto bytes = make_valid_ktex_bytes(0, 0);
    KTexHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.width  = 0xFFFFFFFFu;
    hdr.height = 0xFFFFFFFFu;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));

    KTexHeader parsed{};
    const ParseResult pr = parse_ktex_header(bytes.data(), bytes.size(), parsed);
    EXPECT_TRUE(pr == ParseResult::PayloadOverflow ||
                pr == ParseResult::OffsetOutOfBounds)
        << "got " << static_cast<int>(pr);
}
