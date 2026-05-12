//! Error types for kuma-bake.
//!
//! Every fallible function returns `Result<T, BakeError>`; the `?`
//! operator propagates with automatic conversion via `#[from]`.
//! Error messages carry the failing file path where useful so the
//! CLI output is actionable.

use std::path::PathBuf;

#[derive(thiserror::Error, Debug)]
pub enum BakeError {
    #[error("file I/O failed for {path}: {source}")]
    Io {
        path: PathBuf,
        #[source]
        source: std::io::Error,
    },

    #[error("OBJ parse failed: {0}")]
    Obj(#[from] tobj::LoadError),

    #[error("image decode failed: {0}")]
    Image(#[from] image::ImageError),

    #[error("invalid input: {0}")]
    Invalid(String),
}

impl BakeError {
    /// Wrap a `std::io::Error` with the path that failed - the bare
    /// io::Error doesn't include filename info, which makes errors
    /// useless ("file not found: file not found").
    pub fn io(path: impl Into<PathBuf>, source: std::io::Error) -> Self {
        BakeError::Io { path: path.into(), source }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io;

    #[test]
    fn io_error_includes_path_in_message() {
        let err: BakeError = BakeError::io(
            "assets/source/missing.obj",
            io::Error::new(io::ErrorKind::NotFound, "no such file"),
        );
        let msg: String = err.to_string();
        assert!(msg.contains("assets/source/missing.obj"), "msg was: {msg}");
        assert!(msg.contains("no such file"), "msg was: {msg}");
    }

    #[test]
    fn invalid_error_carries_message() {
        let err: BakeError = BakeError::Invalid("mesh has zero vertices".into());
        assert_eq!(err.to_string(), "invalid input: mesh has zero vertices");
    }
}
