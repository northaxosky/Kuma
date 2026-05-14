//! glTF / GLB -> .kmesh conversion.
//!
//! Supports both .gltf (JSON + external .bin) and .glb (binary
//! container). v1 takes the FIRST mesh's FIRST primitive only -
//! emits an artist-friendly error for files containing more.
//!
//! Coordinate system: glTF spec is RH +Y up -Z forward, identical
//! to Kuma's convention - no transform needed.
//!
//! UV V convention: glTF's (0, 0) is upper-left, same as Vulkan.
//! UNLIKE the OBJ baker, we do NOT flip V here. Double-flipping is
//! a silent bug class (textures appear upside down only) so the
//! integration tests use 3 distinct UVs to catch any regression.
//!
//! Node transforms ARE applied: an artist who moves a mesh in
//! Blender and forgets to "Apply All Transforms" before export
//! still gets a baked file matching what they see in the viewport.

use crate::error::BakeError;
use crate::format::{Vertex, write_kmesh};

use std::collections::HashMap;
use std::path::Path;

// Primitive geometry in local space, deduplicated and ready to
// write to a .kmesh file. Returned by extract_primitive_local so
// scene baking can write the same geometry once and instance it
// many times via per-node transforms in the .kscene file.
pub(crate) struct LocalMeshData {
    pub vertices: Vec<Vertex>,
    pub indices:  Vec<u16>,
}

// Walk a glTF primitive, validate Triangles mode + attribute counts,
// dedup vertices into a u16-indexed buffer. Does NOT apply any node
// transform - the caller is responsible for handling world space
// (single-mesh bake_gltf bakes the transform into vertices, scene
// bake stores the transform per-node and keeps mesh data local).
//
// `label` is woven into error messages so users see e.g.
// "scene 'sponza.glb' mesh #3 primitive #1 yields >65535 vertices".
pub(crate) fn extract_primitive_local<'a, F>(
    primitive: &'a gltf::Primitive<'a>,
    buffer_data: F,
    label: &str,
) -> Result<LocalMeshData, BakeError>
where
    F: Clone + Fn(gltf::Buffer<'a>) -> Option<&'a [u8]>,
{
    if primitive.mode() != gltf::mesh::Mode::Triangles {
        return Err(BakeError::Invalid(format!(
            "{label}: primitive uses mode {:?}; only Triangles is supported. \
             Re-export with triangulation enabled.",
            primitive.mode()
        )));
    }

    let reader = primitive.reader(buffer_data);

    let positions: Vec<[f32; 3]> = reader
        .read_positions()
        .ok_or_else(|| BakeError::Invalid(format!(
            "{label}: primitive missing required POSITION attribute"
        )))?
        .collect();

    let normals: Vec<[f32; 3]> = reader
        .read_normals()
        .map(|it| it.collect())
        .unwrap_or_else(|| vec![[0.0, 1.0, 0.0]; positions.len()]);

    let uvs: Vec<[f32; 2]> = reader
        .read_tex_coords(0)
        .map(|tc| tc.into_f32().collect())
        .unwrap_or_else(|| vec![[0.0, 0.0]; positions.len()]);

    if normals.len() != positions.len() || uvs.len() != positions.len() {
        return Err(BakeError::Invalid(format!(
            "{label}: vertex attribute counts mismatch (positions={}, normals={}, uvs={})",
            positions.len(), normals.len(), uvs.len()
        )));
    }

    let raw_indices: Vec<u32> = reader
        .read_indices()
        .map(|it| it.into_u32().collect())
        .unwrap_or_else(|| (0..positions.len() as u32).collect());

    if raw_indices.len() % 3 != 0 {
        return Err(BakeError::Invalid(format!(
            "{label}: triangle list has {} indices (not divisible by 3)",
            raw_indices.len()
        )));
    }

    let mut vertices: Vec<Vertex> = Vec::new();
    let mut indices:  Vec<u16>    = Vec::new();
    let mut dedup:    HashMap<[u32; 8], u16> = HashMap::new();

    for &raw_idx in &raw_indices {
        let i: usize = raw_idx as usize;
        if i >= positions.len() {
            return Err(BakeError::Invalid(format!(
                "{label}: index {i} out of range (vertex count {})",
                positions.len()
            )));
        }
        let v: Vertex = Vertex {
            pos:    positions[i],
            uv:     uvs[i],
            normal: normalize3(normals[i]),
        };
        let key: [u32; 8] = [
            v.pos[0].to_bits(), v.pos[1].to_bits(), v.pos[2].to_bits(),
            v.uv[0].to_bits(),  v.uv[1].to_bits(),
            v.normal[0].to_bits(), v.normal[1].to_bits(), v.normal[2].to_bits(),
        ];
        let idx: u16 = match dedup.get(&key) {
            Some(&existing) => existing,
            None => {
                let new_idx: u16 = vertices.len().try_into().map_err(|_| {
                    BakeError::Invalid(format!(
                        "{label}: yields more than 65535 unique vertices after dedup; \
                         the engine's u16 indices cannot address larger meshes. Split \
                         or simplify the mesh."
                    ))
                })?;
                vertices.push(v);
                dedup.insert(key, new_idx);
                new_idx
            }
        };
        indices.push(idx);
    }

    Ok(LocalMeshData { vertices, indices })
}

// Apply a column-major 4x4 transform to every position; the upper-
// left 3x3 (no translation, no perspective) is applied to normals.
// For non-uniform scale this isn't perfectly correct - the proper
// fix is the inverse-transpose - but uniform scale is the common
// case and the engine doesn't do lighting yet anyway.
pub(crate) fn apply_world_transform(data: &mut LocalMeshData, world: &[[f32; 4]; 4]) {
    let normal_mat: [[f32; 3]; 3] = upper_left_3x3(*world);
    for v in &mut data.vertices {
        v.pos    = transform_point(world, v.pos);
        v.normal = normalize3(transform_dir(&normal_mat, v.normal));
    }
}

pub fn bake_gltf(input: &Path, output: &Path) -> Result<(), BakeError> {
    // gltf::import handles .gltf vs .glb transparently. Buffers
    // (the binary blobs holding vertex data) get loaded eagerly;
    // images are loaded but ignored - v1 doesn't extract textures.
    let (doc, buffers, _images) = gltf::import(input)?;

    // Loud errors for content we deliberately drop on the floor.
    // Silent partial import is the bug class artists hate most.
    let mesh_count: usize = doc.meshes().count();
    if mesh_count == 0 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' contains no meshes",
            input.display()
        )));
    }
    if mesh_count > 1 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' contains {} meshes; the single-mesh baker handles one mesh per file. \
             Use the 'scene' subcommand for multi-mesh files.",
            input.display(),
            mesh_count
        )));
    }

    let mesh: gltf::Mesh<'_> = doc.meshes().next().expect("mesh_count > 0 checked");
    let primitive_count: usize = mesh.primitives().count();
    if primitive_count > 1 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' mesh '{}' contains {} primitives; the single-mesh baker handles \
             one primitive per file. Split by material in your DCC tool, or use the \
             'scene' subcommand which bakes each primitive as its own .kmesh.",
            input.display(),
            mesh.name().unwrap_or("<unnamed>"),
            primitive_count
        )));
    }

    let primitive: gltf::Primitive<'_> = mesh
        .primitives()
        .next()
        .ok_or_else(|| BakeError::Invalid(format!(
            "glTF '{}' mesh has zero primitives",
            input.display()
        )))?;

    // Find the first scene node that references this mesh and use
    // its world-space transform. Catches the "artist moved the
    // object in Blender but didn't Apply Transforms" case so the
    // baked geometry matches the viewport. Default: identity if no
    // node references the mesh (rare; exporters always create one).
    let world: [[f32; 4]; 4] = find_mesh_node_transform(&doc, mesh.index());

    let label: String = format!("glTF '{}'", input.display());
    let mut data = extract_primitive_local(&primitive, |b| Some(&buffers[b.index()]), &label)?;
    apply_world_transform(&mut data, &world);

    write_kmesh(output, &data.vertices, &data.indices)
}

// ── Transform helpers ──────────────────────────────────────────

// Walk the default scene; find the first node that references the
// given mesh index and return its world-space transform. Returns
// identity if no node references it (rare; would mean an orphaned
// mesh that no exporter normally produces).
fn find_mesh_node_transform(doc: &gltf::Document, mesh_index: usize) -> [[f32; 4]; 4] {
    let identity: [[f32; 4]; 4] = [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ];

    let scene: gltf::Scene<'_> = match doc.default_scene().or_else(|| doc.scenes().next()) {
        Some(s) => s,
        None    => return identity,
    };

    for node in scene.nodes() {
        if let Some(t) = walk(node, mesh_index, identity) {
            return t;
        }
    }
    identity
}

// Recursively search for the mesh, multiplying world transforms
// down the node tree.
fn walk(node: gltf::Node<'_>, mesh_index: usize, parent: [[f32; 4]; 4]) -> Option<[[f32; 4]; 4]> {
    let local: [[f32; 4]; 4] = node.transform().matrix();
    let world: [[f32; 4]; 4] = mat4_mul(parent, local);
    if let Some(m) = node.mesh() {
        if m.index() == mesh_index {
            return Some(world);
        }
    }
    for child in node.children() {
        if let Some(t) = walk(child, mesh_index, world) {
            return Some(t);
        }
    }
    None
}

// Column-major 4x4 matrix multiply matching glTF's storage convention.
pub(crate) fn mat4_mul(a: [[f32; 4]; 4], b: [[f32; 4]; 4]) -> [[f32; 4]; 4] {
    let mut out: [[f32; 4]; 4] = [[0.0; 4]; 4];
    for col in 0..4 {
        for row in 0..4 {
            let mut s: f32 = 0.0;
            for k in 0..4 {
                s += a[k][row] * b[col][k];
            }
            out[col][row] = s;
        }
    }
    out
}

fn transform_point(m: &[[f32; 4]; 4], p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[1][0] * p[1] + m[2][0] * p[2] + m[3][0],
        m[0][1] * p[0] + m[1][1] * p[1] + m[2][1] * p[2] + m[3][1],
        m[0][2] * p[0] + m[1][2] * p[1] + m[2][2] * p[2] + m[3][2],
    ]
}

fn upper_left_3x3(m: [[f32; 4]; 4]) -> [[f32; 3]; 3] {
    [
        [m[0][0], m[0][1], m[0][2]],
        [m[1][0], m[1][1], m[1][2]],
        [m[2][0], m[2][1], m[2][2]],
    ]
}

fn transform_dir(m: &[[f32; 3]; 3], v: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2],
        m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2],
        m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2],
    ]
}

fn normalize3(v: [f32; 3]) -> [f32; 3] {
    let len: f32 = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
    if len == 0.0 {
        [0.0, 1.0, 0.0]
    } else {
        [v[0] / len, v[1] / len, v[2] / len]
    }
}
