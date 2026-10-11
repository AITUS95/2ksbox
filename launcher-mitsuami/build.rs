include!("../packaging/windows/win-icon.rs");

fn main() {
    // `sidebar`: the machines are the window's sidebar, the toolbar is
    // Start and the machine's windows, and the rest is in the menus (the
    // menu bar on macOS, the primary menu on GTK). Windows and Kirigami
    // keep the list and the details' buttons.
    println!("cargo::rustc-check-cfg=cfg(sidebar)");
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let feature = |f: &str| std::env::var_os(format!("CARGO_FEATURE_{f}")).is_some();
    let gtk = os == "linux" && feature("GTK") && !feature("KDE");
    if os == "macos" || gtk {
        println!("cargo::rustc-cfg=sidebar");
    }
    // `menu_bar`: the app's commands, the user manual and About are menus
    // (macOS's menu bar, GTK's primary menu, a menu bar in Windows' title
    // bar), not toolbar buttons; Kirigami keeps the buttons and the
    // window takes the commands' keys itself.
    println!("cargo::rustc-check-cfg=cfg(menu_bar)");
    if os == "macos" || gtk || os == "windows" {
        println!("cargo::rustc-cfg=menu_bar");
    }
    // The icon Explorer shows for the launcher and its manifest: this crate
    // becomes 2ksbox.exe in the Windows package.
    embed_windows_resources();
}
