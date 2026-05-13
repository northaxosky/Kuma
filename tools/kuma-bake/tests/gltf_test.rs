//! End-to-end glTF bake tests. Drives bake_gltf against fixtures
//! and verifies the .kmesh layout + key semantic properties:
//!   - node transforms ARE applied
//!   - UVs are NOT V-flipped (glTF already uses upper-left origin)
//!   - normals come through correctly

use kuma_bake::format::{KMeshHeader, MAGIC_KMESH, KMESH_VERSION, Vertex};
use std::path::PathBuf;

fn fixture(name: &str) -> PathBuf {
    let mut p: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("tests");
    p.push("fixtures");
    p.push(name);
    p
}

fn read_kmesh(path: &std::path::Path) -> (KMeshHeader, Vec<Vertex>, Vec<u16>) {
    let bytes: Vec<u8> = std::fs::read(path).expect("read baked file");
    let hdr: KMeshHeader =
        *bytemuck::from_bytes(&bytes[..std::mem::size_of::<KMeshHeader>()]);

    let vstart: usize = hdr.vertex_offset as usize;
    let vend:   usize = vstart + (hdr.vertex_count as usize) * std::mem::size_of::<Vertex>();
    let verts: Vec<Vertex> = bytemuck::cast_slice::<u8, Vertex>(&bytes[vstart..vend]).to_vec();

    let istart: usize = hdr.index_offset as usize;
    let iend:   usize = istart + (hdr.index_count as usize) * std::mem::size_of::<u16>();
    let idx:   Vec<u16> = bytemuck::cast_slice::<u8, u16>(&bytes[istart..iend]).to_vec();

    (hdr, verts, idx)
}

#[test]
fn bake_triangle_glb_produces_valid_kmesh() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_gltf(&fixture("triangle.glb"), &out_path)
        .expect("bake should succeed on the fixture");

    let (hdr, verts, idx) = read_kmesh(&out_path);

    assert_eq!(hdr.magic, MAGIC_KMESH);
    assert_eq!(hdr.version, KMESH_VERSION);
    assert_eq!(hdr.vertex_count, 3, "fixture is a 3-vertex triangle");
    assert_eq!(hdr.index_count, 3);
    assert_eq!(verts.len(), 3);
    assert_eq!(idx.len(), 3);
}

#[test]
fn bake_applies_node_transform_to_positions() {
    // The fixture node has translation (5, 0, 0). Raw mesh
    // positions are (0,0,0), (1,0,0), (0,1,0). After node
    // transform, X is shifted by 5 on every vertex.
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_gltf(&fixture("triangle.glb"), &out_path).unwrap();

    let (_hdr, verts, _idx) = read_kmesh(&out_path);

    let xs: Vec<f32> = verts.iter().map(|v| v.pos[0]).collect();
    assert!(xs.contains(&5.0), "expected X=5 (raw 0 + translate 5), got {:?}", xs);
    assert!(xs.contains(&6.0), "expected X=6 (raw 1 + translate 5), got {:?}", xs);
    assert!(xs.iter().all(|&x| x >= 5.0), "no vertex should have X<5 after translation");
}

#[test]
fn bake_does_not_flip_uvs() {
    // The fixture has 3 distinct UVs: (0,0), (1,0.25), (0.5,1).
    // glTF uses upper-left origin (same as Vulkan); the baker
    // must NOT flip V (unlike the OBJ baker which does flip).
    // Catches the silent double-flip bug class.
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_gltf(&fixture("triangle.glb"), &out_path).unwrap();

    let (_hdr, verts, _idx) = read_kmesh(&out_path);

    let uvs: Vec<[f32; 2]> = verts.iter().map(|v| v.uv).collect();

    // Every fixture UV must appear EXACTLY in the baked output.
    // If a flip happened, (1, 0.25) would become (1, 0.75) etc.
    let expected: [[f32; 2]; 3] = [[0.0, 0.0], [1.0, 0.25], [0.5, 1.0]];
    for want in &expected {
        let found = uvs.iter().any(|got| (got[0] - want[0]).abs() < 1e-5
                                      && (got[1] - want[1]).abs() < 1e-5);
        assert!(found, "expected UV {:?} not found in baked verts {:?}", want, uvs);
    }
}

#[test]
fn bake_preserves_normals() {
    // Fixture: all three vertices share normal (0, 0, 1).
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_gltf(&fixture("triangle.glb"), &out_path).unwrap();

    let (_hdr, verts, _idx) = read_kmesh(&out_path);

    for v in &verts {
        assert_eq!(v.normal, [0.0, 0.0, 1.0]);
    }
}

#[test]
fn bake_indices_in_range() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_gltf(&fixture("triangle.glb"), &out_path).unwrap();

    let (hdr, _verts, idx) = read_kmesh(&out_path);
    for &i in &idx {
        assert!((i as u32) < hdr.vertex_count, "index {} out of range", i);
    }
}

#[test]
fn bake_missing_file_returns_error() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("nope.kmesh");
    let err = kuma_bake::bake_gltf(&PathBuf::from("does/not/exist.glb"), &out_path)
        .expect_err("should fail");
    let msg: String = err.to_string();
    assert!(!msg.is_empty());
}
