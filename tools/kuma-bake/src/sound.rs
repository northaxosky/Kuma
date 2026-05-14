//! Sound baker.
//!
//! Converts source audio (.wav, .ogg) into the engine's .ksound
//! binary format. Two output paths:
//!
//! - **PCM bake** (default for .wav, opt-in for .ogg via --decode):
//!   Symphonia decodes the source to interleaved f32 samples,
//!   normalized to [-1, 1]. Engine reads samples into a miniaudio
//!   audio buffer at load with zero decode work. Best for short SFX
//!   where load time and play latency matter more than disk size.
//!
//! - **OGG passthrough** (default for .ogg): the original encoded
//!   bytes are copied into the .ksound payload as-is. Engine hands
//!   them to miniaudio's decoder at load time. Best for long music
//!   tracks where ~10x compression is worth the one-time decode
//!   cost at load.

use std::fs;
use std::io::Write;
use std::path::Path;

use symphonia::core::audio::{AudioBufferRef, Signal};
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::errors::Error as SymphoniaError;
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;

use crate::error::BakeError;
use crate::format::{
    KSoundHeader, AUDIO_FORMAT_OGG, AUDIO_FORMAT_PCM_F32, KSOUND_VERSION, MAGIC_KSOUND,
};

// ── Public entry points ─────────────────────────────────────────

/// Bake an audio file to .ksound. Picks PCM vs passthrough based on
/// the source extension: .wav -> PCM, .ogg -> passthrough. Override
/// with `bake_sound_pcm` / `bake_sound_passthrough` for explicit
/// control (useful when an .ogg should be decoded for fast SFX play).
pub fn bake_sound(input: &Path, output: &Path) -> Result<(), BakeError> {
    let ext: String = input
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();

    match ext.as_str() {
        "ogg" => bake_sound_passthrough(input, output),
        "wav" => bake_sound_pcm(input, output),
        other => Err(BakeError::Invalid(format!(
            "unsupported audio extension '{other}' - expected .wav or .ogg"
        ))),
    }
}

/// Decode the source to interleaved f32 PCM via Symphonia and write
/// to .ksound with format=PCM_F32. Works for any extension Symphonia
/// can decode (.wav, .ogg).
pub fn bake_sound_pcm(input: &Path, output: &Path) -> Result<(), BakeError> {
    let decoded: DecodedAudio = decode_to_pcm(input)?;
    write_pcm_ksound(output, &decoded)
}

/// Copy the source bytes as-is into a .ksound with the appropriate
/// compressed format flag. Probe via Symphonia to extract sample
/// rate + channel count for the header without decoding the payload.
pub fn bake_sound_passthrough(input: &Path, output: &Path) -> Result<(), BakeError> {
    let probe: ProbeInfo = probe_source(input)?;
    let bytes: Vec<u8> = fs::read(input).map_err(|e| BakeError::io(input, e))?;

    let format: u32 = match probe.container {
        Container::Ogg => AUDIO_FORMAT_OGG,
        Container::Wav => {
            return Err(BakeError::Invalid(
                "WAV passthrough makes no sense - WAV is already raw PCM. Use bake_sound_pcm instead.".into(),
            ));
        }
    };

    let header_size: u32 = std::mem::size_of::<KSoundHeader>() as u32;
    let header: KSoundHeader = KSoundHeader {
        magic:          MAGIC_KSOUND,
        version:        KSOUND_VERSION,
        format,
        sample_rate:    probe.sample_rate,
        channels:       probe.channels,
        frame_count:    0,  // compressed: backend computes at load
        payload_offset: header_size,
        payload_size:   bytes.len() as u32,
    };
    write_ksound(output, &header, &bytes)
}

// ── Internals ───────────────────────────────────────────────────

#[derive(Debug)]
struct DecodedAudio {
    samples: Vec<f32>,    // interleaved frame-major
    sample_rate: u32,
    channels: u32,
    frame_count: u32,
}

#[derive(Debug, PartialEq)]
enum Container {
    Wav,
    Ogg,
}

#[derive(Debug)]
struct ProbeInfo {
    container: Container,
    sample_rate: u32,
    channels: u32,
}

/// Probe a source file's container and stream info without decoding
/// the audio payload. Used by passthrough mode to fill in the .ksound
/// header.
fn probe_source(input: &Path) -> Result<ProbeInfo, BakeError> {
    let file: fs::File = fs::File::open(input).map_err(|e| BakeError::io(input, e))?;
    let mss: MediaSourceStream = MediaSourceStream::new(Box::new(file), Default::default());

    let mut hint: Hint = Hint::new();
    if let Some(ext) = input.extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }

    let probed = symphonia::default::get_probe().format(
        &hint,
        mss,
        &FormatOptions::default(),
        &MetadataOptions::default(),
    )?;

    let track = probed
        .format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .ok_or_else(|| BakeError::Invalid("no decodable audio track found".into()))?;

    let sample_rate: u32 = track
        .codec_params
        .sample_rate
        .ok_or_else(|| BakeError::Invalid("source missing sample_rate".into()))?;
    let channels: u32 = track
        .codec_params
        .channels
        .map(|c| c.count() as u32)
        .ok_or_else(|| BakeError::Invalid("source missing channel count".into()))?;

    if channels != 1 && channels != 2 {
        return Err(BakeError::Invalid(format!(
            "unsupported channel count {channels} - expected 1 (mono) or 2 (stereo)"
        )));
    }

    let ext: String = input
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    let container: Container = match ext.as_str() {
        "wav" => Container::Wav,
        "ogg" => Container::Ogg,
        other => {
            return Err(BakeError::Invalid(format!(
                "unsupported container '{other}'"
            )));
        }
    };

    Ok(ProbeInfo { container, sample_rate, channels })
}

/// Full Symphonia decode pipeline: open file, probe format, walk
/// every packet, convert decoded samples to f32 interleaved.
fn decode_to_pcm(input: &Path) -> Result<DecodedAudio, BakeError> {
    let file: fs::File = fs::File::open(input).map_err(|e| BakeError::io(input, e))?;
    let mss: MediaSourceStream = MediaSourceStream::new(Box::new(file), Default::default());

    let mut hint: Hint = Hint::new();
    if let Some(ext) = input.extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }

    let mut probed = symphonia::default::get_probe().format(
        &hint,
        mss,
        &FormatOptions::default(),
        &MetadataOptions::default(),
    )?;

    let track = probed
        .format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .ok_or_else(|| BakeError::Invalid("no decodable audio track found".into()))?;

    let track_id: u32 = track.id;
    let sample_rate: u32 = track
        .codec_params
        .sample_rate
        .ok_or_else(|| BakeError::Invalid("source missing sample_rate".into()))?;
    let channels: u32 = track
        .codec_params
        .channels
        .map(|c| c.count() as u32)
        .ok_or_else(|| BakeError::Invalid("source missing channel count".into()))?;

    if channels != 1 && channels != 2 {
        return Err(BakeError::Invalid(format!(
            "unsupported channel count {channels} - expected 1 (mono) or 2 (stereo)"
        )));
    }

    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())?;

    let mut samples: Vec<f32> = Vec::new();

    loop {
        let packet = match probed.format.next_packet() {
            Ok(p) => p,
            // End of stream is the only "expected" error here -
            // Symphonia signals it via an IO error with kind=UnexpectedEof.
            Err(SymphoniaError::IoError(ref io)) if io.kind() == std::io::ErrorKind::UnexpectedEof => {
                break;
            }
            Err(e) => return Err(BakeError::Audio(e)),
        };

        if packet.track_id() != track_id {
            continue;
        }

        let decoded: AudioBufferRef<'_> = decoder.decode(&packet)?;
        append_interleaved_f32(&decoded, channels as usize, &mut samples);
    }

    if samples.is_empty() {
        return Err(BakeError::Invalid(
            "audio decoded to zero samples - source may be empty".into(),
        ));
    }

    let frame_count: u32 = (samples.len() / channels as usize) as u32;

    Ok(DecodedAudio { samples, sample_rate, channels, frame_count })
}

/// Symphonia returns AudioBufferRef in whatever sample format the
/// codec produces (i16, i32, f32, etc.). Convert each variant to
/// f32 in [-1, 1] and append in interleaved frame-major order.
fn append_interleaved_f32(buf: &AudioBufferRef<'_>, channels: usize, out: &mut Vec<f32>) {
    macro_rules! interleave {
        ($buf:expr) => {{
            let frames: usize = $buf.frames();
            for frame in 0..frames {
                for ch in 0..channels {
                    let s: f32 = $buf.chan(ch)[frame].into_f32();
                    out.push(s);
                }
            }
        }};
    }

    match buf {
        AudioBufferRef::U8(b)  => interleave!(b),
        AudioBufferRef::U16(b) => interleave!(b),
        AudioBufferRef::U24(b) => interleave!(b),
        AudioBufferRef::U32(b) => interleave!(b),
        AudioBufferRef::S8(b)  => interleave!(b),
        AudioBufferRef::S16(b) => interleave!(b),
        AudioBufferRef::S24(b) => interleave!(b),
        AudioBufferRef::S32(b) => interleave!(b),
        AudioBufferRef::F32(b) => interleave!(b),
        AudioBufferRef::F64(b) => interleave!(b),
    }
}

/// Trait shim so the macro above can call `into_f32()` on every
/// Symphonia sample type uniformly.
trait IntoF32 {
    fn into_f32(self) -> f32;
}

macro_rules! impl_into_f32 {
    ($t:ty) => {
        impl IntoF32 for $t {
            fn into_f32(self) -> f32 {
                symphonia::core::conv::FromSample::from_sample(self)
            }
        }
    };
}
impl_into_f32!(u8);
impl_into_f32!(u16);
impl_into_f32!(symphonia::core::sample::u24);
impl_into_f32!(u32);
impl_into_f32!(i8);
impl_into_f32!(i16);
impl_into_f32!(symphonia::core::sample::i24);
impl_into_f32!(i32);
impl_into_f32!(f32);
impl_into_f32!(f64);

fn write_pcm_ksound(output: &Path, decoded: &DecodedAudio) -> Result<(), BakeError> {
    let payload_bytes: &[u8] = bytemuck::cast_slice(&decoded.samples);
    let header_size: u32 = std::mem::size_of::<KSoundHeader>() as u32;
    let header: KSoundHeader = KSoundHeader {
        magic:          MAGIC_KSOUND,
        version:        KSOUND_VERSION,
        format:         AUDIO_FORMAT_PCM_F32,
        sample_rate:    decoded.sample_rate,
        channels:       decoded.channels,
        frame_count:    decoded.frame_count,
        payload_offset: header_size,
        payload_size:   payload_bytes.len() as u32,
    };
    write_ksound(output, &header, payload_bytes)
}

fn write_ksound(output: &Path, header: &KSoundHeader, payload: &[u8]) -> Result<(), BakeError> {
    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).map_err(|e| BakeError::io(parent, e))?;
    }
    let mut file: fs::File =
        fs::File::create(output).map_err(|e| BakeError::io(output, e))?;
    file.write_all(bytemuck::bytes_of(header))
        .map_err(|e| BakeError::io(output, e))?;
    file.write_all(payload).map_err(|e| BakeError::io(output, e))?;
    Ok(())
}

// Re-exports so the lib root can `pub use` cleanly without exposing
// internals.
pub use bake_sound as _re_export_anchor;
