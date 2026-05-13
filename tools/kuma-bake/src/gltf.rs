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
            "glTF '{}' contains {} meshes; v1 baker handles only single-mesh files. \
             Export each object separately, or wait for multi-mesh support.",
            input.display(),
            mesh_count
        )));
    }

    let mesh: gltf::Mesh<'_> = doc.meshes().next().expect("mesh_count > 0 checked");
    let primitive_count: usize = mesh.primitives().count();
    if primitive_count > 1 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' mesh '{}' contains {} primitives; v1 baker handles only \
             single-primitive meshes (typically one material per object). Split \
             the mesh by material in your DCC tool, or wait for multi-primitive support.",
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

    // Triangle list is the only mode the engine renders. Without
    // this guard, a POINTS / LINES / STRIPS / FANS primitive would
    // silently bake into garbage indexed-triangle output.
    if primitive.mode() != gltf::mesh::Mode::Triangles {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' primitive uses mode {:?}; only Triangles is supported. \
             Re-export with triangulation enabled.",
            input.display(),
            primitive.mode()
        )));
    }

    // Find the first scene node that references this mesh and use
    // its world-space transform. Catches the "artist moved the
    // object in Blender but didn't Apply Transforms" case so the
    // baked geometry matches the viewport. Default: identity if no
    // node references the mesh (rare; exporters always create one).
    let node_transform: [[f32; 4]; 4] = find_mesh_node_transform(&doc, mesh.index());
    let normal_matrix:  [[f32; 3]; 3] = upper_left_3x3(node_transform);

    let reader = primitive.reader(|b| Some(&buffers[b.index()]));

    let raw_positions: Vec<[f32; 3]> = reader
        .read_positions()
        .ok_or_else(|| BakeError::Invalid(format!(
            "glTF '{}' primitive missing required POSITION attribute",
            input.display()
        )))?
        .collect();

    // Defaults match the OBJ baker: missing normals -> +Y, missing
    // UVs -> (0, 0). gltf::Reader returns None when the attribute
    // is absent.
    let raw_normals: Vec<[f32; 3]> = reader
        .read_normals()
        .map(|it| it.collect())
        .unwrap_or_else(|| vec![[0.0, 1.0, 0.0]; raw_positions.len()]);

    let raw_uvs: Vec<[f32; 2]> = reader
        .read_tex_coords(0)
        .map(|tc| tc.into_f32().collect())
        .unwrap_or_else(|| vec![[0.0, 0.0]; raw_positions.len()]);

    if raw_normals.len() != raw_positions.len() || raw_uvs.len() != raw_positions.len() {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' vertex attribute counts mismatch (positions={}, normals={}, uvs={})",
            input.display(), raw_positions.len(), raw_normals.len(), raw_uvs.len()
        )));
    }

    // Apply the node transform to positions; normals get the
    // upper-left 3x3 (no translation, no perspective). For non-
    // uniform scale this isn't perfectly correct (should use the
    // inverse-transpose), but uniform scale is the common case
    // and the engine doesn't lighting yet anyway.
    let positions: Vec<[f32; 3]> = raw_positions.iter()
        .map(|p| transform_point(&node_transform, *p))
        .collect();
    let normals: Vec<[f32; 3]> = raw_normals.iter()
        .map(|n| normalize3(transform_dir(&normal_matrix, *n)))
        .collect();

    // Indices: glTF allows u8/u16/u32. Use into_u32() to get a
    // single iteration type. Missing indices means sequential
    // triangles (0,1,2)(3,4,5)... so generate them.
    let raw_indices: Vec<u32> = reader
        .read_indices()
        .map(|it| it.into_u32().collect())
        .unwrap_or_else(|| (0..positions.len() as u32).collect());

    if raw_indices.len() % 3 != 0 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' triangle list has {} indices (not divisible by 3)",
            input.display(), raw_indices.len()
        )));
    }

    // Dedup via the same key shape as the OBJ baker (8 floats as
    // bits). glTF source data is often pre-deduped so this is
    // mostly identity, but we keep the pass to (a) enforce the
    // u16 vertex limit, (b) match OBJ baker behavior, (c) catch
    // exporters that emit non-indexed triangle soup.
    let mut vertices: Vec<Vertex> = Vec::new();
    let mut indices:  Vec<u16>    = Vec::new();
    let mut dedup:    HashMap<[u32; 8], u16> = HashMap::new();

    for &raw_idx in &raw_indices {
        let i: usize = raw_idx as usize;
        if i >= positions.len() {
            return Err(BakeError::Invalid(format!(
                "glTF '{}' index {} out of range (vertex count {})",
                input.display(), i, positions.len()
            )));
        }
        let v: Vertex = Vertex {
            pos:    positions[i],
            uv:     raw_uvs[i],
            normal: normals[i],
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
                        "glTF '{}' yields more than 65535 unique vertices after dedup; \
                         current engine uses u16 indices. Split or simplify the mesh.",
                        input.display()
                    ))
                })?;
                vertices.push(v);
                dedup.insert(key, new_idx);
                new_idx
            }
        };
        indices.push(idx);
    }

    write_kmesh(output, &vertices, &indices)
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
fn mat4_mul(a: [[f32; 4]; 4], b: [[f32; 4]; 4]) -> [[f32; 4]; 4] {
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
