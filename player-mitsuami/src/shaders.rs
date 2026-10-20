//! The machine's shader profile from the player's View > Shader menu
//! (track M22 step 5): the settings window's Shader picker as a menu, the
//! same rows (`launcher_core::shader_library::menu_profiles`: the app
//! default, Off, then the profiles), the machine's checked. Only a swap
//! (user, 2026-10-19): profiles are made and edited in the launcher. A
//! choice is saved to the bundle, as the picker saves it, and the picture
//! changes at once. The rows follow the launcher adding, renaming and
//! deleting profiles, moving the default and editing the machine, as the
//! files change (`watch.rs`), never on a timer.

use crate::wake::Wake;
use crate::watch;
use launcher_core::shader_library::{self, MenuProfile, ShaderChoice};
use mitsuami::prelude::*;
use std::path::PathBuf;
use std::sync::Arc;

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
        let shaders = Shaders { rows: signal(rows(&bundle)), bundle: signal(bundle.clone()) };
        let changed = Arc::new(Wake::default());
        let watch = watch::watch(&[bundle], &[shader_library::default_dir()], &changed);
        spawn_local(async move {
            let _watch = watch;
            loop {
                changed.next().await;
                shaders.refresh();
            }
        });
        shaders
    }

    /// The menu changes only when what it lists does.
    fn refresh(self) {
        let fresh = self.bundle.with_untracked(|b| rows(b));
        if self.rows.with_untracked(|r| *r != fresh) {
            self.rows.set(fresh);
        }
    }

    /// The machine plays with `choice` from now on, and the picture swaps
    /// to it.
    fn choose(self, choice: ShaderChoice) {
        match self.bundle.with_untracked(|b| shader_library::choose_for(b, choice)) {
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
                    let (choice, chosen) = (row.choice, row.chosen);
                    MenuItem::new(row.label).checked(chosen).on_select(move || {
                        if !chosen {
                            self.choose(choice.clone());
                        }
                    })
                })
                .collect::<Vec<_>>()
        })
    }
}
