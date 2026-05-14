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
using kuma::asset_format::KSceneHeader;
using kuma::asset_format::KSceneMeshEntry;
using kuma::asset_format::KSceneNodeEntry;
using kuma::asset_format::KSoundHeader;
using kuma::asset_format::KTexHeader;
using kuma::asset_format::ParseResult;
using kuma::asset_format::Vertex;
using kuma::asset_format::kAudioFormatOgg;
using kuma::asset_format::kAudioFormatPcmF32;
using kuma::asset_format::kFormatRGBA8;
using kuma::asset_format::kKMeshVersion;
using kuma::asset_format::kKSceneVersion;
using kuma::asset_format::kKSoundVersion;
using kuma::asset_format::kKTexVersion;
using kuma::asset_format::kMagicKMesh;
using kuma::asset_format::kMagicKScene;
using kuma::asset_format::kMagicKSound;
using kuma::asset_format::kMagicKTex;
using kuma::asset_format::kSceneNoMesh;
using kuma::asset_format::parse_kmesh_header;
using kuma::asset_format::parse_kscene_header;
using kuma::asset_format::parse_ksound_header;
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

// ── KSound test helpers ─────────────────────────────────────────

namespace {
std::vector<char> make_valid_ksound_pcm_bytes(uint32_t frames, uint32_t channels) {
    const size_t header_size = sizeof(KSoundHeader);
    const size_t payload_bytes = static_cast<size_t>(frames) * channels * sizeof(float);

    std::vector<char> bytes(header_size + payload_bytes, 0);
    KSoundHeader hdr{};
    hdr.magic          = kMagicKSound;
    hdr.version        = kKSoundVersion;
    hdr.format         = kAudioFormatPcmF32;
    hdr.sample_rate    = 44100;
    hdr.channels       = channels;
    hdr.frame_count    = frames;
    hdr.payload_offset = static_cast<uint32_t>(header_size);
    hdr.payload_size   = static_cast<uint32_t>(payload_bytes);
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    return bytes;
}

std::vector<char> make_valid_ksound_ogg_bytes(uint32_t payload_bytes) {
    const size_t header_size = sizeof(KSoundHeader);
    std::vector<char> bytes(header_size + payload_bytes, 0);
    KSoundHeader hdr{};
    hdr.magic          = kMagicKSound;
    hdr.version        = kKSoundVersion;
    hdr.format         = kAudioFormatOgg;
    hdr.sample_rate    = 48000;
    hdr.channels       = 2;
    hdr.frame_count    = 0;  // compressed: backend computes at load
    hdr.payload_offset = static_cast<uint32_t>(header_size);
    hdr.payload_size   = payload_bytes;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    return bytes;
}
}  // namespace

// ── KSound corruption matrix ────────────────────────────────────

TEST(IntegrationAssetFormatKSound, ValidPcmBytesParseOk) {
    const auto bytes = make_valid_ksound_pcm_bytes(100, 2);
    KSoundHeader hdr{};
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    EXPECT_EQ(hdr.sample_rate, 44100u);
    EXPECT_EQ(hdr.channels, 2u);
    EXPECT_EQ(hdr.frame_count, 100u);
}

TEST(IntegrationAssetFormatKSound, ValidOggBytesParseOk) {
    const auto bytes = make_valid_ksound_ogg_bytes(8192);
    KSoundHeader hdr{};
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    EXPECT_EQ(hdr.format, kAudioFormatOgg);
    EXPECT_EQ(hdr.frame_count, 0u);
}

TEST(IntegrationAssetFormatKSound, EmptyBufferReportsTooSmall) {
    KSoundHeader hdr{};
    EXPECT_EQ(parse_ksound_header(nullptr, 0, hdr), ParseResult::TooSmall);
}

TEST(IntegrationAssetFormatKSound, BadMagicReportsBadMagic) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 1);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.magic = 0xDEADBEEF;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::BadMagic);
}

TEST(IntegrationAssetFormatKSound, WrongVersionReportsVersionMismatch) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 1);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.version = 99;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::VersionMismatch);
}

TEST(IntegrationAssetFormatKSound, UnknownFormatReportsUnsupportedFormat) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 1);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.format = 99;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::UnsupportedFormat);
}

TEST(IntegrationAssetFormatKSound, ZeroSampleRateReportsBadSampleRate) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 1);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.sample_rate = 0;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::BadSampleRate);
}

TEST(IntegrationAssetFormatKSound, FiveChannelsReportsBadChannels) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 1);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.channels = 5;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::BadChannels);
}

TEST(IntegrationAssetFormatKSound, PcmZeroFrameCountReportsBadFrameCount) {
    auto bytes = make_valid_ksound_pcm_bytes(0, 1);  // PCM with 0 frames
    KSoundHeader hdr{};
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::BadFrameCount);
}

TEST(IntegrationAssetFormatKSound, OggNonZeroFrameCountReportsBadFrameCount) {
    auto bytes = make_valid_ksound_ogg_bytes(8192);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.frame_count = 1234;  // compressed must be 0
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::BadFrameCount);
}

TEST(IntegrationAssetFormatKSound, MismatchedPayloadSizeReportsMismatch) {
    auto bytes = make_valid_ksound_pcm_bytes(10, 2);
    KSoundHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.payload_size = 999;  // doesn't match 10 frames * 2 channels * 4 bytes
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::PayloadSizeMismatch);
}

TEST(IntegrationAssetFormatKSound, TruncatedPayloadReportsOffsetOutOfBounds) {
    auto bytes = make_valid_ksound_pcm_bytes(100, 2);
    bytes.resize(sizeof(KSoundHeader) + 10);  // chop almost all the payload
    KSoundHeader hdr{};
    EXPECT_EQ(parse_ksound_header(bytes.data(), bytes.size(), hdr), ParseResult::OffsetOutOfBounds);
}

// ── KScene test helpers ─────────────────────────────────────────

namespace {
// Build a minimal valid .kscene blob: header + mesh_count mesh
// entries (each with empty path strings) + node_count node entries
// (all pointing at mesh 0, identity transform) + a string table
// just big enough to hold the placeholder path "0.kmesh" once.
std::vector<char> make_valid_kscene_bytes(uint32_t mesh_count, uint32_t node_count) {
    constexpr char kPath[] = "0.kmesh";
    const size_t path_len = sizeof(kPath) - 1;

    const size_t header_size = sizeof(KSceneHeader);
    const size_t mesh_table_size = mesh_count * sizeof(KSceneMeshEntry);
    const size_t node_table_size = node_count * sizeof(KSceneNodeEntry);
    const size_t string_table_size = mesh_count > 0 ? path_len : 0;

    const size_t mesh_table_offset = header_size;
    const size_t node_table_offset = mesh_table_offset + mesh_table_size;
    const size_t string_table_offset = node_table_offset + node_table_size;

    std::vector<char> bytes(string_table_offset + string_table_size, 0);

    KSceneHeader hdr{};
    hdr.magic = kMagicKScene;
    hdr.version = kKSceneVersion;
    hdr.mesh_count = mesh_count;
    hdr.node_count = node_count;
    hdr.mesh_table_offset = static_cast<uint32_t>(mesh_table_offset);
    hdr.node_table_offset = static_cast<uint32_t>(node_table_offset);
    hdr.string_table_offset = static_cast<uint32_t>(string_table_offset);
    hdr.string_table_size = static_cast<uint32_t>(string_table_size);
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));

    // Mesh entries all point at offset 0, length path_len.
    for (uint32_t i = 0; i < mesh_count; ++i) {
        KSceneMeshEntry m{};
        m.path_offset = 0;
        m.path_length = static_cast<uint32_t>(path_len);
        std::memcpy(bytes.data() + mesh_table_offset + i * sizeof(KSceneMeshEntry),
                    &m, sizeof(m));
    }

    // Node entries all reference mesh 0 with identity transform.
    for (uint32_t i = 0; i < node_count; ++i) {
        KSceneNodeEntry n{};
        n.mesh_index = mesh_count > 0 ? 0 : kSceneNoMesh;
        // Identity column-major: 1 at indices 0, 5, 10, 15.
        n.transform[0]  = 1.0f;
        n.transform[5]  = 1.0f;
        n.transform[10] = 1.0f;
        n.transform[15] = 1.0f;
        std::memcpy(bytes.data() + node_table_offset + i * sizeof(KSceneNodeEntry),
                    &n, sizeof(n));
    }

    // String table: write the path bytes if we have any meshes.
    if (mesh_count > 0) {
        std::memcpy(bytes.data() + string_table_offset, kPath, path_len);
    }

    return bytes;
}
}  // namespace

// ── KScene corruption matrix ────────────────────────────────────

TEST(IntegrationAssetFormatKScene, ValidBytesParseOk) {
    const auto bytes = make_valid_kscene_bytes(2, 3);
    KSceneHeader hdr{};
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
    EXPECT_EQ(hdr.mesh_count, 2u);
    EXPECT_EQ(hdr.node_count, 3u);
}

TEST(IntegrationAssetFormatKScene, EmptyBufferReportsTooSmall) {
    KSceneHeader hdr{};
    EXPECT_EQ(parse_kscene_header(nullptr, 0, hdr), ParseResult::TooSmall);
}

TEST(IntegrationAssetFormatKScene, BadMagicReportsBadMagic) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.magic = 0xDEADBEEF;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::BadMagic);
}

TEST(IntegrationAssetFormatKScene, WrongVersionReportsVersionMismatch) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.version = 99;
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::VersionMismatch);
}

TEST(IntegrationAssetFormatKScene, MeshTableOutOfBoundsReportsOffsetOutOfBounds) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.mesh_table_offset = static_cast<uint32_t>(bytes.size() + 100);
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::OffsetOutOfBounds);
}

TEST(IntegrationAssetFormatKScene, StringTableTruncationReportsOffsetOutOfBounds) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    hdr.string_table_size = 999999;  // claims more bytes than the buffer holds
    std::memcpy(bytes.data(), &hdr, sizeof(hdr));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::OffsetOutOfBounds);
}

TEST(IntegrationAssetFormatKScene, MeshPathOutOfStringTableReportsOffsetOutOfBounds) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    // Push the mesh entry's path_offset past the string table size.
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    KSceneMeshEntry m{};
    m.path_offset = hdr.string_table_size + 10;
    m.path_length = 1;
    std::memcpy(bytes.data() + hdr.mesh_table_offset, &m, sizeof(m));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::OffsetOutOfBounds);
}

TEST(IntegrationAssetFormatKScene, NodeReferencingMissingMeshReportsBadMeshIndex) {
    auto bytes = make_valid_kscene_bytes(2, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    KSceneNodeEntry n{};
    std::memcpy(&n, bytes.data() + hdr.node_table_offset, sizeof(n));
    n.mesh_index = 99;  // mesh_count is 2
    std::memcpy(bytes.data() + hdr.node_table_offset, &n, sizeof(n));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::BadMeshIndex);
}

TEST(IntegrationAssetFormatKScene, NoMeshSentinelIsAccepted) {
    auto bytes = make_valid_kscene_bytes(1, 1);
    KSceneHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    KSceneNodeEntry n{};
    std::memcpy(&n, bytes.data() + hdr.node_table_offset, sizeof(n));
    n.mesh_index = kSceneNoMesh;
    std::memcpy(bytes.data() + hdr.node_table_offset, &n, sizeof(n));
    EXPECT_EQ(parse_kscene_header(bytes.data(), bytes.size(), hdr), ParseResult::Ok);
}
