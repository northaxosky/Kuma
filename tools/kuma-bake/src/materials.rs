//! glTF material extraction -> .kmaterial files + sibling .ktex
//! files for the diffuse texture of each material.
//!
//! The current renderer only samples the diffuse slot, so the bake
//! intentionally extracts ONLY diffuse textures. Other slots (normal,
//! metallic-roughness, occlusion, emissive) are still recorded as
//! factor scalars in the .kmaterial header so a future lit shader
//! can read them, but their texture references are dropped to keep
//! Sponza's bake output around 25 .ktex files (~100MB) instead of
//! ~125 (~400MB). Lazy loading of those slots is on followups.md.
//!
//! Texture dedup: glTF materials can share the same image (Sponza
//! has several materials referencing the same diffuse jpg). Images
//! are keyed by their glTF document index so each .ktex is written
//! exactly once.

use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::error::BakeError;
use crate::format::{
    KMaterialHeader, ALPHA_MODE_BLEND, ALPHA_MODE_MASK, ALPHA_MODE_OPAQUE, KMATERIAL_VERSION,
    MAGIC_KMATERIAL, MATERIAL_FLAG_DOUBLE_SIDED,
};
use crate::texture::bake_texture_from_memory;

/// Result of materials extraction: one relative path per glTF
/// material in document order. The scene baker writes these into
/// the .kscene's material table and uses each primitive's
/// `material().index()` to look them up.
pub(crate) struct MaterialBakeOutput {
    pub paths: Vec<String>,
}

/// Walk every material in `document`, bake any referenced diffuse
/// images into sibling .ktex files, and emit one .kmaterial per
/// material into `materials_dir_abs`. Paths returned are relative
/// to the .kscene's parent directory (matching the convention
/// scene::resolve_relative_path expects at runtime).
#[allow(clippy::too_many_arguments)]
pub(crate) fn bake_materials(
    document: &gltf::Document,
    buffers: &[gltf::buffer::Data],
    input: &Path,
    materials_dir_name: &str,
    materials_dir_abs: &Path,
    textures_dir_name: &str,
    textures_dir_abs: &Path,
) -> Result<MaterialBakeOutput, BakeError> {
    let mut paths: Vec<String> = Vec::new();
    // image index -> relative path (relative to the .kmaterial file's
    // directory, which is materials_dir_abs). For Sponza this caps
    // texture writes at ~25 even though there are ~100 mesh primitives.
    let mut image_cache: HashMap<usize, String> = HashMap::new();

    let base_dir: &Path = input.parent().unwrap_or_else(|| Path::new("."));

    for material in document.materials() {
        let mat_index = material.index();
        if mat_index.is_none() {
            // glTF's "default material" (Material::index() == None).
            // No primitive references it by an index that lands in
            // our paths vec - the scene baker uses
            // KSCENE_NO_MATERIAL for such primitives, so we skip
            // emitting a file here.
            continue;
        }
        let mat_index: usize = mat_index.unwrap();

        // Defensive: if a material is sandwiched between two None
        // indices, paths.len() would drift out of sync with mat_index.
        // glTF guarantees materials are 0..N contiguous so this
        // assertion never fires - but the guard makes the invariant
        // explicit for readers.
        assert_eq!(paths.len(), mat_index, "glTF material indices must be contiguous");

        let pbr = material.pbr_metallic_roughness();

        // Diffuse texture (the only slot we actually bake for now).
        // Texture loads that fail (missing source jpg, broken URI)
        // degrade to "no diffuse" so a partial asset checkout still
        // produces a valid .kmaterial - the runtime falls back to
        // the renderer's white default for the diffuse slot.
        let diffuse_relative: Option<String> = if let Some(info) = pbr.base_color_texture() {
            match resolve_or_bake_image(
                &info.texture(),
                buffers,
                base_dir,
                textures_dir_name,
                textures_dir_abs,
                materials_dir_name,
                &mut image_cache,
            ) {
                Ok(rel) => Some(rel),
                Err(BakeError::Io { path, .. }) => {
                    eprintln!(
                        "kuma-bake: material #{mat_index} diffuse texture missing ({}); \
                         baking material with no diffuse - runtime will use white default",
                        path.display()
                    );
                    None
                }
                Err(other) => return Err(other),
            }
        } else {
            None
        };

        let alpha_mode: u32 = match material.alpha_mode() {
            gltf::material::AlphaMode::Opaque => ALPHA_MODE_OPAQUE,
            gltf::material::AlphaMode::Mask   => ALPHA_MODE_MASK,
            gltf::material::AlphaMode::Blend  => ALPHA_MODE_BLEND,
        };

        let mut flags: u32 = 0;
        if material.double_sided() {
            flags |= MATERIAL_FLAG_DOUBLE_SIDED;
        }

        // Build the string table. Only the diffuse path is recorded.
        // Unused slot offsets stay zero with length zero, which
        // KMaterialHeader treats as "this slot is unused".
        let mut string_table: Vec<u8> = Vec::new();
        let (diffuse_offset, diffuse_length): (u32, u32) = match &diffuse_relative {
            Some(path) => {
                let off: u32 = string_table.len() as u32;
                let len: u32 = path.len() as u32;
                string_table.extend_from_slice(path.as_bytes());
                (off, len)
            }
            None => (0, 0),
        };

        let header = KMaterialHeader {
            magic:                          MAGIC_KMATERIAL,
            version:                        KMATERIAL_VERSION,
            flags,
            alpha_mode,

            base_color:                     pbr.base_color_factor(),
            alpha_cutoff:                   material.alpha_cutoff().unwrap_or(0.5),
            metallic_factor:                pbr.metallic_factor(),
            roughness_factor:               pbr.roughness_factor(),
            normal_scale:                   material
                .normal_texture()
                .map(|n| n.scale())
                .unwrap_or(1.0),
            occlusion_strength:             material
                .occlusion_texture()
                .map(|o| o.strength())
                .unwrap_or(1.0),
            emissive_factor:                material.emissive_factor(),

            diffuse_path_offset:            diffuse_offset,
            diffuse_path_length:            diffuse_length,
            normal_path_offset:             0,
            normal_path_length:             0,
            metallic_roughness_path_offset: 0,
            metallic_roughness_path_length: 0,
            occlusion_path_offset:          0,
            occlusion_path_length:          0,
            emissive_path_offset:           0,
            emissive_path_length:           0,

            string_table_size: string_table.len() as u32,
        };

        fs::create_dir_all(materials_dir_abs)
            .map_err(|e| BakeError::io(materials_dir_abs, e))?;
        let out_path: PathBuf = materials_dir_abs.join(format!("{mat_index}.kmaterial"));
        let mut file: fs::File = fs::File::create(&out_path)
            .map_err(|e| BakeError::io(&out_path, e))?;
        file.write_all(bytemuck::bytes_of(&header))
            .map_err(|e| BakeError::io(&out_path, e))?;
        file.write_all(&string_table)
            .map_err(|e| BakeError::io(&out_path, e))?;

        paths.push(format!("{materials_dir_name}/{mat_index}.kmaterial"));
    }

    Ok(MaterialBakeOutput { paths })
}

// Bake (or look up in cache) the image referenced by a glTF
// texture, returning the .ktex path RELATIVE to the .kmaterial
// file's directory. The .kmaterial sits in
// `<scene_dir>/<materials_dir_name>/`, the .ktex in
// `<scene_dir>/<textures_dir_name>/`, so the relative path goes up
// one level: "../<textures_dir_name>/<idx>.ktex".
fn resolve_or_bake_image(
    texture: &gltf::Texture<'_>,
    buffers: &[gltf::buffer::Data],
    gltf_base_dir: &Path,
    textures_dir_name: &str,
    textures_dir_abs: &Path,
    materials_dir_name: &str,
    image_cache: &mut HashMap<usize, String>,
) -> Result<String, BakeError> {
    let image = texture.source();
    let img_index: usize = image.index();

    if let Some(path) = image_cache.get(&img_index) {
        return Ok(path.clone());
    }

    // Decode image bytes to memory (URI-on-disk or buffer-view embed).
    let bytes: Vec<u8> = match image.source() {
        gltf::image::Source::Uri { uri, .. } => {
            if let Some(rest) = uri.strip_prefix("data:") {
                decode_image_data_uri(rest)?
            } else {
                let resolved: PathBuf = gltf_base_dir.join(uri);
                fs::read(&resolved).map_err(|e| BakeError::io(resolved, e))?
            }
        }
        gltf::image::Source::View { view, .. } => {
            let buffer_data: &[u8] = &buffers[view.buffer().index()].0;
            let start = view.offset();
            let end = start + view.length();
            buffer_data[start..end].to_vec()
        }
    };

    fs::create_dir_all(textures_dir_abs)
        .map_err(|e| BakeError::io(textures_dir_abs, e))?;
    let ktex_path: PathBuf = textures_dir_abs.join(format!("{img_index}.ktex"));
    bake_texture_from_memory(&bytes, &ktex_path)?;

    // Path stored is relative to the .kmaterial's own directory.
    // .kmaterials live in <materials_dir>, .ktexes live in
    // <textures_dir>; both are siblings of the .kscene. So a
    // .kmaterial pointing at a .ktex must hop up one level and
    // back down into the textures dir.
    let _ = materials_dir_name;  // kept for future use if dirs change
    let relative: String = format!("../{textures_dir_name}/{img_index}.ktex");
    image_cache.insert(img_index, relative.clone());
    Ok(relative)
}

// Same shape as the buffer data-URI decoder in scene.rs but kept
// local to avoid pulling that helper out into a shared module for
// just one extra caller. Very rarely hit (most glTFs use external
// or buffer-embedded textures, not data URIs).
fn decode_image_data_uri(rest: &str) -> Result<Vec<u8>, BakeError> {
    let comma = rest.find(',').ok_or_else(|| {
        BakeError::Invalid("image data URI missing comma".to_string())
    })?;
    let (meta, payload) = rest.split_at(comma);
    let payload = &payload[1..];
    if !meta.contains(";base64") {
        return Err(BakeError::Invalid(
            "non-base64 image data URIs not supported".to_string(),
        ));
    }
    decode_base64(payload).map_err(BakeError::Invalid)
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
