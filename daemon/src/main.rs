//! axp22x-soft-battery: the daemon of the learning, `gen-dts` and `import`
//! (docs/plan.md of axp22x-soft-battery, "The program").
//!
//! A skeleton for the packages: it reads the command line and does no work
//! yet.

use std::process::ExitCode;

const USAGE: &str = "\
usage: axp22x-soft-battery daemon
       axp22x-soft-battery gen-dts [--use-partial]
       axp22x-soft-battery import <log>";

enum Command {
    Daemon,
    GenDts { use_partial: bool },
    Import { log: std::path::PathBuf },
}

fn parse() -> Result<Command, lexopt::Error> {
    use lexopt::prelude::*;
    let mut parser = lexopt::Parser::from_env();
    let name = match parser.next()? {
        Some(Value(v)) => v.string()?,
        Some(arg) => return Err(arg.unexpected()),
        None => return Err("no command".into()),
    };
    let command = match name.as_str() {
        "daemon" => Command::Daemon,
        "gen-dts" => {
            let mut use_partial = false;
            while let Some(arg) = parser.next()? {
                match arg {
                    Long("use-partial") => use_partial = true,
                    _ => return Err(arg.unexpected()),
                }
            }
            return Ok(Command::GenDts { use_partial });
        }
        "import" => Command::Import {
            log: parser.value()?.into(),
        },
        _ => return Err(format!("unknown command {name}").into()),
    };
    if let Some(arg) = parser.next()? {
        return Err(arg.unexpected());
    }
    Ok(command)
}

fn main() -> ExitCode {
    let command = match parse() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("{e}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    match command {
        Command::Daemon => eprintln!("daemon: not written yet"),
        Command::GenDts { use_partial } => {
            eprintln!("gen-dts (use-partial {use_partial}): not written yet")
        }
        Command::Import { log } => eprintln!("import {}: not written yet", log.display()),
    }
    ExitCode::FAILURE
}
