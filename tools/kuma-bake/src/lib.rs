//! kuma-bake library entry point.
//!
//! The CLI in `main.rs` is a thin shim over the public functions
//! re-exported from this module, so unit and integration tests can
//! exercise the converters directly without spawning subprocesses.

pub mod error;
pub mod format;
pub mod gltf;
pub mod mesh;
pub mod sound;
pub mod texture;

pub use error::BakeError;
pub use gltf::bake_gltf;
pub use mesh::bake_mesh;
pub use sound::{bake_sound, bake_sound_passthrough, bake_sound_pcm};
pub use texture::bake_texture;
