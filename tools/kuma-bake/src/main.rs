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
}

fn main() -> ExitCode {
    let cli: Cli = Cli::parse();

    let result: Result<(), kuma_bake::BakeError> = match cli.command {
        Command::Mesh { input, output } => kuma_bake::bake_mesh(&input, &output),
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("kuma-bake: {e}");
            ExitCode::FAILURE
        }
    }
}
