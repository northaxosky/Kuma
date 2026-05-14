//! Multi-mesh scene baker.
//!
//! Walks a glTF scene tree, dedups primitive geometry into per-
//! primitive .kmesh files, composes parent-chain world transforms
//! for every node, and writes a .kscene index referencing them.
//!
//! Output layout for `bake_scene("sponza.glb", "sponza.kscene")`:
//!
//!   sponza.kscene                  the scene index
//!   sponza-meshes/0.kmesh          one .kmesh per unique
//!   sponza-meshes/1.kmesh           (mesh, primitive) pair
//!   ...
//!
//! At runtime, kuma::scene::load resolves mesh paths relative to
//! the .kscene's own directory.

use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::error::BakeError;
use crate::format::{
    write_kmesh, KSceneHeader, KSceneMeshEntry, KSceneNodeEntry, KSCENE_NO_MATERIAL,
    KSCENE_VERSION, MAGIC_KSCENE,
};
use crate::gltf::{extract_primitive_local, mat4_mul};
use crate::materials::bake_materials;

const IDENTITY: [[f32; 4]; 4] = [
    [1.0, 0.0, 0.0, 0.0],
    [0.0, 1.0, 0.0, 0.0],
    [0.0, 0.0, 1.0, 0.0],
    [0.0, 0.0, 0.0, 1.0],
];

// Identifies a unique geometry payload inside a glTF document. Two
// nodes that reference the same (mesh, primitive) pair share one
// baked .kmesh file - the dedup map below maps each pair to an
// integer index that becomes the .kmesh filename.
#[derive(Hash, Eq, PartialEq, Clone, Copy, Debug)]
struct PrimitiveKey {
    mesh_index:      usize,
    primitive_index: usize,
}

#[derive(Debug)]
struct PendingNode {
    mesh_index:     u32,             // index into the unique-mesh table
    material_index: u32,             // KSCENE_NO_MATERIAL when primitive lacks one
    transform:      [[f32; 4]; 4],   // world-space, column-major
}

/// Bake a multi-mesh glTF / GLB scene into .kscene + sibling
/// .kmesh files. Output sibling directory is named after the
/// .kscene basename plus "-meshes".
pub fn bake_scene(input: &Path, output: &Path) -> Result<(), BakeError> {
    // The gltf crate's high-level `import` tries to load every
    // referenced texture as well, which would fail noisily on
    // material-less geometry sources where the textures aren't
    // shipped (Sponza in particular). Open the document directly
    // and load only buffer data so missing image files don't break
    // the bake. Materials/textures get IGNORED for now anyway.
    let gltf_root = gltf::Gltf::open(input).map_err(BakeError::Gltf)?;
    let document  = gltf_root.document.clone();
    let blob      = gltf_root.blob.clone();
    let buffers   = load_buffers(&document, input, blob)?;

    if document.meshes().count() == 0 {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' contains no meshes - nothing to bake into a scene",
            input.display()
        )));
    }

    // Pick a scene to walk. glTF lets a file specify a default;
    // fall back to the first scene if none is marked default.
    let scene = document
        .default_scene()
        .or_else(|| document.scenes().next())
        .ok_or_else(|| BakeError::Invalid(format!(
            "glTF '{}' has no scenes - cannot pick a node tree to bake",
            input.display()
        )))?;

    // Resolve the sibling .kmesh directory relative to the .kscene.
    // Keeping mesh files in a sibling dir matches the path the
    // engine resolver will compute at load time.
    let kscene_basename: &str = output
        .file_stem()
        .and_then(|s| s.to_str())
        .ok_or_else(|| BakeError::Invalid(format!(
            "output path '{}' has no usable file stem",
            output.display()
        )))?;
    let mesh_dir_name:      String  = format!("{kscene_basename}-meshes");
    let materials_dir_name: String  = format!("{kscene_basename}-materials");
    let textures_dir_name:  String  = format!("{kscene_basename}-textures");
    let scene_dir: &Path = output.parent().unwrap_or_else(|| Path::new("."));
    let mesh_dir_abs:      PathBuf = scene_dir.join(&mesh_dir_name);
    let materials_dir_abs: PathBuf = scene_dir.join(&materials_dir_name);
    let textures_dir_abs:  PathBuf = scene_dir.join(&textures_dir_name);

    // Materials extraction runs BEFORE the geometry walk so each
    // PendingNode can stamp its primitive's material_index using the
    // glTF material index directly. The bake writes diffuse .ktex
    // files into the textures dir and one .kmaterial per glTF
    // material into the materials dir. Returns relative paths in
    // glTF-material-index order, ready to drop into the .kscene's
    // material table.
    let materials_out = bake_materials(
        &document,
        &buffers,
        input,
        &materials_dir_name,
        &materials_dir_abs,
        &textures_dir_name,
        &textures_dir_abs,
    )?;
    let material_paths: Vec<String> = materials_out.paths;

    // Walk + dedup state.
    let mut prim_to_index: HashMap<PrimitiveKey, u32> = HashMap::new();
    let mut mesh_paths:    Vec<String>                = Vec::new();
    let mut nodes:         Vec<PendingNode>           = Vec::new();
    let mut skipped_groups: u32                       = 0;

    for root in scene.nodes() {
        walk_node(
            &document,
            &buffers,
            &root,
            IDENTITY,
            input,
            &mesh_dir_name,
            &mesh_dir_abs,
            &mut prim_to_index,
            &mut mesh_paths,
            &mut nodes,
            &mut skipped_groups,
        )?;
    }

    if nodes.is_empty() {
        return Err(BakeError::Invalid(format!(
            "glTF '{}' produced zero renderable nodes after walking the scene tree",
            input.display()
        )));
    }
    if skipped_groups > 0 {
        eprintln!(
            "kuma-bake: scene '{}' skipped {skipped_groups} group node(s) with no mesh",
            input.display()
        );
    }

    write_kscene(output, &mesh_paths, &material_paths, &nodes)
}

// Load every buffer referenced by the document. Mirrors what
// gltf::import does for buffers - resolves embedded base64,
// embedded .glb chunks, or external .bin paths relative to the
// .gltf - WITHOUT touching images so missing texture files don't
// fail the bake.
fn load_buffers(
    document: &gltf::Document,
    input: &Path,
    blob: Option<Vec<u8>>,
) -> Result<Vec<gltf::buffer::Data>, BakeError> {
    let base_dir = input.parent().unwrap_or_else(|| Path::new("."));
    let mut result: Vec<gltf::buffer::Data> = Vec::new();
    let mut blob = blob;

    for buffer in document.buffers() {
        let data: Vec<u8> = match buffer.source() {
            gltf::buffer::Source::Bin => blob
                .take()
                .ok_or_else(|| BakeError::Invalid(format!(
                    "glTF '{}' references embedded BIN but no blob is present",
                    input.display()
                )))?,
            gltf::buffer::Source::Uri(uri) => {
                if let Some(rest) = uri.strip_prefix("data:") {
                    decode_data_uri(rest).map_err(BakeError::Invalid)?
                } else {
                    let resolved = base_dir.join(uri);
                    fs::read(&resolved).map_err(|e| BakeError::io(resolved, e))?
                }
            }
        };
        if data.len() < buffer.length() {
            return Err(BakeError::Invalid(format!(
                "glTF '{}' buffer #{} declared {} bytes but loaded {}",
                input.display(),
                buffer.index(),
                buffer.length(),
                data.len()
            )));
        }
        result.push(gltf::buffer::Data(data));
    }
    Ok(result)
}

// Minimal "data:application/octet-stream;base64,...." URI decoder
// for the inline-buffer glTF variant. Used rarely but glTF spec
// permits it and Sponza's reference geometry doesn't embed buffers,
// so this exists for completeness.
fn decode_data_uri(rest: &str) -> Result<Vec<u8>, String> {
    let comma = rest.find(',').ok_or_else(|| "data URI missing comma".to_string())?;
    let (meta, payload) = rest.split_at(comma);
    let payload = &payload[1..];
    if !meta.contains(";base64") {
        return Err("non-base64 data URIs not supported".to_string());
    }
    decode_base64(payload)
}

fn decode_base64(s: &str) -> Result<Vec<u8>, String> {
    const TABLE: &[i8; 256] = &{
        let mut t = [-1_i8; 256];
        let alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let mut i = 0;
        while i < alphabet.len() {
            t[alphabet[i] as usize] = i as i8;
            i += 1;
        }
        t
    };
    let mut out: Vec<u8> = Vec::with_capacity(s.len() * 3 / 4);
    let mut buf: u32 = 0;
    let mut nbits: u32 = 0;
    for &b in s.as_bytes() {
        if b == b'=' || b.is_ascii_whitespace() { continue; }
        let v = TABLE[b as usize];
        if v < 0 { return Err(format!("invalid base64 byte 0x{:02x}", b)); }
        buf = (buf << 6) | (v as u32);
        nbits += 6;
        if nbits >= 8 {
            nbits -= 8;
            out.push(((buf >> nbits) & 0xFF) as u8);
        }
    }
    Ok(out)
}

// Recursive scene-tree walker. Composes world = parent * local at
// every step, dedups primitives the first time they're seen, and
// emits one PendingNode per primitive of every node that has a mesh.
#[allow(clippy::too_many_arguments)]
fn walk_node(
    doc: &gltf::Document,
    buffers: &[gltf::buffer::Data],
    node: &gltf::Node<'_>,
    parent_world: [[f32; 4]; 4],
    input: &Path,
    mesh_dir_name: &str,
    mesh_dir_abs: &Path,
    prim_to_index: &mut HashMap<PrimitiveKey, u32>,
    mesh_paths: &mut Vec<String>,
    nodes: &mut Vec<PendingNode>,
    skipped_groups: &mut u32,
) -> Result<(), BakeError> {
    let local: [[f32; 4]; 4] = node.transform().matrix();
    let world: [[f32; 4]; 4] = mat4_mul(parent_world, local);

    if let Some(mesh) = node.mesh() {
        for (prim_idx, primitive) in mesh.primitives().enumerate() {
            let key = PrimitiveKey {
                mesh_index:      mesh.index(),
                primitive_index: prim_idx,
            };

            // Bake on first sighting; otherwise reuse the existing
            // index. Same primitive referenced by 6 nodes = one
            // .kmesh file + 6 PendingNode entries.
            let unique_idx: u32 = if let Some(&existing) = prim_to_index.get(&key) {
                existing
            } else {
                let new_idx: u32 = mesh_paths.len() as u32;
                let label: String = format!(
                    "scene '{}' mesh#{} primitive#{}",
                    input.display(),
                    mesh.index(),
                    prim_idx
                );

                let data = extract_primitive_local(
                    &primitive,
                    |b| Some(&buffers[b.index()]),
                    &label,
                )?;

                fs::create_dir_all(mesh_dir_abs)
                    .map_err(|e| BakeError::io(mesh_dir_abs, e))?;
                let out_kmesh: PathBuf = mesh_dir_abs.join(format!("{new_idx}.kmesh"));
                write_kmesh(&out_kmesh, &data.vertices, &data.indices)?;

                // Store path RELATIVE to the .kscene's directory -
                // the engine resolver concatenates it with the
                // .kscene's parent dir. Forward slashes are valid
                // in both Unix paths and Windows ResourceManager
                // (which already handles either separator).
                mesh_paths.push(format!("{mesh_dir_name}/{new_idx}.kmesh"));
                prim_to_index.insert(key, new_idx);
                new_idx
            };

            nodes.push(PendingNode {
                mesh_index:     unique_idx,
                material_index: primitive
                    .material()
                    .index()
                    .map(|i| i as u32)
                    .unwrap_or(KSCENE_NO_MATERIAL),
                transform:      world,
            });
        }
    } else if node.children().count() == 0 {
        // No mesh and no children = pure marker / empty group. Drop
        // it on the floor with a count - the .kscene format has no
        // way to identify them by name yet, so they'd just become
        // invisible "ghost" entities at runtime.
        *skipped_groups += 1;
    }

    for child in node.children() {
        walk_node(
            doc, buffers, &child, world,
            input, mesh_dir_name, mesh_dir_abs,
            prim_to_index, mesh_paths, nodes, skipped_groups,
        )?;
    }
    // Suppress unused param lint since we keep `doc` for future
    // metadata extraction (named markers, light placeholders).
    let _ = doc;
    Ok(())
}

// Sponza-class scenes have many short mesh paths. Layout: header,
// then mesh table (8 bytes per entry, path offsets into the string
// table), then node table (72 bytes per entry, world transform),
// then a packed string table containing every mesh path back-to-back.
//
// v2 stamps each node with both a mesh_index and a material_index.
// material_count + material_table_offset reserve space for a string-
// table-backed material entry list, sized identically to the mesh
// table (each entry is just an offset+length pair into the same
// string table). Empty material_paths produces a zero-sized material
// table - safely parsed by the runtime, falls back to defaults.
fn write_kscene(
    output: &Path,
    mesh_paths: &[String],
    material_paths: &[String],
    nodes: &[PendingNode],
) -> Result<(), BakeError> {
    let header_size = std::mem::size_of::<KSceneHeader>() as u32;
    let mesh_count:     u32 = mesh_paths.len() as u32;
    let material_count: u32 = material_paths.len() as u32;
    let node_count:     u32 = nodes.len() as u32;

    let mesh_table_offset:     u32 = header_size;
    let mesh_table_size:       u32 = mesh_count * std::mem::size_of::<KSceneMeshEntry>() as u32;
    let material_table_offset: u32 = mesh_table_offset + mesh_table_size;
    let material_table_size:   u32 = material_count * std::mem::size_of::<KSceneMeshEntry>() as u32;
    let node_table_offset:     u32 = material_table_offset + material_table_size;
    let node_table_size:       u32 = node_count * std::mem::size_of::<KSceneNodeEntry>() as u32;
    let string_table_offset:   u32 = node_table_offset + node_table_size;

    // Build entries + string table simultaneously: each path gets
    // appended to the shared buffer with its offset and length
    // recorded in the corresponding entry.
    let mut string_table: Vec<u8> = Vec::new();
    let mut mesh_entries: Vec<KSceneMeshEntry> = Vec::with_capacity(mesh_paths.len());
    for path in mesh_paths {
        let bytes = path.as_bytes();
        mesh_entries.push(KSceneMeshEntry {
            path_offset: string_table.len() as u32,
            path_length: bytes.len() as u32,
        });
        string_table.extend_from_slice(bytes);
    }
    let mut material_entries: Vec<KSceneMeshEntry> = Vec::with_capacity(material_paths.len());
    for path in material_paths {
        let bytes = path.as_bytes();
        material_entries.push(KSceneMeshEntry {
            path_offset: string_table.len() as u32,
            path_length: bytes.len() as u32,
        });
        string_table.extend_from_slice(bytes);
    }

    let string_table_size: u32 = string_table.len() as u32;

    // Node entries: copy the 4x4 transform out of the column-of-
    // arrays layout that gltf gives us into a flat [f32; 16]. Same
    // column-major convention either way (parent is column 0..3,
    // each column is a Vec4), so we lay it out column-by-column.
    let mut node_entries: Vec<KSceneNodeEntry> = Vec::with_capacity(nodes.len());
    for n in nodes {
        let mut t: [f32; 16] = [0.0; 16];
        for col in 0..4 {
            for row in 0..4 {
                t[col * 4 + row] = n.transform[col][row];
            }
        }
        node_entries.push(KSceneNodeEntry {
            mesh_index:     n.mesh_index,
            material_index: n.material_index,
            transform:      t,
        });
    }

    let header = KSceneHeader {
        magic:                 MAGIC_KSCENE,
        version:               KSCENE_VERSION,
        mesh_count,
        material_count,
        node_count,
        mesh_table_offset,
        material_table_offset,
        node_table_offset,
        string_table_offset,
        string_table_size,
    };

    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).map_err(|e| BakeError::io(parent, e))?;
    }
    let mut file: fs::File = fs::File::create(output).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::bytes_of(&header)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(&mesh_entries)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(&material_entries)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::cast_slice(&node_entries)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(&string_table).map_err(|e| BakeError::io(output, e))?;
    Ok(())
}
