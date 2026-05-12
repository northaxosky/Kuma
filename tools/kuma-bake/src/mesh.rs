//! OBJ -> .kmesh conversion.
//!
//! Reads an OBJ file via the `tobj` crate, deduplicates vertices,
//! flips UV V to match Vulkan convention (V=0 at top), supplies
//! sane defaults for missing UVs (zero) and normals (`+Y`), and
//! writes the result to a `.kmesh` blob with a 32-byte header
//! followed by packed Vertex + u16 index arrays.

use crate::error::BakeError;
use crate::format::{KMESH_VERSION, KMeshHeader, MAGIC_KMESH, Vertex};

use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::Path;

/// Bake a single OBJ file into a `.kmesh`. Creates parent
/// directories of `output` if they don't exist.
pub fn bake_mesh(input: &Path, output: &Path) -> Result<(), BakeError> {
    // 1. Parse the OBJ. `single_index = true` makes tobj produce
    //    one index stream (matching our Vertex layout) instead of
    //    separate position/uv/normal streams.
    let load_opts: tobj::LoadOptions = tobj::LoadOptions {
        triangulate: true,
        single_index: true,
        ignore_lines: true,
        ignore_points: true,
    };
    let (models, _materials): (Vec<tobj::Model>, _) = tobj::load_obj(input, &load_opts)?;
    if models.is_empty() {
        return Err(BakeError::Invalid("OBJ contains no meshes".into()));
    }

    // 2. Walk every sub-mesh, deduplicating identical vertices
    //    into a packed array. Index buffer references the dense
    //    array. HashMap key reinterprets each float as its u32
    //    bit pattern so HashMap/Eq actually work (f32 isn't Eq).
    let mut vertices: Vec<Vertex> = Vec::new();
    let mut indices:  Vec<u16>    = Vec::new();
    let mut dedup:    HashMap<[u32; 8], u16> = HashMap::new();

    for model in &models {
        let m: &tobj::Mesh = &model.mesh;

        for &i in &m.indices {
            let pos: [f32; 3] = [
                m.positions[(3 * i) as usize],
                m.positions[(3 * i + 1) as usize],
                m.positions[(3 * i + 2) as usize],
            ];

            // Missing UVs default to (0, 0). Vulkan has V=0 at the
            // TOP of the texture; OBJ stores V=0 at the bottom.
            // Flip on the way in so the engine never has to.
            let uv: [f32; 2] = if m.texcoords.is_empty() {
                [0.0, 0.0]
            } else {
                [
                    m.texcoords[(2 * i) as usize],
                    1.0 - m.texcoords[(2 * i + 1) as usize],
                ]
            };

            // Missing normals default to +Y (a sane fallback for
            // ground-plane geometry). When lighting lands we'll
            // either require normals or compute them.
            let normal: [f32; 3] = if m.normals.is_empty() {
                [0.0, 1.0, 0.0]
            } else {
                [
                    m.normals[(3 * i) as usize],
                    m.normals[(3 * i + 1) as usize],
                    m.normals[(3 * i + 2) as usize],
                ]
            };

            let v: Vertex = Vertex { pos, uv, normal };

            let key: [u32; 8] = [
                pos[0].to_bits(), pos[1].to_bits(), pos[2].to_bits(),
                uv[0].to_bits(),  uv[1].to_bits(),
                normal[0].to_bits(), normal[1].to_bits(), normal[2].to_bits(),
            ];

            let idx: u16 = match dedup.get(&key) {
                Some(&existing) => existing,
                None => {
                    let new_idx: u16 = vertices.len().try_into().map_err(|_| {
                        BakeError::Invalid(
                            "mesh exceeds 65535 unique vertices (u16 index limit)".into(),
                        )
                    })?;
                    vertices.push(v);
                    dedup.insert(key, new_idx);
                    new_idx
                }
            };
            indices.push(idx);
        }
    }

    // 3. Write the binary blob: header + vertex array + index array.
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
    file.write_all(bytemuck::cast_slice(&vertices)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(&indices)).map_err(|e| BakeError::io(output, e))?;
    Ok(())
}
