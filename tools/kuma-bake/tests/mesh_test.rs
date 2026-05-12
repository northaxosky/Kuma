//! End-to-end mesh bake tests. Writes real .kmesh files to a
//! tempdir, parses the resulting bytes, and asserts the layout
//! matches what the engine's load_mesh_binary will see.

use kuma_bake::format::{KMeshHeader, MAGIC_KMESH, KMESH_VERSION, Vertex};
use std::path::PathBuf;

fn fixture(name: &str) -> PathBuf {
    let mut p: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("tests");
    p.push("fixtures");
    p.push(name);
    p
}

#[test]
fn bake_triangle_produces_valid_kmesh() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");

    kuma_bake::bake_mesh(&fixture("triangle.obj"), &out_path)
        .expect("bake should succeed on the fixture");

    let bytes: Vec<u8> = std::fs::read(&out_path).expect("read baked file");

    assert!(bytes.len() >= std::mem::size_of::<KMeshHeader>());
    let hdr: KMeshHeader = *bytemuck::from_bytes(&bytes[..std::mem::size_of::<KMeshHeader>()]);

    assert_eq!(hdr.magic, MAGIC_KMESH);
    assert_eq!(hdr.version, KMESH_VERSION);
    assert_eq!(hdr.vertex_count, 3, "triangle has 3 unique vertices");
    assert_eq!(hdr.index_count, 3, "triangle has 3 indices");
    assert_eq!(hdr.vertex_offset, 32, "vertices start right after header");

    let expected_index_offset: u32 =
        hdr.vertex_offset + hdr.vertex_count * (std::mem::size_of::<Vertex>() as u32);
    assert_eq!(hdr.index_offset, expected_index_offset);

    let expected_size: usize = std::mem::size_of::<KMeshHeader>()
        + (hdr.vertex_count as usize) * std::mem::size_of::<Vertex>()
        + (hdr.index_count as usize) * std::mem::size_of::<u16>();
    assert_eq!(bytes.len(), expected_size);
}

#[test]
fn bake_triangle_writes_correct_vertex_data() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("triangle.kmesh");
    kuma_bake::bake_mesh(&fixture("triangle.obj"), &out_path).unwrap();

    let bytes: Vec<u8> = std::fs::read(&out_path).unwrap();
    let hdr: KMeshHeader = *bytemuck::from_bytes(&bytes[..32]);

    let vstart: usize = hdr.vertex_offset as usize;
    let vend:   usize = vstart + (hdr.vertex_count as usize) * std::mem::size_of::<Vertex>();
    let verts: &[Vertex] = bytemuck::cast_slice(&bytes[vstart..vend]);
    assert_eq!(verts.len(), 3);

    // All three vertices have normal (0, 0, 1) per the OBJ.
    for v in verts {
        assert_eq!(v.normal, [0.0, 0.0, 1.0]);
    }

    // V was flipped on the way in. The OBJ said vt 0.0 1.0 -> (0, 0)
    // after flip; vt 0.0 0.0 -> (0, 1).
    let mut found_zero_one: bool = false;
    for v in verts {
        if v.uv == [0.0, 1.0] {
            found_zero_one = true;
        }
    }
    assert!(found_zero_one, "expected a UV of (0, 1) after V flip");
}

#[test]
fn bake_missing_file_returns_error() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("nope.kmesh");
    let err = kuma_bake::bake_mesh(&PathBuf::from("does/not/exist.obj"), &out_path)
        .expect_err("should fail");
    let msg: String = err.to_string();
    assert!(!msg.is_empty(), "error message should be non-empty");
}
