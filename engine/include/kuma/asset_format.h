#pragma once

// ── Kuma asset binary format ────────────────────────────────────
// On-disk layout for .kmesh and .ktex files produced by kuma-bake.
// MUST stay byte-for-byte identical to tools/kuma-bake/src/format.rs.
// Any change in either place requires bumping the corresponding
// version constant and updating the other side in lockstep.
//
// The static_asserts below catch size drift at C++ compile time;
// field reordering must be caught by careful review.

#include <cstddef>
#include <cstdint>

namespace kuma::asset_format {

// ── Magic codes (4 ASCII bytes, little-endian u32 in code) ──────
// Hex dump of a valid .kmesh starts with `4B 4D 53 48` ('KMSH').
inline constexpr uint32_t kMagicKMesh     = 0x48534D4B;  // 'KMSH'
inline constexpr uint32_t kMagicKTex      = 0x5845544B;  // 'KTEX'
inline constexpr uint32_t kMagicKSound    = 0x444E534B;  // 'KSND'
inline constexpr uint32_t kMagicKScene    = 0x4E43534B;  // 'KSCN'
inline constexpr uint32_t kMagicKMaterial = 0x54414D4B;  // 'KMAT'

inline constexpr uint32_t kKMeshVersion     = 1;
inline constexpr uint32_t kKTexVersion      = 1;
inline constexpr uint32_t kKSoundVersion    = 1;
inline constexpr uint32_t kKSceneVersion    = 2;  // bumped from 1 - v1 is rejected
inline constexpr uint32_t kKMaterialVersion = 1;

// ── Texture pixel formats ───────────────────────────────────────
inline constexpr uint32_t kFormatRGBA8 = 1;

// ── Audio payload formats ───────────────────────────────────────
// PcmF32 = the payload is raw IEEE-754 float32 samples, little
// endian, interleaved frame-major (mono = M M M, stereo = L R L R),
// frame_count = sample_count, payload_size = frame_count * channels * 4.
//
// Compressed formats store the original encoded bytes (passthrough).
// frame_count is 0 because the engine doesn't pre-compute it; it
// hands the bytes to the audio backend's decoder at load time.
inline constexpr uint32_t kAudioFormatPcmF32 = 0;
inline constexpr uint32_t kAudioFormatOgg    = 1;
inline constexpr uint32_t kAudioFormatMp3    = 2;
inline constexpr uint32_t kAudioFormatFlac   = 3;
inline constexpr uint32_t kAudioFormatMaxKnown = kAudioFormatFlac;

// ── Vertex (matches engine's Vulkan vertex input) ───────────────
struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};
static_assert(sizeof(Vertex) == 32,
              "Vertex layout must match tools/kuma-bake/src/format.rs");

// ── KMesh header ────────────────────────────────────────────────
struct KMeshHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t reserved[2];
};
static_assert(sizeof(KMeshHeader) == 32,
              "KMeshHeader layout must match tools/kuma-bake/src/format.rs");

// ── KTex header ─────────────────────────────────────────────────
struct KTexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t pixel_offset;
    uint32_t reserved[2];
};
static_assert(sizeof(KTexHeader) == 32,
              "KTexHeader layout must match tools/kuma-bake/src/format.rs");

// ── KSound header ───────────────────────────────────────────────
// PCM payloads: little-endian f32, interleaved frame-major.
// Compressed payloads (OGG/MP3/FLAC): original encoded bytes,
// frame_count == 0 (the audio backend computes it lazily at load).
struct KSoundHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t format;         // see kAudioFormat* constants
    uint32_t sample_rate;    // hz
    uint32_t channels;       // 1 (mono) or 2 (stereo)
    uint32_t frame_count;    // frames per channel for PCM, 0 for compressed
    uint32_t payload_offset; // byte offset of first payload byte
    uint32_t payload_size;   // payload byte count
};
static_assert(sizeof(KSoundHeader) == 32,
              "KSoundHeader layout must match tools/kuma-bake/src/format.rs");

// ── KScene header ───────────────────────────────────────────────
// Multi-mesh scene asset. Layout:
//   header (32 bytes)
//   mesh table  (mesh_count entries, KSceneMeshEntry each)
//   node table  (node_count entries, KSceneNodeEntry each)
//   string table (variable length, packed utf-8 paths)
//
// All offsets are byte offsets from the start of the file. Mesh
// paths are stored in the string table and resolved at runtime
// relative to the .kscene's own directory; absolute paths and
// "../" traversal are rejected by the loader for safety.
// 40-byte header (v2) followed by four tables:
//   mesh table     (mesh_count entries, KSceneMeshEntry each)
//   material table (material_count entries, KSceneMeshEntry each -
//                   same layout, just paths to .kmaterial files)
//   node table     (node_count entries, KSceneNodeEntry each)
//   string table   (string_table_size bytes, packed utf-8 paths)
//
// All offsets are byte offsets from the start of the file. Mesh
// and material paths are stored in the string table and resolved
// at runtime relative to the .kscene's own directory; absolute
// paths and "../" traversal are rejected by the loader for safety.
//
// Version 2 broke binary compat with v1 by adding the material
// table and reusing the node entry's previously-reserved slot for
// material_index. Loading a v1 file is rejected with a clear
// "re-bake required" error rather than silently misinterpreted.
struct KSceneHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t mesh_count;
    uint32_t material_count;
    uint32_t node_count;
    uint32_t mesh_table_offset;
    uint32_t material_table_offset;
    uint32_t node_table_offset;
    uint32_t string_table_offset;
    uint32_t string_table_size;
};
static_assert(sizeof(KSceneHeader) == 40,
              "KSceneHeader layout must match tools/kuma-bake/src/format.rs");

// One per unique mesh OR material referenced by the scene. Stores
// a path into the string table; the path is relative to the
// .kscene's directory.
struct KSceneMeshEntry {
    uint32_t path_offset;  // byte offset into string table (RELATIVE to its start)
    uint32_t path_length;  // byte length of the path
};
static_assert(sizeof(KSceneMeshEntry) == 8,
              "KSceneMeshEntry layout must match tools/kuma-bake/src/format.rs");

// transform is a 4x4 column-major matrix already composed with
// the parent chain - the runtime applies it directly to the
// entity's Transform without walking any hierarchy.
struct KSceneNodeEntry {
    uint32_t mesh_index;        // kSceneNoMesh marks a no-geometry node
    uint32_t material_index;    // kSceneNoMaterial -> renderer's default material
    float    transform[16];
};
static_assert(sizeof(KSceneNodeEntry) == 72,
              "KSceneNodeEntry layout must match tools/kuma-bake/src/format.rs");

// Sentinels for KSceneNodeEntry. Either field can be set to one of
// these to express "no mesh on this node" or "no explicit material -
// use the renderer's default white material".
inline constexpr uint32_t kSceneNoMesh     = 0xFFFF'FFFFu;
inline constexpr uint32_t kSceneNoMaterial = 0xFFFF'FFFFu;

// ── Material constants ─────────────────────────────────────────
// Alpha handling per glTF 2.0 spec.
inline constexpr uint32_t kAlphaModeOpaque   = 0;
inline constexpr uint32_t kAlphaModeMask     = 1;
inline constexpr uint32_t kAlphaModeBlend    = 2;
inline constexpr uint32_t kAlphaModeMaxKnown = kAlphaModeBlend;

// Bit flags packed into KMaterialHeader::flags.
inline constexpr uint32_t kMaterialFlagDoubleSided = 1u << 0;

// ── KMaterial header ────────────────────────────────────────────
// 108-byte header followed by a string table holding the texture
// paths referenced by this material. Every texture path is stored
// relative to the .kmaterial file's directory so a scene's material
// folder can move without rewriting paths. `path_length == 0` is
// the "no texture" sentinel for an optional slot.
//
// Format is PBR-ready: declares all the slots a future lit renderer
// will need (normal, metallic-roughness packed, occlusion, emissive)
// even though the current shader only samples diffuse. Bumping the
// version when lighting lands isn't necessary - the format stays
// the same, the lit shader just reads more slots.
#pragma pack(push, 1)
struct KMaterialHeader {
    uint32_t magic;                // 'KMAT'
    uint32_t version;              // kKMaterialVersion
    uint32_t flags;                // bit field (see kMaterialFlag*)
    uint32_t alpha_mode;           // kAlphaModeOpaque / Mask / Blend

    float    base_color[4];        // RGBA, default (1,1,1,1)
    float    alpha_cutoff;         // for Mask alpha mode, default 0.5
    float    metallic_factor;      // 0..1, default 0
    float    roughness_factor;     // 0..1, default 1
    float    normal_scale;         // default 1
    float    occlusion_strength;   // default 1
    float    emissive_factor[3];   // RGB, default (0,0,0)

    // Texture references. Each pair is (offset, length) into the
    // string table that follows the header. length == 0 -> texture
    // not present in this material; renderer binds its default for
    // the slot.
    uint32_t diffuse_path_offset;
    uint32_t diffuse_path_length;
    uint32_t normal_path_offset;
    uint32_t normal_path_length;
    uint32_t metallic_roughness_path_offset;
    uint32_t metallic_roughness_path_length;
    uint32_t occlusion_path_offset;
    uint32_t occlusion_path_length;
    uint32_t emissive_path_offset;
    uint32_t emissive_path_length;

    uint32_t string_table_size;
};
#pragma pack(pop)
static_assert(sizeof(KMaterialHeader) == 108,
              "KMaterialHeader layout must match tools/kuma-bake/src/format.rs");

// ── Pure parsers (no I/O, no third-party libs) ──────────────────
// Validate the header + slice layout of an in-memory .kmesh /
// .ktex blob. Engine loaders call these before doing any GPU
// upload; integration tests drive them with hand-crafted byte
// buffers to exercise corruption cases (bad magic, bad version,
// truncated payload, offset overlap, integer overflow risk).
//
// On Ok, `out_header` is populated and the payload byte ranges
// `[header.vertex_offset, +vertex_count*sizeof(Vertex))` etc. are
// guaranteed in-bounds. On any other result, out_header contents
// are unspecified and callers must not deref the payload offsets.

enum class ParseResult {
    Ok,
    TooSmall,           // buffer < sizeof(header)
    BadMagic,           // first 4 bytes != expected magic
    VersionMismatch,    // version field != current version
    UnsupportedFormat,  // (KTex / KSound / KMaterial) field not in known set
    OffsetOutOfBounds,  // payload slice extends past buffer
    OffsetOverlap,      // (KMesh) vertex slice overlaps with index slice
    PayloadOverflow,    // count * sizeof(elem) overflows or exceeds buffer
    BadSampleRate,      // (KSound) sample_rate == 0
    BadChannels,        // (KSound) channels != 1 and != 2
    BadFrameCount,      // (KSound) PCM frame_count == 0 OR compressed frame_count != 0
    PayloadSizeMismatch,// (KSound) PCM payload_size != frame_count * channels * 4
    BadMeshIndex,       // (KScene) node references a mesh_index >= mesh_count
    BadMaterialIndex,   // (KScene) node references a material_index >= material_count
};

ParseResult parse_kmesh_header(const void* data, size_t size, KMeshHeader& out_header);
ParseResult parse_ktex_header(const void* data, size_t size, KTexHeader& out_header);
ParseResult parse_ksound_header(const void* data, size_t size, KSoundHeader& out_header);
ParseResult parse_kscene_header(const void* data, size_t size, KSceneHeader& out_header);
ParseResult parse_kmaterial_header(const void* data, size_t size, KMaterialHeader& out_header);

}  // namespace kuma::asset_format
