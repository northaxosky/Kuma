// ── Pure asset_format parsers ───────────────────────────────────
// Implementation of the parse_kmesh_header / parse_ktex_header
// validators declared in <kuma/asset_format.h>. No Vulkan, no
// file I/O - just bounds checks against an in-memory blob.

#include <kuma/asset_format.h>

#include <cstring>
#include <limits>

namespace kuma::asset_format {

namespace {

// True when `count * elem_size` would overflow size_t. Caller must
// then short-circuit before adding to `offset`.
bool would_overflow(size_t count, size_t elem_size) {
    if (count == 0 || elem_size == 0) return false;
    return count > std::numeric_limits<size_t>::max() / elem_size;
}

}  // namespace

ParseResult parse_kmesh_header(const void* data, size_t size, KMeshHeader& out_header) {
    if (size < sizeof(KMeshHeader)) return ParseResult::TooSmall;

    std::memcpy(&out_header, data, sizeof(out_header));

    if (out_header.magic != kMagicKMesh) return ParseResult::BadMagic;
    if (out_header.version != kKMeshVersion) return ParseResult::VersionMismatch;

    // Vertex slice must fit inside the buffer.
    if (would_overflow(out_header.vertex_count, sizeof(Vertex))) {
        return ParseResult::PayloadOverflow;
    }
    const size_t vertex_bytes = static_cast<size_t>(out_header.vertex_count) * sizeof(Vertex);
    if (out_header.vertex_offset > size ||
        vertex_bytes > size - out_header.vertex_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Index slice must fit too.
    if (would_overflow(out_header.index_count, sizeof(uint16_t))) {
        return ParseResult::PayloadOverflow;
    }
    const size_t index_bytes = static_cast<size_t>(out_header.index_count) * sizeof(uint16_t);
    if (out_header.index_offset > size ||
        index_bytes > size - out_header.index_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Vertex slice must end at or before the index slice begins.
    // (Strict no-overlap: the format spec puts indices immediately
    // after vertices; loose layouts are not currently meaningful.)
    if (out_header.vertex_offset + vertex_bytes > out_header.index_offset) {
        return ParseResult::OffsetOverlap;
    }

    return ParseResult::Ok;
}

ParseResult parse_ktex_header(const void* data, size_t size, KTexHeader& out_header) {
    if (size < sizeof(KTexHeader)) return ParseResult::TooSmall;

    std::memcpy(&out_header, data, sizeof(out_header));

    if (out_header.magic != kMagicKTex) return ParseResult::BadMagic;
    if (out_header.version != kKTexVersion) return ParseResult::VersionMismatch;
    if (out_header.format != kFormatRGBA8) return ParseResult::UnsupportedFormat;

    // Pixel byte count: width * height * 4 (RGBA8). Two overflow
    // checks: width*height first, then *4.
    if (would_overflow(out_header.width, out_header.height)) {
        return ParseResult::PayloadOverflow;
    }
    const size_t pixels = static_cast<size_t>(out_header.width) * out_header.height;
    if (would_overflow(pixels, 4)) {
        return ParseResult::PayloadOverflow;
    }
    const size_t pixel_bytes = pixels * 4;
    if (out_header.pixel_offset > size ||
        pixel_bytes > size - out_header.pixel_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    return ParseResult::Ok;
}

ParseResult parse_ksound_header(const void* data, size_t size, KSoundHeader& out_header) {
    if (size < sizeof(KSoundHeader)) return ParseResult::TooSmall;

    std::memcpy(&out_header, data, sizeof(out_header));

    if (out_header.magic != kMagicKSound) return ParseResult::BadMagic;
    if (out_header.version != kKSoundVersion) return ParseResult::VersionMismatch;
    if (out_header.format > kAudioFormatMaxKnown) {
        return ParseResult::UnsupportedFormat;
    }
    if (out_header.sample_rate == 0) return ParseResult::BadSampleRate;
    if (out_header.channels != 1 && out_header.channels != 2) {
        return ParseResult::BadChannels;
    }

    const bool is_pcm = (out_header.format == kAudioFormatPcmF32);
    if (is_pcm) {
        // PCM must declare its frame count and the payload must
        // be exactly frame_count * channels * sizeof(float).
        if (out_header.frame_count == 0) return ParseResult::BadFrameCount;
        if (would_overflow(out_header.frame_count, out_header.channels)) {
            return ParseResult::PayloadOverflow;
        }
        const size_t samples =
            static_cast<size_t>(out_header.frame_count) * out_header.channels;
        if (would_overflow(samples, sizeof(float))) {
            return ParseResult::PayloadOverflow;
        }
        const size_t expected_bytes = samples * sizeof(float);
        if (expected_bytes != out_header.payload_size) {
            return ParseResult::PayloadSizeMismatch;
        }
    } else {
        // Compressed payloads carry the original encoded bytes; the
        // backend's decoder figures out frame count at load time.
        // Insisting on frame_count == 0 keeps the format unambiguous.
        if (out_header.frame_count != 0) return ParseResult::BadFrameCount;
    }

    if (out_header.payload_offset > size ||
        out_header.payload_size > size - out_header.payload_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    return ParseResult::Ok;
}

ParseResult parse_kscene_header(const void* data, size_t size, KSceneHeader& out_header) {
    if (size < sizeof(KSceneHeader)) return ParseResult::TooSmall;

    std::memcpy(&out_header, data, sizeof(out_header));

    if (out_header.magic != kMagicKScene) return ParseResult::BadMagic;
    if (out_header.version != kKSceneVersion) return ParseResult::VersionMismatch;

    // Mesh table fits within buffer.
    if (would_overflow(out_header.mesh_count, sizeof(KSceneMeshEntry))) {
        return ParseResult::PayloadOverflow;
    }
    const size_t mesh_table_bytes =
        static_cast<size_t>(out_header.mesh_count) * sizeof(KSceneMeshEntry);
    if (out_header.mesh_table_offset > size ||
        mesh_table_bytes > size - out_header.mesh_table_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Material table fits within buffer.
    if (would_overflow(out_header.material_count, sizeof(KSceneMeshEntry))) {
        return ParseResult::PayloadOverflow;
    }
    const size_t material_table_bytes =
        static_cast<size_t>(out_header.material_count) * sizeof(KSceneMeshEntry);
    if (out_header.material_table_offset > size ||
        material_table_bytes > size - out_header.material_table_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Node table fits within buffer.
    if (would_overflow(out_header.node_count, sizeof(KSceneNodeEntry))) {
        return ParseResult::PayloadOverflow;
    }
    const size_t node_table_bytes =
        static_cast<size_t>(out_header.node_count) * sizeof(KSceneNodeEntry);
    if (out_header.node_table_offset > size ||
        node_table_bytes > size - out_header.node_table_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // String table fits within buffer.
    if (out_header.string_table_offset > size ||
        out_header.string_table_size > size - out_header.string_table_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Per-mesh path entries must point inside the string table.
    const auto* mesh_entries = reinterpret_cast<const KSceneMeshEntry*>(
        static_cast<const std::uint8_t*>(data) + out_header.mesh_table_offset);
    for (uint32_t i = 0; i < out_header.mesh_count; ++i) {
        const KSceneMeshEntry& m = mesh_entries[i];
        if (m.path_offset > out_header.string_table_size ||
            m.path_length > out_header.string_table_size - m.path_offset) {
            return ParseResult::OffsetOutOfBounds;
        }
    }

    // Per-material path entries must point inside the string table.
    const auto* material_entries = reinterpret_cast<const KSceneMeshEntry*>(
        static_cast<const std::uint8_t*>(data) + out_header.material_table_offset);
    for (uint32_t i = 0; i < out_header.material_count; ++i) {
        const KSceneMeshEntry& m = material_entries[i];
        if (m.path_offset > out_header.string_table_size ||
            m.path_length > out_header.string_table_size - m.path_offset) {
            return ParseResult::OffsetOutOfBounds;
        }
    }

    // Per-node mesh_index + material_index must be valid or sentinel.
    const auto* node_entries = reinterpret_cast<const KSceneNodeEntry*>(
        static_cast<const std::uint8_t*>(data) + out_header.node_table_offset);
    for (uint32_t i = 0; i < out_header.node_count; ++i) {
        const KSceneNodeEntry& n = node_entries[i];
        if (n.mesh_index != kSceneNoMesh && n.mesh_index >= out_header.mesh_count) {
            return ParseResult::BadMeshIndex;
        }
        if (n.material_index != kSceneNoMaterial &&
            n.material_index >= out_header.material_count) {
            return ParseResult::BadMaterialIndex;
        }
    }

    return ParseResult::Ok;
}

ParseResult parse_kmaterial_header(const void* data, size_t size, KMaterialHeader& out_header) {
    if (size < sizeof(KMaterialHeader)) return ParseResult::TooSmall;

    std::memcpy(&out_header, data, sizeof(out_header));

    if (out_header.magic != kMagicKMaterial) return ParseResult::BadMagic;
    if (out_header.version != kKMaterialVersion) return ParseResult::VersionMismatch;
    if (out_header.alpha_mode > kAlphaModeMaxKnown) {
        return ParseResult::UnsupportedFormat;
    }

    // String table sits immediately after the header. Bounds check.
    const size_t string_table_offset = sizeof(KMaterialHeader);
    if (string_table_offset > size ||
        out_header.string_table_size > size - string_table_offset) {
        return ParseResult::OffsetOutOfBounds;
    }

    // Validate every (offset, length) pair against the string table.
    // length == 0 means "this texture slot is unused" so offset is
    // ignored - skip the bounds check for those entries.
    struct PathRef { uint32_t off; uint32_t len; };
    const PathRef path_entries[] = {
        {out_header.diffuse_path_offset,            out_header.diffuse_path_length},
        {out_header.normal_path_offset,             out_header.normal_path_length},
        {out_header.metallic_roughness_path_offset, out_header.metallic_roughness_path_length},
        {out_header.occlusion_path_offset,          out_header.occlusion_path_length},
        {out_header.emissive_path_offset,           out_header.emissive_path_length},
    };
    for (const auto& p : path_entries) {
        if (p.len == 0) continue;
        if (p.off > out_header.string_table_size ||
            p.len > out_header.string_table_size - p.off) {
            return ParseResult::OffsetOutOfBounds;
        }
    }

    return ParseResult::Ok;
}

}  // namespace kuma::asset_format
