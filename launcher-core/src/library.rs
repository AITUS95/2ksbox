//! The machine library (doc 07): a directory of bundle subdirectories,
//! each holding one `machine.toml`. "A plain, documented directory layout
//! the user can back up" (doc 07's settings taxonomy). No database, no
//! hidden index; the grid is a scan.

use crate::bundle::{Family, Machine};
use std::path::{Path, PathBuf};

pub const BUNDLE_FILE: &str = "machine.toml";

/// Help's item that opens `default_dir()` in the file manager.
pub const OPEN_FOLDER: &str = "Open Machines Folder";

/// Help's item that picks the folder again, in the sandbox only
/// (`asks_for_dir`).
pub const CHOOSE_FOLDER: &str = "Choose Machines Folder…";

/// The library directory: the folder the user chose (`chosen_dir`), else
/// the platform data dir (`~/.local/share` on Linux, `~/Library/Application
/// Support` on macOS, `%APPDATA%` on Windows) plus `machines`.
/// `LAUNCHER_LIBRARY_DIR` overrides both for development and tests, like
/// the player's `PLAYER_*` knobs.
pub fn default_dir() -> PathBuf {
    if let Ok(dir) = std::env::var("LAUNCHER_LIBRARY_DIR") {
        return dir.into();
    }
    if let Some(dir) = chosen_dir() {
        return dir;
    }
    crate::paths::data_dir().map(|d| d.join("machines")).unwrap_or_else(|| PathBuf::from("machines"))
}

/// The machines folder the user chose (`choose_dir`), if it is still there.
///
/// Only the App Store build asks (`asks_for_dir`). Its data dir is inside
/// the app's sandbox container, and macOS's open panel never shows a
/// folder in there: a machine kept in the container could not have its
/// disk browsed to, and "Browse…" opened somewhere else (2026-10-18). So
/// that build keeps its machines in a folder the user picks, which the
/// panel shows like any other, and keeps access to it with a
/// security-scoped bookmark (`grants`). Nothing in the container is moved
/// (user decision).
pub fn chosen_dir() -> Option<PathBuf> {
    let text = std::fs::read_to_string(chosen_file()?).ok()?;
    let dir = PathBuf::from(text.trim_end_matches(['\r', '\n']));
    (!dir.as_os_str().is_empty() && dir.is_dir()).then_some(dir)
}

/// `<data dir>/library.txt`: the one line `choose_dir` writes.
fn chosen_file() -> Option<PathBuf> {
    crate::paths::data_dir().map(|d| d.join("library.txt"))
}

/// Whether this launcher asks where its machines go: the macOS App
/// Sandbox, unless `LAUNCHER_LIBRARY_DIR` names the library.
pub fn asks_for_dir() -> bool {
    crate::grants::sandboxed() && std::env::var_os("LAUNCHER_LIBRARY_DIR").is_none()
}

/// Whether to ask now, at start: in the sandbox, until a folder is chosen
/// (and again if it went away). "Later" leaves the container's library in
/// use for this run.
pub fn needs_dir() -> bool {
    asks_for_dir() && chosen_dir().is_none()
}

/// Keep `dir`, picked in a folder dialog, as the library from now on,
/// with its access for later runs.
pub fn choose_dir(dir: &Path) -> std::io::Result<()> {
    let file = chosen_file().ok_or_else(|| std::io::Error::other("no data directory"))?;
    if let Some(parent) = file.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&file, format!("{}\n", dir.display()))?;
    crate::grants::keep(dir);
    Ok(())
}

/// Where the folder dialog opens: the user's real home folder. In the
/// sandbox `$HOME` is the container, which the dialog does not show.
pub fn choose_start() -> Option<PathBuf> {
    #[cfg(unix)]
    {
        // SAFETY: getpwuid's record is read at once, on this thread, and
        // pw_dir checked for null.
        let home = unsafe {
            let pw = libc::getpwuid(libc::getuid());
            if pw.is_null() || (*pw).pw_dir.is_null() {
                return None;
            }
            std::ffi::CStr::from_ptr((*pw).pw_dir).to_string_lossy().into_owned()
        };
        Some(PathBuf::from(home))
    }
    #[cfg(not(unix))]
    {
        directories::BaseDirs::new().map(|d| d.home_dir().to_path_buf())
    }
}

/// The question at start (`needs_dir`), and the folder dialog's prompt.
/// The words are here, not in a front end, like `firstrun`'s.
pub struct Question {
    pub headline: &'static str,
    pub detail: &'static str,
    /// The buttons: pick a folder now, or keep the app's own storage for
    /// this run.
    pub choose: &'static str,
    pub later: &'static str,
    /// The folder dialog's message line.
    pub prompt: &'static str,
    /// The alert's headline when the pick cannot be kept.
    pub failed: &'static str,
}

pub const QUESTION: Question = Question {
    headline: "Choose a folder for your machines",
    detail: "2ksbox keeps each machine's settings and disk in a folder of yours, where you can find, \
             back up and browse to them. Choose one, or make a new one, for example \"2ksbox\" in your \
             home folder.\n\nUntil you do, new machines are kept inside the app's own storage, which \
             file dialogs can't open.",
    choose: "Choose Folder…",
    later: "Later",
    prompt: "Choose or make the folder for your 2ksbox machines",
    failed: "Couldn't use that folder for your machines.",
};

pub struct LibraryEntry {
    pub dir: PathBuf,
    pub machine: Machine,
}

/// A directory name from a machine name: lowercase, non-alphanumerics
/// collapsed to `-`, deduplicated against what's already in `dir`.
fn slug(dir: &Path, name: &str) -> String {
    let mut base: String = name
        .to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '-' })
        .collect();
    while base.contains("--") {
        base = base.replace("--", "-");
    }
    let base = base.trim_matches('-');
    let base = if base.is_empty() { "machine" } else { base };
    let mut candidate = base.to_string();
    let mut n = 2;
    while dir.join(&candidate).exists() {
        candidate = format!("{base}-{n}");
        n += 1;
    }
    candidate
}

/// Reserve a fresh bundle subdirectory for `name` under `dir` (creating
/// both) without writing a `machine.toml` yet. The wizard needs the
/// directory first, to create a disk image inside it before the bundle
/// that references that disk can be written.
pub fn reserve_dir(dir: &Path, name: &str) -> std::io::Result<PathBuf> {
    std::fs::create_dir_all(dir)?;
    let machine_dir = dir.join(slug(dir, name));
    std::fs::create_dir_all(&machine_dir)?;
    Ok(machine_dir)
}

/// Create a new bundle under `dir` from doc 06's reference defaults,
/// returning the path to its `machine.toml`.
pub fn create(dir: &Path, family: Family, name: String, disk: PathBuf) -> std::io::Result<PathBuf> {
    let machine_dir = reserve_dir(dir, &name)?;
    let bundle_path = machine_dir.join(BUNDLE_FILE);
    Machine::reference(family, name, disk).save(&bundle_path)?;
    Ok(bundle_path)
}

/// Every bundle directly under `dir` (one level, not recursive). A
/// subdirectory without a readable `machine.toml` is skipped, so one
/// corrupt bundle doesn't take down the whole grid.
pub fn scan(dir: &Path) -> Vec<LibraryEntry> {
    let Ok(read) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut entries: Vec<LibraryEntry> = read
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false))
        .filter_map(|e| {
            let dir = e.path();
            match Machine::load(&dir.join(BUNDLE_FILE)) {
                Ok(machine) => Some(LibraryEntry { dir, machine }),
                Err(err) => {
                    eprintln!("[library] skipping {}: {err}", dir.display());
                    None
                }
            }
        })
        .collect();
    // By name, ignoring case ("doom" beside "Diablo", not after "Zork");
    // names equal but for case keep a fixed order.
    entries.sort_by(|a, b| {
        let (a, b) = (&a.machine.name, &b.machine.name);
        a.to_lowercase().cmp(&b.to_lowercase()).then_with(|| a.cmp(b))
    });
    entries
}
