//! Binary asset format definitions.
//!
//! These structs describe the on-disk layout of `.kmesh` and
//! `.ktex` files. They MUST stay byte-for-byte identical to the
//! mirrored types in `engine/include/kuma/asset_format.h`. Any
//! change here requires:
//!   1. Bumping the corresponding version constant.
//!   2. Updating the C++ side in lockstep.
//!   3. The static_asserts in asset_format.h will catch size drift
//!      at compile time; field reordering is on us.
//!
//! All multi-byte fields are little-endian (the only endianness on
//! every platform Kuma targets - x86_64, aarch64).

use bytemuck::{Pod, Zeroable};

// ── Magic codes ─────────────────────────────────────────────────
// 4 ASCII bytes that identify the file type. Visible in a hex dump.

pub const MAGIC_KMESH: [u8; 4] = *b"KMSH";
pub const MAGIC_KTEX:  [u8; 4] = *b"KTEX";

pub const KMESH_VERSION: u32 = 1;
pub const KTEX_VERSION:  u32 = 1;

// ── Texture pixel formats ───────────────────────────────────────
// Only RGBA8 in v1; compression formats (BC7, BC5) arrive later.

pub const FORMAT_RGBA8: u32 = 1;

// ── Vertex ──────────────────────────────────────────────────────
// Per-vertex layout consumed by the engine's Vulkan vertex input.
// 32 bytes total; asserted by tests.

#[repr(C)]
#[derive(Pod, Zeroable, Copy, Clone, Debug, PartialEq)]
pub struct Vertex {
    pub pos:    [f32; 3],
    pub uv:     [f32; 2],
    pub normal: [f32; 3],
}

// ── KMesh header ────────────────────────────────────────────────
// 32-byte header at the start of every `.kmesh` file. After the
// header come `vertex_count` Vertex structs followed by
// `index_count` u16 indices. Offsets are absolute (from start of
// file) so a reader can jump directly to either section.

#[repr(C)]
#[derive(Pod, Zeroable, Copy, Clone, Debug)]
pub struct KMeshHeader {
    pub magic:         [u8; 4],
    pub version:       u32,
    pub vertex_count:  u32,
    pub index_count:   u32,
    pub vertex_offset: u32,
    pub index_offset:  u32,
    pub _reserved:     [u32; 2],
}

// ── KTex header ─────────────────────────────────────────────────
// 32-byte header at the start of every `.ktex` file. Followed by
// `width * height * bytes_per_pixel(format)` raw pixel bytes.

#[repr(C)]
#[derive(Pod, Zeroable, Copy, Clone, Debug)]
pub struct KTexHeader {
    pub magic:        [u8; 4],
    pub version:      u32,
    pub width:        u32,
    pub height:       u32,
    pub format:       u32,
    pub pixel_offset: u32,
    pub _reserved:    [u32; 2],
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::size_of;

    #[test]
    fn vertex_is_32_bytes() {
        // Kuma's Vulkan vertex input expects exactly 32 bytes per
        // vertex. If padding ever sneaks in, the GPU will read
        // garbage instead of UVs.
        assert_eq!(size_of::<Vertex>(), 32);
    }

    #[test]
    fn headers_are_32_bytes() {
        // Header size is part of the file format - readers compute
        // payload offsets relative to it.
        assert_eq!(size_of::<KMeshHeader>(), 32);
        assert_eq!(size_of::<KTexHeader>(), 32);
    }

    #[test]
    fn magics_are_ascii_strings() {
        // Sanity check: hex dump of a .kmesh starts with `4B 4D 53 48`,
        // which spells `KMSH` in ASCII. Catches accidental endian
        // confusion if someone reorders the bytes.
        assert_eq!(&MAGIC_KMESH, b"KMSH");
        assert_eq!(&MAGIC_KTEX,  b"KTEX");
    }

    #[test]
    fn kmesh_header_round_trips_through_bytes() {
        // bytemuck reinterprets the struct as raw bytes, which is
        // exactly what we'll write to disk. Reading back must
        // produce field-equal data.
        let original: KMeshHeader = KMeshHeader {
            magic:         MAGIC_KMESH,
            version:       KMESH_VERSION,
            vertex_count:  4,
            index_count:   6,
            vertex_offset: 32,
            index_offset:  32 + 4 * 32,
            _reserved:     [0; 2],
        };
        let bytes: &[u8] = bytemuck::bytes_of(&original);
        assert_eq!(bytes.len(), 32);

        let parsed: KMeshHeader = *bytemuck::from_bytes(bytes);
        assert_eq!(parsed.magic,         original.magic);
        assert_eq!(parsed.version,       original.version);
        assert_eq!(parsed.vertex_count,  original.vertex_count);
        assert_eq!(parsed.index_count,   original.index_count);
        assert_eq!(parsed.vertex_offset, original.vertex_offset);
        assert_eq!(parsed.index_offset,  original.index_offset);
    }

    #[test]
    fn ktex_header_round_trips_through_bytes() {
        let original: KTexHeader = KTexHeader {
            magic:        MAGIC_KTEX,
            version:      KTEX_VERSION,
            width:        512,
            height:       256,
            format:       FORMAT_RGBA8,
            pixel_offset: 32,
            _reserved:    [0; 2],
        };
        let bytes: &[u8] = bytemuck::bytes_of(&original);
        let parsed: KTexHeader = *bytemuck::from_bytes(bytes);
        assert_eq!(parsed.width,  512);
        assert_eq!(parsed.height, 256);
        assert_eq!(parsed.format, FORMAT_RGBA8);
    }

    #[test]
    fn vertex_round_trips_through_bytes() {
        // The vertex array is the bulk of every .kmesh, written as a
        // single bytemuck::cast_slice call. Round-tripping a vertex
        // catches field-order bugs.
        let v: Vertex = Vertex {
            pos:    [1.0, 2.0, 3.0],
            uv:     [0.5, 0.75],
            normal: [0.0, 1.0, 0.0],
        };
        let bytes: &[u8] = bytemuck::bytes_of(&v);
        let parsed: Vertex = *bytemuck::from_bytes(bytes);
        assert_eq!(parsed, v);
    }
}

// ── Shared .kmesh writer ────────────────────────────────────────
// Produces the canonical on-disk layout: 32-byte KMeshHeader,
// then `vertices.len()` Vertex structs, then `indices.len()` u16
// indices. Used by every baker that emits .kmesh (currently mesh
// and gltf). Centralized here so the byte layout is described in
// one place; if the format ever changes, this is the only writer
// that needs updating.

use std::fs;
use std::io::Write;
use std::path::Path;

use crate::error::BakeError;

pub fn write_kmesh(output: &Path, vertices: &[Vertex], indices: &[u16]) -> Result<(), BakeError> {
    let header_size:  u32 = std::mem::size_of::<KMeshHeader>() as u32;
    let vertex_bytes: u32 = (vertices.len() * std::mem::size_of::<Vertex>()) as u32;
    let header: KMeshHeader = KMeshHeader {
        magic:         MAGIC_KMESH,
        version:       KMESH_VERSION,
        vertex_count:  vertices.len() as u32,
        index_count:   indices.len() as u32,
        vertex_offset: header_size,
        index_offset:  header_size + vertex_bytes,
        _reserved:     [0; 2],
    };

    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).map_err(|e| BakeError::io(parent, e))?;
    }
    let mut file: fs::File = fs::File::create(output).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::bytes_of(&header)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(vertices)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(indices)).map_err(|e| BakeError::io(output, e))?;
    Ok(())
}
