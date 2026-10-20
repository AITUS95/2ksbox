//! What the Disc and Shader menus follow (track M22): files the launcher
//! writes (the shelf, the machine's bundle, the shader profiles), watched
//! through the platform's own notifications (`notify`: inotify, FSEvents,
//! `ReadDirectoryChangesW`), so a menu changes when they do and nothing
//! reads them on a timer (user, 2026-10-19). The drive's moves come from
//! QEMU's own events instead (`main.rs`, `DEVICE_TRAY_MOVED`).
//!
//! A file is watched through its folder: a save that writes a new file
//! and renames it over the old one is the folder's event, and the file
//! watched directly would be the replaced one. The folders also hold
//! files that change all the time (the data folder has `player.log`), so
//! only events naming a watched file wake the menu.

use crate::wake::Wake;
use notify::{RecommendedWatcher, RecursiveMode, Watcher};
use std::collections::HashSet;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::sync::Arc;

/// Wakes `wake` while it lives.
pub struct Watch {
    _watcher: Option<RecommendedWatcher>,
}

/// Wake `wake` when one of `files` changes, or anything in one of `dirs`.
/// A path that can't be watched is said on stderr and left out: its menu
/// then changes only when the player itself changes it.
pub fn watch(files: &[PathBuf], dirs: &[PathBuf], wake: &Arc<Wake>) -> Watch {
    // what each watched folder's events must name, `None` for anything
    let mut folders: Vec<(PathBuf, Option<HashSet<OsString>>)> = Vec::new();
    for file in files {
        let (Some(dir), Some(name)) = (file.parent(), file.file_name()) else { continue };
        let dir = canonical(dir);
        match folders.iter_mut().find(|(d, _)| *d == dir) {
            Some((_, Some(names))) => _ = names.insert(name.to_owned()),
            Some((_, None)) => {}
            None => folders.push((dir, Some(HashSet::from([name.to_owned()])))),
        }
    }
    for dir in dirs {
        let dir = canonical(dir);
        match folders.iter_mut().find(|(d, _)| *d == dir) {
            Some(entry) => entry.1 = None,
            None => folders.push((dir, None)),
        }
    }
    let wanted = folders.clone();
    let notifier = wake.notifier();
    let watcher = notify::recommended_watcher(move |event: notify::Result<notify::Event>| {
        let Ok(event) = event else { return };
        if event.kind.is_access() {
            return;
        }
        let named = |path: &PathBuf| {
            wanted.iter().any(|(dir, names)| {
                path.parent() == Some(dir.as_path())
                    && names.as_ref().is_none_or(|n| path.file_name().is_some_and(|f| n.contains(f)))
            })
        };
        if event.paths.iter().any(named) {
            notifier();
        }
    });
    let mut watcher = match watcher {
        Ok(w) => w,
        Err(e) => {
            eprintln!("[watch] no file notifications: {e}");
            return Watch { _watcher: None };
        }
    };
    for (dir, _) in &folders {
        if let Err(e) = watcher.watch(dir, RecursiveMode::NonRecursive) {
            eprintln!("[watch] {}: {e}", dir.display());
        }
    }
    Watch { _watcher: Some(watcher) }
}

/// One spelling for a folder, watched as such so its events name it the
/// same way: the launcher hands the player `C:/…/library\machine\machine.toml`,
/// and macOS reports `/private/var/…` for `/var/…`. Once per folder, not
/// per event: the data folder's `player.log` changes all the time.
fn canonical(dir: &Path) -> PathBuf {
    dir.canonicalize().unwrap_or_else(|_| dir.to_path_buf())
}
