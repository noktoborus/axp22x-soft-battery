//! The files of the runs:
//!
//! ```text
//! discharge/full/     the last 5 valid runs to the silence
//! discharge/archive/  the valid runs past 5
//! discharge/invalid/  the runs that failed the check
//! discharge/partial/  the runs cut short
//! charge/full/        the last 5 charges that reached the end
//! charge/archive/     those past 5
//! charge/invalid/     those that failed the check
//! ```
//!
//! A file is `<seq>.env`, six digits; seq is 1 + the highest of the kind.
//! full/ keeps 5, the others 100: past it the least seq goes.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::envfile::EnvFile;
use crate::Result;

const FULL_KEEP: usize = 5;
const OTHER_KEEP: usize = 100;

pub const DISCHARGE: &str = "discharge";
pub const CHARGE: &str = "charge";

fn seqs(dir: &Path) -> Vec<u32> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut seqs: Vec<u32> = entries
        .flatten()
        .filter_map(|e| {
            let name = e.file_name().into_string().ok()?;
            name.strip_suffix(".env")?.parse().ok()
        })
        .collect();
    seqs.sort_unstable();
    seqs
}

fn file(dir: &Path, seq: u32) -> PathBuf {
    dir.join(format!("{seq:06}.env"))
}

fn next_seq(kind_dir: &Path) -> u32 {
    let subs = fs::read_dir(kind_dir).into_iter().flatten().flatten();
    subs.flat_map(|e| seqs(&e.path())).max().unwrap_or(0) + 1
}

fn sync_dir(dir: &Path) -> Result<()> {
    fs::File::open(dir)?.sync_all()?;
    Ok(())
}

fn write_synced(path: &Path, text: &str) -> Result<()> {
    let tmp = path.with_extension("tmp");
    let mut f = fs::File::create(&tmp)?;
    f.write_all(text.as_bytes())?;
    f.sync_all()?;
    fs::rename(&tmp, path)?;
    sync_dir(path.parent().ok_or("no directory")?)
}

/// Past 100 files the least seq is deleted.
fn trim(dir: &Path) -> Result<()> {
    let s = seqs(dir);
    for seq in s.iter().take(s.len().saturating_sub(OTHER_KEEP)) {
        fs::remove_file(file(dir, *seq))?;
    }
    Ok(())
}

/// Past 5 in full/ the least seq goes to archive/.
fn rotate_full(kind_dir: &Path) -> Result<()> {
    let full = kind_dir.join("full");
    let archive = kind_dir.join("archive");
    let s = seqs(&full);
    for seq in s.iter().take(s.len().saturating_sub(FULL_KEEP)) {
        fs::create_dir_all(&archive)?;
        fs::rename(file(&full, *seq), file(&archive, *seq))?;
        println!("{}: run {seq:06} to archive/", kind_dir.display());
    }
    trim(&archive)
}

/// Saves a run in <dir>/<kind>/<sub>/; the path of its file.
pub fn save(dir: &Path, kind: &str, sub: &str, env: &mut EnvFile) -> Result<PathBuf> {
    let kind_dir = dir.join(kind);
    let sub_dir = kind_dir.join(sub);
    fs::create_dir_all(&sub_dir)?;
    let seq = next_seq(&kind_dir);
    env.set("SEQ", format!("{seq:06}"));
    let path = file(&sub_dir, seq);
    write_synced(&path, &env.text())?;
    if sub == "full" {
        rotate_full(&kind_dir)?;
    } else {
        trim(&sub_dir)?;
    }
    Ok(path)
}

/// The runs of <dir>/<kind>/<sub>/, by seq.
pub fn load(dir: &Path, kind: &str, sub: &str) -> Result<Vec<(PathBuf, EnvFile)>> {
    let sub_dir = dir.join(kind).join(sub);
    seqs(&sub_dir)
        .into_iter()
        .map(|seq| {
            let path = file(&sub_dir, seq);
            let text = fs::read_to_string(&path)?;
            let env = EnvFile::parse(&text).map_err(|e| format!("{}: {e}", path.display()))?;
            Ok((path, env))
        })
        .collect()
}
