//! glTF / GLB -> .kmesh conversion.
//!
//! The real implementation lands in commit 2. This file currently
//! exposes a stub that errors with "not yet implemented" so the
//! CLI surface is stable and CMake helpers can wire up against
//! the final symbol.

use crate::error::BakeError;

use std::path::Path;

pub fn bake_gltf(_input: &Path, _output: &Path) -> Result<(), BakeError> {
    Err(BakeError::Invalid(
        "glTF baker not yet implemented (skeleton commit; real conversion in next commit)".into(),
    ))
}
