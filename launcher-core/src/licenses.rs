//! The Licences window's contents: 2ksbox's own licence, the third-party
//! notices, and every licence text the package carries, one entry each.
//! Here and not in a front end (ADR-014). `launcherx --licenses` lists
//! the entries and the files behind each.
//!
//! The texts are the package's `licenses/` directory, which
//! `tools/third-party-notices.py stage` fills beside COPYING and
//! THIRD-PARTY-NOTICES.md: the generic texts as `<SPDX id>.txt`, a folder
//! per component, and `crates/<name>-<version>/` per Rust crate. A
//! checkout has no such directory, so it shows only the first two.

use std::path::{Path, PathBuf};

pub const TITLE: &str = "Licences";
/// The About window's button that opens it.
pub const OPEN: &str = "Licences";

/// What an entry is, for the line under its name.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    /// 2ksbox's own licence and the third-party notices.
    Own,
    /// A licence's text, named by its SPDX id, that the notices point at.
    Text,
    /// A project the app ships or links.
    Component,
    Crate,
}

impl Kind {
    pub fn label(self) -> &'static str {
        match self {
            Kind::Own => "This app",
            Kind::Text => "Licence text",
            Kind::Component => "Component",
            Kind::Crate => "Rust crate",
        }
    }
}

#[derive(Clone, PartialEq, Debug)]
pub struct Entry {
    pub name: String,
    pub kind: Kind,
    /// The file, or the folder whose files are shown one after another.
    pub path: PathBuf,
}

/// Where COPYING and THIRD-PARTY-NOTICES.md are: the package's doc
/// directory (`doc\` in a Windows package, `share/doc/2ksbox` in the
/// others; doc 07's layout), or the checkout's root.
fn doc_dir() -> PathBuf {
    match crate::paths::install_prefix() {
        Some(prefix) if cfg!(windows) => prefix.join("doc"),
        Some(prefix) => prefix.join("share/doc/2ksbox"),
        None => crate::paths::checkout(""),
    }
}

/// Every entry, in the order the window lists them: 2ksbox's own, then
/// the licence texts, the components and the crates, each by name.
pub fn entries() -> Vec<Entry> {
    let doc = doc_dir();
    let mut out = vec![
        Entry {
            name: "2ksbox".into(),
            kind: Kind::Own,
            path: doc.join("COPYING"),
        },
        Entry {
            name: "Third-party notices".into(),
            kind: Kind::Own,
            path: doc.join("THIRD-PARTY-NOTICES.md"),
        },
    ];
    let dir = doc.join("licenses");
    let (mut texts, mut components) = (Vec::new(), Vec::new());
    for (name, path) in sorted_dir(&dir) {
        if path.is_dir() {
            if name != "crates" {
                components.push(Entry {
                    name: display_name(name),
                    kind: Kind::Component,
                    path,
                });
            }
        } else if let Some(id) = name.strip_suffix(".txt") {
            texts.push(Entry {
                name: id.into(),
                kind: Kind::Text,
                path,
            });
        }
    }
    let crates = sorted_dir(&dir.join("crates"))
        .into_iter()
        .map(|(name, path)| Entry {
            // `name-1.2.3`: the version is what follows the last dash.
            name: match name.rsplit_once('-') {
                Some((crate_name, version)) => format!("{crate_name} {version}"),
                None => name,
            },
            kind: Kind::Crate,
            path,
        });
    out.extend(texts);
    out.extend(components);
    out.extend(crates);
    out
}

/// A component's folder by its project's own name: About's (`qemu` is
/// QEMU), these for the ones About credits through another project, else
/// as it is.
fn display_name(folder: String) -> String {
    let own = match folder.as_str() {
        "edk2" => Some("EDK II"),
        "openssl" => Some("OpenSSL"),
        "pcre2" => Some("PCRE2"),
        _ => None,
    };
    if let Some(name) = own {
        return name.to_owned();
    }
    crate::about::GROUPS
        .iter()
        .flat_map(|g| g.credits)
        .find(|c| c.name.eq_ignore_ascii_case(&folder))
        .map_or(folder, |c| c.name.to_owned())
}

/// A directory's entries as (name, path), by name without regard to case.
fn sorted_dir(dir: &Path) -> Vec<(String, PathBuf)> {
    let Ok(read) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut all: Vec<_> = read
        .flatten()
        .map(|e| (e.file_name().to_string_lossy().into_owned(), e.path()))
        .collect();
    all.sort_by_key(|(name, _)| name.to_lowercase());
    all
}

/// The entry's text: the file, or each of the folder's files under its
/// name. A missing file says so, rather than leaving the pane empty.
pub fn text(entry: &Entry) -> String {
    if !entry.path.is_dir() {
        return read(&entry.path);
    }
    sorted_dir(&entry.path)
        .into_iter()
        .filter(|(_, path)| path.is_file())
        .map(|(name, path)| format!("{name}\n\n{}", read(&path)))
        .collect::<Vec<_>>()
        .join("\n\n")
}

fn read(path: &Path) -> String {
    match std::fs::read(path) {
        Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
        Err(_) => format!("{} is missing from this build.", path.display()),
    }
}

/// The entries as plain text, for `launcherx --licenses`.
pub fn list_text() -> String {
    use std::fmt::Write;
    let mut s = String::new();
    for e in entries() {
        let _ = writeln!(
            s,
            "{:<36} {:<12} {}",
            e.name,
            e.kind.label(),
            e.path.display()
        );
    }
    s
}
