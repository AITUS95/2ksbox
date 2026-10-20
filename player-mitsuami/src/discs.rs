//! The machine's CD drive from the player's Disc menu (track M22 step 5):
//! the shared disc shelf, the disc in the drive checked, Eject, and a disc
//! image or folder picked here, which goes on the shelf and in the drive
//! at once. The model is `launcher_core::shelf`, the launcher's shelf
//! window's, opened for the bundle the launcher named (`--machine`): an
//! insert swaps the tray through the machine's monitor (the `-qmp` socket
//! the launcher started it with) and sets the disc it boots with. The
//! guest changes the drive too (`CDSHELF`) and the launcher the shelf, so
//! both are read again every two seconds. Everything that asks the monitor
//! runs on a thread of its own: QEMU answers from its main loop, which a
//! snapshot can hold for seconds, and the window must not wait for it.

use launcher_core::browse;
use launcher_core::disc_library::{self, DISC_FILTER};
use launcher_core::shelf::Shelf;
use mitsuami::prelude::*;
use std::path::{Path, PathBuf};
use std::time::Duration;

#[derive(Clone, Copy)]
pub struct Discs {
    bundle: Signal<PathBuf>,
    model: Signal<Shelf>,
}

/// The shelf opened for the machine, its drive asked of the monitor.
fn load(bundle: &Path) -> Shelf {
    let mut shelf = Shelf::default();
    shelf.open_for_path(bundle.to_path_buf(), &disc_library::default_path());
    shelf
}

impl Discs {
    /// The shelf for `bundle`, followed for as long as the window is up.
    pub fn open(bundle: PathBuf) -> Discs {
        let discs = Discs { model: signal(Shelf::default()), bundle: signal(bundle) };
        spawn_local(discs.poll());
        discs
    }

    /// The menu changes only when what it lists does.
    async fn poll(self) {
        loop {
            let bundle = self.bundle.get_untracked();
            let fresh = spawn_blocking(move || load(&bundle)).await;
            if fresh.menu_discs() != self.model.with_untracked(Shelf::menu_discs) {
                self.model.set(fresh);
            }
            sleep(Duration::from_secs(2)).await;
        }
    }

    /// Insert or eject, on the shelf as it is now, written at once; the
    /// guest's `CDSHELF` lists a disc added here as soon as the shelf is
    /// written.
    fn change(self, f: impl FnOnce(&mut Shelf) + Send + 'static) {
        let bundle = self.bundle.get_untracked();
        spawn_local(async move {
            let shelf = spawn_blocking(move || {
                let mut s = load(&bundle);
                f(&mut s);
                s.flush_reporting();
                if s.take_saved() {
                    if let Some(dir) = s.bundle_dir() {
                        s.publish_to(dir);
                    }
                }
                s
            })
            .await;
            let error = shelf.error().map(str::to_owned);
            self.model.set(shelf);
            if let Some(error) = error {
                crate::report("The disc was not changed", error);
            }
        });
    }

    /// A disc image or a folder, picked in the platform's dialog, on the
    /// shelf and in the drive as soon as the dialog closes.
    fn pick(self, folder: bool) {
        let mut request = if folder {
            OpenFile::new().title("Insert a folder as a disc").directories()
        } else {
            OpenFile::new()
                .title("Insert a disc image")
                .filter(FileFilter::new(DISC_FILTER.0, browse::extensions(DISC_FILTER)))
                .filter(FileFilter::all("All files"))
        };
        if let Some(start) = browse::browse_start("", None) {
            request = request.start_folder(start);
        }
        spawn_local(async move {
            let Some(path) = open_file(request).await.and_then(|p| p.into_iter().next()) else { return };
            let path = browse::picked(&path);
            browse::remember(&path);
            self.change(move |s| s.add_and_insert(path));
        });
    }

    pub fn menu(self) -> Menu {
        Menu::new("Disc")
            .children_with(move || {
                let rows = self.model.with(Shelf::menu_discs);
                if rows.is_empty() {
                    return vec![MenuItem::new("No discs yet.").enabled(false)];
                }
                rows.into_iter()
                    .map(|disc| {
                        let (path, in_drive) = (disc.path, disc.in_drive);
                        MenuItem::new(disc.label).checked(in_drive).on_select(move || {
                            if !in_drive {
                                let path = path.clone();
                                self.change(move |s| s.insert(&path));
                            }
                        })
                    })
                    .collect()
            })
            .separator()
            .item(
                MenuItem::new("Eject")
                    .enabled(move || self.model.with(|s| s.drive().is_some()))
                    .on_select(move || self.change(Shelf::eject)),
            )
            .separator()
            .item(MenuItem::new("Insert Disc Image…").on_select(move || self.pick(false)))
            .item(MenuItem::new("Insert Folder as Disc…").on_select(move || self.pick(true)))
    }
}
