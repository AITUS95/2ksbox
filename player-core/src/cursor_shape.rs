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
/// it (both of Windows 11's hand over 64x64). Measured on our adapter at
/// the default pointer size and each of the display scalings 100 % to
/// 200 % (2026-10-20): Windows draws three sizes, 32x32 at 100 % and
/// 125 %, 48x48 at 150 % and 175 %, 64x64 at 200 %. Another scaling or
/// pointer size draws other shapes, which stay the guest's. The busy
/// cursors are animated, one shape a frame: 18 each (17 for the wait at
/// 200 %, two of its frames alike). IDC_UPARROW has no host cursor and
/// stays the guest's.
const KNOWN_TRIMMED: &[(u64, CursorKind)] = &[
    // 100 % and 125 % (32x32)
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
    // 150 % and 175 % (48x48)
    (0x2f21_46b9_926c_e0dd, CursorKind::Arrow),
    (0x6637_1692_c32d_1085, CursorKind::Text),
    (0x9e01_2c8f_5a6b_da27, CursorKind::Crosshair),
    (0x91df_3566_1963_d620, CursorKind::ResizeNorthWestSouthEast),
    (0x0e2e_0063_26ad_6e84, CursorKind::ResizeNorthEastSouthWest),
    (0xe1eb_bf66_9295_ce5d, CursorKind::ResizeEastWest),
    (0x51d6_0188_9cc2_49cf, CursorKind::ResizeNorthSouth),
    (0x4370_a037_dfd3_3f05, CursorKind::Move),
    (0xeac3_45d0_7ef0_5d90, CursorKind::NotAllowed),
    (0x3ee2_12a5_a09f_19c3, CursorKind::Pointer),
    (0x61c6_9e4e_0675_37ec, CursorKind::Wait),
    (0x945c_3829_a031_0bc1, CursorKind::Wait),
    (0x4e26_1a17_19c3_dfca, CursorKind::Wait),
    (0x9c25_e98b_0960_6cb7, CursorKind::Wait),
    (0x994c_1f7e_f625_b30e, CursorKind::Wait),
    (0x8b4f_674b_045c_85eb, CursorKind::Wait),
    (0xad50_bb82_0e81_7e44, CursorKind::Wait),
    (0x7c9d_1de4_de35_1be4, CursorKind::Wait),
    (0x7280_223c_7f82_6627, CursorKind::Wait),
    (0x91f9_8287_9ded_8c80, CursorKind::Wait),
    (0x13a5_3a97_14eb_89a1, CursorKind::Wait),
    (0x8268_c3bc_1354_8ff0, CursorKind::Wait),
    (0x9255_2e27_7171_ee37, CursorKind::Wait),
    (0x2352_b877_d8f8_15a2, CursorKind::Wait),
    (0x7091_2747_e0bf_4081, CursorKind::Wait),
    (0xb1ea_cc8e_1776_05c3, CursorKind::Wait),
    (0x0057_a563_b316_5926, CursorKind::Wait),
    (0xdaab_0300_4bc3_9e83, CursorKind::Wait),
    (0x2629_fa47_364d_4b2e, CursorKind::Progress),
    (0x7cbc_00e5_442f_438a, CursorKind::Progress),
    (0x3057_eef7_9d33_fcd9, CursorKind::Progress),
    (0x934e_3446_ae9b_1112, CursorKind::Progress),
    (0xa422_fa95_ab0f_7f88, CursorKind::Progress),
    (0xc0ff_2030_c0fb_21f9, CursorKind::Progress),
    (0xae77_634e_aab1_0d5c, CursorKind::Progress),
    (0x3a59_79d3_be9b_dbc2, CursorKind::Progress),
    (0x018f_ecf4_428c_e983, CursorKind::Progress),
    (0xc0fe_2c0b_f78c_c30e, CursorKind::Progress),
    (0x59c2_10a3_348e_0e1d, CursorKind::Progress),
    (0xb371_9eb2_9a01_e671, CursorKind::Progress),
    (0x64b5_9882_2348_153d, CursorKind::Progress),
    (0xa1bf_7830_4cf6_96aa, CursorKind::Progress),
    (0x39b3_ad6e_e40a_4e6a, CursorKind::Progress),
    (0xc7e9_1865_a49f_9f02, CursorKind::Progress),
    (0x42f5_7cb2_43cf_1b9b, CursorKind::Progress),
    (0x307c_4615_aced_b80e, CursorKind::Progress),
    (0x3a43_d400_fb76_0682, CursorKind::Help),
    // 200 % (64x64)
    (0x21d4_cfd5_a1f5_73b8, CursorKind::Arrow),
    (0x5afa_ee65_4605_56e5, CursorKind::Text),
    (0xe451_55f3_c4e4_abd7, CursorKind::Crosshair),
    (0x77e2_d899_d539_9a2d, CursorKind::ResizeNorthWestSouthEast),
    (0x190b_722e_0e2f_6cad, CursorKind::ResizeNorthEastSouthWest),
    (0xa870_6fb6_b06f_e54a, CursorKind::ResizeEastWest),
    (0x809c_5e70_ede5_6b45, CursorKind::ResizeNorthSouth),
    (0x057a_cd20_c049_4f6d, CursorKind::Move),
    (0x0abb_3f7a_1f2c_d70a, CursorKind::NotAllowed),
    (0xc99e_b733_163f_a7da, CursorKind::Pointer),
    (0xf882_cf46_2cff_f4c5, CursorKind::Wait),
    (0xe2ea_939a_2c0a_f18f, CursorKind::Wait),
    (0xe4bc_64d2_31f6_8b70, CursorKind::Wait),
    (0xcde0_3691_11a7_cf82, CursorKind::Wait),
    (0x33f6_36fb_cbbf_29bf, CursorKind::Wait),
    (0xdd58_dd3d_5696_0736, CursorKind::Wait),
    (0xd19f_2213_4e95_6fc2, CursorKind::Wait),
    (0x4cb2_53b4_e03c_f66d, CursorKind::Wait),
    (0xee8c_43a3_f2b3_d029, CursorKind::Wait),
    (0x27d1_b473_e01b_e95b, CursorKind::Wait),
    (0x3836_7a67_c5a0_ffef, CursorKind::Wait),
    (0x6e25_ba1f_fb04_6a2b, CursorKind::Wait),
    (0xd6ae_cad3_5a7f_0c5c, CursorKind::Wait),
    (0x3d95_10ce_86de_7129, CursorKind::Wait),
    (0x6ea5_097e_4c40_6460, CursorKind::Wait),
    (0xa203_67cc_88e2_dc34, CursorKind::Wait),
    (0x349f_e1e9_78af_51e4, CursorKind::Wait),
    (0xe694_0465_5196_e92b, CursorKind::Progress),
    (0x2312_d5ae_d677_6f04, CursorKind::Progress),
    (0x7a10_f5aa_d1de_dcad, CursorKind::Progress),
    (0x6469_dc3e_72c3_8f45, CursorKind::Progress),
    (0xd575_3d5e_5ff4_2ddf, CursorKind::Progress),
    (0x391d_af75_1352_8d38, CursorKind::Progress),
    (0xad9d_d24b_12dd_5f98, CursorKind::Progress),
    (0x758b_6fbc_7d48_0fa9, CursorKind::Progress),
    (0xea6d_4aa3_ba41_f1b8, CursorKind::Progress),
    (0x6b7f_4f92_abad_a8ef, CursorKind::Progress),
    (0xc68d_7480_d861_6c42, CursorKind::Progress),
    (0x2d29_4cb1_3520_77f7, CursorKind::Progress),
    (0xb3c3_20fe_d6f1_4483, CursorKind::Progress),
    (0x3a31_bd4c_ce1d_46d3, CursorKind::Progress),
    (0xfad6_291d_c942_f9a0, CursorKind::Progress),
    (0x0b9b_79f5_d7f3_86c5, CursorKind::Progress),
    (0xc267_067a_20a8_97e2, CursorKind::Progress),
    (0xa500_b3ef_54b0_7958, CursorKind::Progress),
    (0x71fb_2395_97a6_932b, CursorKind::Help),
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
