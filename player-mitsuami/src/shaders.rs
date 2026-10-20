//! The machine's shader profile from the player's View > Shader menu
//! (track M22 step 5): the settings window's Shader picker as a menu, the
//! same rows (`launcher_core::shader_library::menu_profiles`), the
//! machine's checked. Only a swap (user, 2026-10-19): profiles are made
//! and edited in the launcher. A choice is saved to the bundle, as the
//! picker saves it, and the picture changes at once. The rows are read
//! again every two seconds, since the launcher adds, renames and deletes
//! profiles and moves the default.

use launcher_core::shader_library::{self, MenuProfile};
use mitsuami::prelude::*;
use std::path::PathBuf;
use std::time::Duration;

#[derive(Clone, Copy)]
pub struct Shaders {
    bundle: Signal<PathBuf>,
    rows: Signal<Vec<MenuProfile>>,
}

fn rows(bundle: &std::path::Path) -> Vec<MenuProfile> {
    shader_library::menu_profiles(bundle, &shader_library::default_dir())
}

impl Shaders {
    /// The rows for `bundle`, followed for as long as the window is up.
    pub fn open(bundle: PathBuf) -> Shaders {
        let shaders = Shaders { rows: signal(rows(&bundle)), bundle: signal(bundle) };
        spawn_local(shaders.poll());
        shaders
    }

    /// The menu changes only when what it lists does.
    async fn poll(self) {
        loop {
            sleep(Duration::from_secs(2)).await;
            self.refresh();
        }
    }

    fn refresh(self) {
        let fresh = self.bundle.with_untracked(|b| rows(b));
        if self.rows.with_untracked(|r| *r != fresh) {
            self.rows.set(fresh);
        }
    }

    /// The machine plays with `id` from now on (`None`: the app default),
    /// and the picture swaps to it.
    fn choose(self, id: Option<String>) {
        match self.bundle.with_untracked(|b| shader_library::choose_for(b, id)) {
            Ok(profile) => crate::set_shader(profile.map(|p| (p.preset, p.params.into_iter().collect()))),
            Err(e) => crate::report("The shader was not changed", e),
        }
        self.refresh();
    }

    pub fn menu(self) -> Menu {
        Menu::new("Shader").children_with(move || {
            self.rows
                .get()
                .into_iter()
                .map(|row| {
                    let (id, chosen) = (row.id, row.chosen);
                    MenuItem::new(row.label).checked(chosen).on_select(move || {
                        if !chosen {
                            self.choose(id.clone());
                        }
                    })
                })
                .collect::<Vec<_>>()
        })
    }
}
