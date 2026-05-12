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

}  // namespace kuma::asset_format
