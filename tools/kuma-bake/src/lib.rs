//! kuma-bake library entry point.
//!
//! The CLI in `main.rs` is a thin shim over the public functions
//! re-exported from this module, so unit and integration tests can
//! exercise the converters directly without spawning subprocesses.

pub mod error;
pub mod format;

pub use error::BakeError;
