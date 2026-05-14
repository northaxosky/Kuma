//! End-to-end scene bake tests. Drives bake_scene against fixtures
//! and verifies:
//!   - .kscene header values (magic, version, mesh + node counts)
//!   - mesh dedup (one .kmesh per unique primitive, even when N
//!     nodes reference the same mesh)
//!   - per-node world transforms preserved
//!   - sibling -meshes/ directory populated with the right files

use kuma_bake::format::{
    KSceneHeader, KSceneMeshEntry, KSceneNodeEntry, KSCENE_VERSION, MAGIC_KSCENE,
};
use std::path::PathBuf;

fn fixture(name: &str) -> PathBuf {
    let mut p: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("tests");
    p.push("fixtures");
    p.push(name);
    p
}

fn read_kscene(
    path: &std::path::Path,
) -> (KSceneHeader, Vec<KSceneMeshEntry>, Vec<KSceneNodeEntry>, Vec<u8>) {
    let bytes: Vec<u8> = std::fs::read(path).expect("read baked scene");
    let header_size: usize = std::mem::size_of::<KSceneHeader>();
    let hdr: KSceneHeader = *bytemuck::from_bytes(&bytes[..header_size]);

    let m_off:    usize = hdr.mesh_table_offset as usize;
    let m_count:  usize = hdr.mesh_count as usize;
    let m_bytes:  usize = m_count * std::mem::size_of::<KSceneMeshEntry>();
    let mesh_table: Vec<KSceneMeshEntry> =
        bytemuck::cast_slice::<u8, KSceneMeshEntry>(&bytes[m_off..m_off + m_bytes]).to_vec();

    let n_off:   usize = hdr.node_table_offset as usize;
    let n_count: usize = hdr.node_count as usize;
    let n_bytes: usize = n_count * std::mem::size_of::<KSceneNodeEntry>();
    let node_table: Vec<KSceneNodeEntry> =
        bytemuck::cast_slice::<u8, KSceneNodeEntry>(&bytes[n_off..n_off + n_bytes]).to_vec();

    let s_off:  usize = hdr.string_table_offset as usize;
    let s_size: usize = hdr.string_table_size as usize;
    let strings: Vec<u8> = bytes[s_off..s_off + s_size].to_vec();

    (hdr, mesh_table, node_table, strings)
}

fn extract_path(strings: &[u8], entry: &KSceneMeshEntry) -> String {
    let off: usize = entry.path_offset as usize;
    let len: usize = entry.path_length as usize;
    String::from_utf8(strings[off..off + len].to_vec()).expect("path is utf-8")
}

#[test]
fn bake_single_mesh_scene_produces_one_kmesh_one_node() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out: PathBuf = tmp.path().join("triangle.kscene");
    kuma_bake::bake_scene(&fixture("triangle.glb"), &out).expect("bake");

    let (hdr, mesh_table, node_table, strings) = read_kscene(&out);
    assert_eq!(hdr.magic, MAGIC_KSCENE);
    assert_eq!(hdr.version, KSCENE_VERSION);
    assert_eq!(hdr.mesh_count, 1);
    assert_eq!(hdr.node_count, 1);

    let path = extract_path(&strings, &mesh_table[0]);
    assert_eq!(path, "triangle-meshes/0.kmesh");

    assert_eq!(node_table[0].mesh_index, 0);
    assert!(tmp.path().join("triangle-meshes").join("0.kmesh").exists());
}

#[test]
fn bake_two_node_scene_dedups_mesh_and_preserves_transforms() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out: PathBuf = tmp.path().join("two_node.kscene");
    kuma_bake::bake_scene(&fixture("two_node_scene.glb"), &out).expect("bake");

    let (hdr, mesh_table, node_table, _) = read_kscene(&out);
    assert_eq!(hdr.mesh_count, 1, "both nodes share the same primitive - one .kmesh");
    assert_eq!(hdr.node_count, 2);
    assert_eq!(mesh_table.len(), 1);
    assert_eq!(node_table.len(), 2);

    // Both nodes point at the single deduplicated mesh.
    assert_eq!(node_table[0].mesh_index, 0);
    assert_eq!(node_table[1].mesh_index, 0);

    // Node A is identity-transformed; Node B is translated +5 on X.
    // glTF transform layout: translation lives at column 3 of the
    // column-major 4x4 matrix - flat index col*4 + row = 3*4 + 0 = 12.
    let a_tx: f32 = node_table[0].transform[12];
    let b_tx: f32 = node_table[1].transform[12];
    assert!((a_tx - 0.0).abs() < 0.001, "node A x={a_tx} expected 0");
    assert!((b_tx - 5.0).abs() < 0.001, "node B x={b_tx} expected 5");

    // Only one .kmesh file written (dedup worked).
    let mesh_dir = tmp.path().join("two_node-meshes");
    assert!(mesh_dir.join("0.kmesh").exists());
    assert!(!mesh_dir.join("1.kmesh").exists(), "second .kmesh would mean dedup failed");
}

#[test]
fn bake_scene_creates_meshes_dir_alongside_kscene() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out: PathBuf = tmp.path().join("triangle.kscene");
    kuma_bake::bake_scene(&fixture("triangle.glb"), &out).expect("bake");

    let mesh_dir = tmp.path().join("triangle-meshes");
    assert!(mesh_dir.is_dir(), "expected sibling meshes dir at {:?}", mesh_dir);
}

#[test]
fn bake_empty_scene_returns_error() {
    // Pass a non-glTF file; the gltf crate's error path will surface
    // as a BakeError::Gltf and the bake fails cleanly.
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let bogus: PathBuf = tmp.path().join("bogus.glb");
    std::fs::write(&bogus, b"not actually glTF").expect("write");
    let out: PathBuf = tmp.path().join("scene.kscene");
    let res = kuma_bake::bake_scene(&bogus, &out);
    assert!(res.is_err(), "expected error from non-glTF input");
}
