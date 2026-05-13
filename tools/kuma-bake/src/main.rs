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
    /// Convert a glTF / GLB mesh into a .kmesh binary
    Gltf { input: PathBuf, output: PathBuf },
}

fn main() -> ExitCode {
    let cli: Cli = Cli::parse();

    let result: Result<(), kuma_bake::BakeError> = match cli.command {
        Command::Mesh { input, output } => kuma_bake::bake_mesh(&input, &output),
        Command::Tex  { input, output } => kuma_bake::bake_texture(&input, &output),
        Command::Gltf { input, output } => kuma_bake::bake_gltf(&input, &output),
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("kuma-bake: {e}");
            ExitCode::FAILURE
        }
    }
}
