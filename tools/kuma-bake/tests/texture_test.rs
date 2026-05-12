//! End-to-end texture bake tests. Generates a tiny PNG in-memory,
//! writes it to a tempdir, runs bake_texture, and verifies the
//! resulting .ktex layout matches what the engine's
//! load_texture_binary will see.

use kuma_bake::format::{FORMAT_RGBA8, KTexHeader, KTEX_VERSION, MAGIC_KTEX};
use std::path::PathBuf;

/// Generates a 4x4 PNG at `path` with a recognisable color pattern
/// (so we can verify the pixels survived encode -> decode -> bake).
fn write_test_png(path: &std::path::Path) {
    let mut img: image::RgbaImage = image::RgbaImage::new(4, 4);
    for (x, y, p) in img.enumerate_pixels_mut() {
        *p = image::Rgba([(x * 60) as u8, (y * 60) as u8, 128, 255]);
    }
    img.save(path).expect("write test png");
}

#[test]
fn bake_png_produces_valid_ktex() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("test.png");
    let out_path: PathBuf = tmp.path().join("test.ktex");
    write_test_png(&in_path);

    kuma_bake::bake_texture(&in_path, &out_path).expect("bake should succeed");

    let bytes: Vec<u8> = std::fs::read(&out_path).expect("read baked file");

    let hdr: KTexHeader = *bytemuck::from_bytes(&bytes[..std::mem::size_of::<KTexHeader>()]);
    assert_eq!(hdr.magic, MAGIC_KTEX);
    assert_eq!(hdr.version, KTEX_VERSION);
    assert_eq!(hdr.width, 4);
    assert_eq!(hdr.height, 4);
    assert_eq!(hdr.format, FORMAT_RGBA8);
    assert_eq!(hdr.pixel_offset, 32);

    // 32 byte header + 4*4*4 = 64 bytes of pixels = 96 total.
    assert_eq!(bytes.len(), 96);
}

#[test]
fn bake_png_pixels_round_trip() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("test.png");
    let out_path: PathBuf = tmp.path().join("test.ktex");
    write_test_png(&in_path);

    kuma_bake::bake_texture(&in_path, &out_path).unwrap();
    let bytes: Vec<u8> = std::fs::read(&out_path).unwrap();
    let hdr: KTexHeader = *bytemuck::from_bytes(&bytes[..32]);
    let pixels: &[u8] = &bytes[hdr.pixel_offset as usize..];

    // Top-left pixel: (0*60, 0*60, 128, 255) = (0, 0, 128, 255).
    assert_eq!(&pixels[0..4], &[0, 0, 128, 255]);
    // Bottom-right pixel: x=3, y=3 -> (180, 180, 128, 255).
    let last_idx: usize = (4 * 4 - 1) * 4;
    assert_eq!(&pixels[last_idx..last_idx + 4], &[180, 180, 128, 255]);
}

#[test]
fn bake_missing_file_returns_error() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let out_path: PathBuf = tmp.path().join("nope.ktex");
    let err = kuma_bake::bake_texture(&PathBuf::from("does/not/exist.png"), &out_path)
        .expect_err("should fail");
    let msg: String = err.to_string();
    assert!(msg.contains("does/not/exist.png") || msg.contains("does\\not\\exist.png"),
            "expected path in error message, got: {msg}");
}
