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

/// Windows 11's (24H2, the Windows default scheme), by
/// `CursorImage::trimmed_hash`, so a shape matches however an adapter pads
/// it (both of Windows 11's hand over 64x64). Measured at Windows' 100 %
/// scaling and the default pointer size, on our adapter (2026-10-20); any
/// other scaling or size draws other shapes, which stay the guest's. The
/// busy cursors are animated, one shape a frame: 18 each. IDC_UPARROW has
/// no host cursor and stays the guest's.
const KNOWN_TRIMMED: &[(u64, CursorKind)] = &[
    (0x0fc3_a101_485f_a188, CursorKind::Arrow),
    (0xeac6_3e5d_c85b_b394, CursorKind::Text),
    (0x1d9f_3d95_0548_fc6e, CursorKind::Wait),
    (0x4978_3e5e_3c0b_6eac, CursorKind::Wait),
    (0x239d_3b77_6676_3282, CursorKind::Wait),
    (0x25db_8ac7_9b2b_5e02, CursorKind::Wait),
    (0x4899_4e0a_0663_cd1f, CursorKind::Wait),
    (0x2459_dcd3_6984_d62a, CursorKind::Wait),
    (0x8b9b_d09d_fcca_8355, CursorKind::Wait),
    (0x5aac_a512_0250_5117, CursorKind::Wait),
    (0x2b20_7953_90ed_e750, CursorKind::Wait),
    (0xcac6_775d_4cf1_c5a6, CursorKind::Wait),
    (0x89a4_f69d_d608_270d, CursorKind::Wait),
    (0x2e60_9291_ca01_fbae, CursorKind::Wait),
    (0x5a05_7918_2b6a_da86, CursorKind::Wait),
    (0x97e2_7dcf_a932_283a, CursorKind::Wait),
    (0x607a_6228_fa25_18ad, CursorKind::Wait),
    (0xdfcc_438f_e012_6611, CursorKind::Wait),
    (0x5775_908f_f38d_6bca, CursorKind::Wait),
    (0x000a_b1b2_9f76_0111, CursorKind::Wait),
    (0xfe5c_1d7b_c68d_bde5, CursorKind::Crosshair),
    (0xe2c2_1db5_a5b7_f69b, CursorKind::ResizeNorthWestSouthEast),
    (0xdae5_d3a5_4260_c8db, CursorKind::ResizeNorthEastSouthWest),
    (0x8ede_1b56_65f8_e49e, CursorKind::ResizeEastWest),
    (0xb194_27d1_798e_0aee, CursorKind::ResizeNorthSouth),
    (0x0454_15d1_1b78_57b2, CursorKind::Move),
    (0x5844_ca23_ab52_bb2d, CursorKind::NotAllowed),
    (0xbb55_f28c_21c4_4817, CursorKind::Pointer),
    (0x0386_1b24_01de_de88, CursorKind::Progress),
    (0x7a18_ec53_ceb0_0739, CursorKind::Progress),
    (0x5f82_5d19_df99_3fd6, CursorKind::Progress),
    (0xf919_b5c1_ed72_8e4a, CursorKind::Progress),
    (0xda31_d9e3_428e_86d0, CursorKind::Progress),
    (0xb2a1_466b_ca74_12b0, CursorKind::Progress),
    (0x62cf_4419_e4c5_18c7, CursorKind::Progress),
    (0x8af7_76d7_dd5e_3242, CursorKind::Progress),
    (0x7ec9_54c8_36cd_1e29, CursorKind::Progress),
    (0x7057_801b_3b6b_bfb7, CursorKind::Progress),
    (0x30c4_2160_7b8c_1efc, CursorKind::Progress),
    (0x8786_7884_649c_0af2, CursorKind::Progress),
    (0xe2c8_fea2_d5c6_0f64, CursorKind::Progress),
    (0x2cf0_c98c_d287_d5d4, CursorKind::Progress),
    (0x3475_fdb3_d1fb_a080, CursorKind::Progress),
    (0x1bc6_af2f_a143_b6b3, CursorKind::Progress),
    (0x59dd_df21_26be_4f36, CursorKind::Progress),
    (0x8335_89cd_668f_76d8, CursorKind::Progress),
    (0x8d9e_7e28_9013_2c39, CursorKind::Help),
];

/// The standard cursor this shape is, if it is one.
pub fn recognise(c: &CursorImage) -> Option<CursorKind> {
    let h = c.hash();
    if let Some((_, kind)) = KNOWN.iter().find(|(k, _)| *k == h) {
        return Some(*kind);
    }
    let t = c.trimmed_hash();
    KNOWN_TRIMMED.iter().find(|(k, _)| *k == t).map(|(_, kind)| *kind)
}
