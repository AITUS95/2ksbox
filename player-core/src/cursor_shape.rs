//! The guest's standard cursors, recognised by their pixels, so the host's
//! own cursor of the same meaning can stand in for them (user,
//! 2026-10-12: the guest's, scaled to the picture, looked worse).
//!
//! Windows draws its standard cursors (`IDC_*`) from fixed bitmaps, so
//! each one reaches the hardware cursor as the same pixels every time:
//! `CursorImage::hash` names it. The table is measured, not derived:
//! `guest-tools/src/cursors.c` (CURSORS.EXE) sets each `IDC_*` in turn and
//! `PLAYER_CURSOR_LOG=1` prints the hash of every shape the guest defines
//! (doc 15, "Player"). A shape not in it (a game's own) stays the guest's.

use crate::qemu_vm::CursorImage;

/// What a guest cursor means, where the host has one of its own for it.
/// The arrow is here too: over a scaled picture it becomes the host's.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CursorKind {
    Arrow,
    Text,
    Wait,
    /// The arrow with the hourglass (`IDC_APPSTARTING`).
    Progress,
    /// The pointing hand (`IDC_HAND`).
    Pointer,
    Crosshair,
    NotAllowed,
    Help,
    Move,
    ResizeNorthSouth,
    ResizeEastWest,
    ResizeNorthWestSouthEast,
    ResizeNorthEastSouthWest,
}

/// Each standard cursor's hash, per Windows family. Windows 98 (base98-br,
/// all 32x32; IDC_UPARROW has no host cursor and stays the guest's). XP
/// has none here: its driver takes no alpha cursor, so with the pointer
/// shadow on GDI draws XP's pointer itself and the standard ones never
/// reach the hardware cursor.
const KNOWN: &[(u64, CursorKind)] = &[
    // Windows 98
    (0x984d_a16f_a6dc_bd68, CursorKind::Arrow),
    (0xb597_2959_6b28_259a, CursorKind::Text),
    (0xb187_9b41_9d69_8715, CursorKind::Wait),
    (0x97f2_cfff_4e34_f633, CursorKind::Crosshair),
    (0x2e53_d702_cf67_8678, CursorKind::ResizeNorthWestSouthEast),
    (0x214b_e656_2e12_4f78, CursorKind::ResizeNorthEastSouthWest),
    (0x312a_97dc_d6f1_f878, CursorKind::ResizeEastWest),
    (0x4d5a_4f2b_d4bd_e2d2, CursorKind::ResizeNorthSouth),
    (0x7b54_855c_36c4_6b33, CursorKind::Move),
    (0x62b7_c0cf_5ba1_0c7d, CursorKind::NotAllowed),
    (0xc6a8_e15f_b157_8add, CursorKind::Pointer),
    (0x1cb5_254f_45e7_2939, CursorKind::Progress),
    (0x591f_f902_d165_6cff, CursorKind::Help),
];

/// The standard cursor this shape is, if it is one.
pub fn recognise(c: &CursorImage) -> Option<CursorKind> {
    let h = c.hash();
    KNOWN.iter().find(|(k, _)| *k == h).map(|(_, kind)| *kind)
}
