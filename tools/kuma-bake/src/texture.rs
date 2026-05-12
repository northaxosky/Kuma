//! PNG/JPEG/TGA -> .ktex conversion. Always outputs RGBA8 in v1.
//!
//! Heavy lifting done by the `image` crate: decode any common
//! source format, convert to RGBA8, write a 32-byte header
//! followed by raw pixel bytes (top-left origin, row-major).

use crate::error::BakeError;
use crate::format::{FORMAT_RGBA8, KTEX_VERSION, KTexHeader, MAGIC_KTEX};

use std::fs;
use std::io::Write;
use std::path::Path;

/// Bake a single image file into a `.ktex`. Creates parent
/// directories of `output` if they don't exist.
pub fn bake_texture(input: &Path, output: &Path) -> Result<(), BakeError> {
    let bytes: Vec<u8> = fs::read(input).map_err(|e| BakeError::io(input, e))?;
    let img: image::DynamicImage = image::load_from_memory(&bytes)?;
    let rgba: image::RgbaImage = img.to_rgba8();

    let header: KTexHeader = KTexHeader {
        magic:        MAGIC_KTEX,
        version:      KTEX_VERSION,
        width:        rgba.width(),
        height:       rgba.height(),
        format:       FORMAT_RGBA8,
        pixel_offset: std::mem::size_of::<KTexHeader>() as u32,
        _reserved:    [0; 2],
    };

    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).map_err(|e| BakeError::io(parent, e))?;
    }
    let mut file: fs::File = fs::File::create(output).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::bytes_of(&header)).map_err(|e| BakeError::io(output, e))?;
    file.write_all(rgba.as_raw()).map_err(|e| BakeError::io(output, e))?;
    Ok(())
}
