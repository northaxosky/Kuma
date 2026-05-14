//! End-to-end sound bake tests. Generates a tiny WAV in-memory,
//! writes it to a tempdir, runs bake_sound_pcm, and verifies the
//! resulting .ksound layout matches what the engine's load path
//! (parse_ksound_header) will see.

use kuma_bake::format::{
    KSoundHeader, AUDIO_FORMAT_PCM_F32, KSOUND_VERSION, MAGIC_KSOUND,
};
use std::path::PathBuf;

/// Build a minimal WAV file: PCM 16-bit, 8000 Hz, mono, 100 samples
/// of a 1 kHz sine. Spec: http://soundfile.sapp.org/doc/WaveFormat/
fn write_test_wav(path: &std::path::Path) {
    use std::io::Write;
    let sample_rate: u32 = 8000;
    let channels: u16 = 1;
    let bits_per_sample: u16 = 16;
    let frame_count: u32 = 100;

    let byte_rate: u32 = sample_rate * channels as u32 * bits_per_sample as u32 / 8;
    let block_align: u16 = channels * bits_per_sample / 8;
    let data_size: u32 = frame_count * block_align as u32;
    let chunk_size: u32 = 36 + data_size;

    let mut bytes: Vec<u8> = Vec::with_capacity(44 + data_size as usize);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&chunk_size.to_le_bytes());
    bytes.extend_from_slice(b"WAVE");
    bytes.extend_from_slice(b"fmt ");
    bytes.extend_from_slice(&16u32.to_le_bytes());          // fmt subchunk size
    bytes.extend_from_slice(&1u16.to_le_bytes());           // PCM
    bytes.extend_from_slice(&channels.to_le_bytes());
    bytes.extend_from_slice(&sample_rate.to_le_bytes());
    bytes.extend_from_slice(&byte_rate.to_le_bytes());
    bytes.extend_from_slice(&block_align.to_le_bytes());
    bytes.extend_from_slice(&bits_per_sample.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_size.to_le_bytes());

    // 1 kHz sine at 8 kHz sample rate = 8 samples per cycle, peak 0.8 amplitude
    for n in 0..frame_count {
        let t: f32 = n as f32 / sample_rate as f32;
        let s: f32 = (t * std::f32::consts::TAU * 1000.0).sin() * 0.8;
        let i: i16 = (s * i16::MAX as f32) as i16;
        bytes.extend_from_slice(&i.to_le_bytes());
    }

    std::fs::write(path, &bytes).expect("write test wav");
}

#[test]
fn bake_wav_produces_valid_pcm_ksound() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("test.wav");
    let out_path: PathBuf = tmp.path().join("test.ksound");
    write_test_wav(&in_path);

    kuma_bake::bake_sound_pcm(&in_path, &out_path).expect("bake should succeed");

    let bytes: Vec<u8> = std::fs::read(&out_path).expect("read baked file");
    assert!(bytes.len() >= std::mem::size_of::<KSoundHeader>());

    let hdr: KSoundHeader =
        *bytemuck::from_bytes(&bytes[..std::mem::size_of::<KSoundHeader>()]);
    assert_eq!(hdr.magic, MAGIC_KSOUND);
    assert_eq!(hdr.version, KSOUND_VERSION);
    assert_eq!(hdr.format, AUDIO_FORMAT_PCM_F32);
    assert_eq!(hdr.sample_rate, 8000);
    assert_eq!(hdr.channels, 1);
    assert_eq!(hdr.frame_count, 100);
    assert_eq!(hdr.payload_offset, 32);

    let expected_payload: u32 = 100 * 1 * 4;  // frames * channels * sizeof(f32)
    assert_eq!(hdr.payload_size, expected_payload);
    assert_eq!(bytes.len(), 32 + expected_payload as usize);

    // Spot-check that decoded samples are in the [-1, 1] range and
    // the sine peaks land near +/- 0.8 like our generator wrote.
    let samples: &[f32] =
        bytemuck::cast_slice(&bytes[32..32 + expected_payload as usize]);
    let max_abs: f32 = samples.iter().fold(0.0_f32, |acc, &s| acc.max(s.abs()));
    assert!(max_abs > 0.5, "sine peak too low: {max_abs}");
    assert!(max_abs <= 1.0, "sample out of [-1, 1]: {max_abs}");
}

#[test]
fn bake_sound_dispatches_wav_to_pcm() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("dispatch.wav");
    let out_path: PathBuf = tmp.path().join("dispatch.ksound");
    write_test_wav(&in_path);

    kuma_bake::bake_sound(&in_path, &out_path).expect("dispatch should succeed");

    let bytes: Vec<u8> = std::fs::read(&out_path).expect("read");
    let hdr: KSoundHeader =
        *bytemuck::from_bytes(&bytes[..std::mem::size_of::<KSoundHeader>()]);
    assert_eq!(hdr.format, AUDIO_FORMAT_PCM_F32);
    assert!(hdr.frame_count > 0);
}

#[test]
fn unknown_extension_rejected() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("song.flac");
    let out_path: PathBuf = tmp.path().join("song.ksound");
    std::fs::write(&in_path, b"not really a file").expect("write");

    let err = kuma_bake::bake_sound(&in_path, &out_path)
        .expect_err("unknown extension should be rejected");
    let msg: String = err.to_string();
    assert!(msg.contains("unsupported"), "msg was: {msg}");
}

#[test]
fn passthrough_on_wav_returns_error() {
    let tmp: tempfile::TempDir = tempfile::tempdir().expect("tempdir");
    let in_path: PathBuf = tmp.path().join("nope.wav");
    let out_path: PathBuf = tmp.path().join("nope.ksound");
    write_test_wav(&in_path);

    let err = kuma_bake::bake_sound_passthrough(&in_path, &out_path)
        .expect_err("WAV passthrough should be rejected");
    let msg: String = err.to_string();
    assert!(msg.to_lowercase().contains("wav"), "msg was: {msg}");
}
