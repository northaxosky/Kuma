//! kuma-bake: command-line asset baker for the Kuma engine.
//!
//! Subcommands:
//!   mesh <input.obj>  <output.kmesh>
//!   tex  <input.png>  <output.ktex>     (lands in commit 4)

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

#[derive(Parser)]
#[command(name = "kuma-bake")]
#[command(about = "Bake source assets into Kuma engine binary format")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Convert an OBJ mesh into a .kmesh binary
    Mesh { input: PathBuf, output: PathBuf },
    /// Convert a PNG/JPEG/TGA image into a .ktex binary (RGBA8)
    Tex { input: PathBuf, output: PathBuf },
    /// Convert a single-mesh glTF / GLB into a .kmesh binary (transform baked in)
    Gltf { input: PathBuf, output: PathBuf },
    /// Convert a multi-mesh glTF / GLB scene into a .kscene + sibling .kmesh files
    Scene { input: PathBuf, output: PathBuf },
    /// Convert a .wav (decoded to PCM) or .ogg (passthrough) into a .ksound binary
    Sound { input: PathBuf, output: PathBuf },
}

fn main() -> ExitCode {
    let cli: Cli = Cli::parse();

    let result: Result<(), kuma_bake::BakeError> = match cli.command {
        Command::Mesh  { input, output } => kuma_bake::bake_mesh(&input, &output),
        Command::Tex   { input, output } => kuma_bake::bake_texture(&input, &output),
        Command::Gltf  { input, output } => kuma_bake::bake_gltf(&input, &output),
        Command::Scene { input, output } => kuma_bake::bake_scene(&input, &output),
        Command::Sound { input, output } => kuma_bake::bake_sound(&input, &output),
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("kuma-bake: {e}");
            ExitCode::FAILURE
        }
    }
}
