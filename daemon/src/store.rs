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
//! full/ keeps 5, the others 100: past it the least seq goes. Past 5 in
//! charge/full/ the short charges go first (`leave_order`).

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::envfile::EnvFile;
use crate::Result;

const FULL_KEEP: usize = 5;
const OTHER_KEEP: usize = 100;
/// A charge with fewer points of the phase of constant voltage than this
/// percent of the longest one in full/ is short.
const CHARGE_SHORT_PCT: usize = 80;

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

/// A charge seen from before its phase of constant voltage: then not all
/// the charge came in that phase.
fn charge_whole(env: &EnvFile) -> bool {
    let first = env.get_list("CV_Q_AFTER_UAH").and_then(|q| q.first().copied());
    matches!((first, env.get_i64("Q_IN_UAH")), (Some(f), Some(q)) if f < q)
}

/// The order in which the charges leave full/: first those that started
/// in the phase of constant voltage, then those with fewer points of it
/// than CHARGE_SHORT_PCT of the longest, each the fewest points first;
/// then the least seq. A file that cannot be read counts as started in
/// that phase, with no point.
fn charge_leave_order(runs: &[(u32, Option<EnvFile>)]) -> Vec<u32> {
    let points = |e: &Option<EnvFile>| {
        e.as_ref().and_then(|e| e.get_list("CV_I_UA")).map_or(0, |l| l.len())
    };
    let longest = runs.iter().map(|(_, e)| points(e)).max().unwrap_or(0);
    let mut order: Vec<(u8, usize, u32)> = runs
        .iter()
        .map(|(seq, e)| {
            let n = points(e);
            let group = if !e.as_ref().is_some_and(charge_whole) {
                0
            } else if n * 100 < longest * CHARGE_SHORT_PCT {
                1
            } else {
                return (2, 0, *seq);
            };
            (group, n, *seq)
        })
        .collect();
    order.sort_unstable();
    order.into_iter().map(|(_, _, seq)| seq).collect()
}

/// The seqs of full/ in the order they leave: for a charge by
/// `charge_leave_order`, else the least seq first.
fn leave_order(kind_dir: &Path, full: &Path) -> Vec<u32> {
    let s = seqs(full);
    if kind_dir.file_name().is_none_or(|n| n != CHARGE) {
        return s;
    }
    let runs: Vec<(u32, Option<EnvFile>)> = s
        .into_iter()
        .map(|seq| {
            let text = fs::read_to_string(file(full, seq)).ok();
            (seq, text.and_then(|t| EnvFile::parse(&t).ok()))
        })
        .collect();
    charge_leave_order(&runs)
}

/// Past 5 in full/ the first of `leave_order` go to archive/.
fn rotate_full(kind_dir: &Path) -> Result<()> {
    let full = kind_dir.join("full");
    let archive = kind_dir.join("archive");
    let s = leave_order(kind_dir, &full);
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

#[cfg(test)]
mod tests {
    use super::*;

    fn charge(q_in: i64, q_after: &[i64]) -> Option<EnvFile> {
        let mut e = EnvFile::default();
        e.set("Q_IN_UAH", q_in);
        e.set("CV_Q_AFTER_UAH", crate::envfile::join(q_after));
        e.set("CV_I_UA", crate::envfile::join(&vec![1; q_after.len()]));
        Some(e)
    }

    #[test]
    fn short_charges_leave_first() {
        let runs = vec![
            (1, charge(3000, &[900; 100])),
            (2, charge(500, &[500; 30])),
            (3, charge(3000, &[900; 95])),
            (4, charge(3000, &[400; 50])),
            (5, None),
            (6, charge(3000, &[900; 90])),
            (7, charge(400, &[400; 10])),
        ];
        assert_eq!(charge_leave_order(&runs), vec![5, 7, 2, 4, 1, 3, 6]);
    }
}
