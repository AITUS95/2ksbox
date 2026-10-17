# Releasing 2ksbox

A release is a version in the root `Cargo.toml`, store packages built
from one commit of `main`, and a `v<version>` tag on that commit. The
project publishes binaries only through the app stores (ADR-028): the
tag and the source are what GitHub gets. The store listings, the
submission steps and the store business notes are in the private store
repository, not here.

## The version

The root `Cargo.toml`'s `[workspace.package] version` is the app's
version. These carry it too, and `scripts/release.sh check` fails until
they agree:

| Where | What |
|---|---|
| `launcher-mitsuami/Cargo.toml`, `player-mitsuami/Cargo.toml` | their own workspaces, so their own `version` |
| `packaging/windows/app.manifest` | `assemblyIdentity version="A.B.C.0"` |
| `packaging/linux/com._2ksbox.Launcher.metainfo.xml` | the first `<release>`, newest first, with the release's date |
| `manual/src/content/docs/release-notes.mdx` | a `## A.B.C` section |

The macOS `Info.plist` and the MSIX take the version from `Cargo.toml`
when they are packed. The Store refuses a first number of 0, and App
Store Connect a `CFBundleVersion` it has seen before, so every upload
needs a higher version than the last.

## Steps

1. **Land everything on `main`** and push it. Bump the version in the
   files above, write the release notes (short and plain; the stores'
   *What's New* is taken from them), and set the metainfo release's
   date to the day you tag.
2. **Run the gate on Linux** from that commit: `scripts/build.sh`,
   `scripts/test.sh all`, `scripts/package-linux.sh` and
   `scripts/package-flatpak.sh`. Compare failures against the known
   ones in `docs/00-status.md` "Open threads" before calling one new.
   After any dependency change, `scripts/gen-flatpak-cargo-sources.sh`
   first.
3. **Run the gate on the PC**: `scripts/build-windows.sh`,
   `scripts/test.sh all` in the MINGW64 shell, then the Store MSIX
   (`build-windows.md` "The Store package") with Partner Center's
   identity, through the certification kit.
4. **Run the gate on the Air**: `scripts/build.sh`, `scripts/test.sh
   all`, then the App Store package (`build-macos.md` "The App Store
   package") and TestFlight on it.
5. **Tag**: `scripts/release.sh check`, then `scripts/release.sh tag`.
   Every package uploaded must come from the tagged commit; if a fix
   lands after the tag, it is the next version.
6. **Upload** both packages from the store repository's submission
   steps.
7. **When the listings are live**, put the store links in the manual's
   `install.mdx` (it says neither listing is up yet) and in `README.md`,
   and update `docs/00-status.md`.
