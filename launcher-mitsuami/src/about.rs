//! About 2ksbox, over `launcher_core::about`: the version, the licence
//! and the projects 2ksbox is built on, each a link with what it does
//! for us and its licence. Opened by the Help menu's About item
//! (`app_menu`; macOS's application menu, the end of GTK's primary menu
//! and of Kirigami's drawer). Its Licences button opens the licence
//! texts the package carries (`launcher_core::licenses`).

use launcher_core::about::{self, Credit};
use launcher_core::library;
use launcher_core::licenses::{self, Entry};
use std::path::PathBuf;
use mitsuami::prelude::*;

const NAME_W: f32 = 170.0;
const LICENSE_W: f32 = 150.0;
const ENTRIES_W: f32 = 260.0;

#[derive(Clone, Copy)]
pub struct About {
    open: Signal<bool>,
    /// The Licences window: whether it shows, what it lists (read from
    /// the package when it opens) and the entry whose text it shows.
    licenses: Signal<bool>,
    entries: Signal<Vec<Entry>>,
    selected: Signal<Vec<PathBuf>>,
}

impl Store for About {
    fn create() -> About {
        About { open: signal(false), licenses: signal(false), entries: signal(Vec::new()), selected: signal(Vec::new()) }
    }
}

impl About {
    pub fn show(&self) {
        self.open.set(true);
    }

    /// Opens the Licences window on 2ksbox's own licence.
    pub fn show_licenses(&self) {
        let entries = licenses::entries();
        self.selected.set(entries.first().map(|e| e.path.clone()).into_iter().collect());
        self.entries.set(entries);
        self.licenses.set(true);
    }

    /// The selected entry's text.
    fn license_text(&self) -> String {
        let path = self.selected.with(|s| s.first().cloned());
        self.entries.with(|entries| {
            entries.iter().find(|e| Some(&e.path) == path.as_ref()).map(licenses::text).unwrap_or_default()
        })
    }
}

/// The app's menus: `commands` (the main window's, `machines::command_menus`)
/// and Help, with the user manual, the machines folder and About. About
/// belongs in macOS's application menu and at the end of GTK's primary menu
/// and Kirigami's drawer, and the role puts it there; on Windows the menu
/// bar is in the title bar (`main`).
pub fn app_menu(about: About, commands: MenuBar) {
    set_menu(
        commands.menu(
            Menu::new("Help")
                .item(MenuItem::new(about::MANUAL).on_select(open_manual))
                .item(MenuItem::new(library::OPEN_FOLDER).on_select(open_machines_folder))
                .item(MenuItem::new("About 2ksbox").role(MenuRole::About).on_select(move || about.show())),
        ),
    );
}

/// Opens the user manual in the browser: Help's item.
fn open_manual() {
    open_url(about::MANUAL_URL);
}

/// Opens the machines' directory in the file manager: Help's item. A
/// library with no machine yet has no directory, so it is made first.
fn open_machines_folder() {
    let dir = library::default_dir();
    if let Err(e) = std::fs::create_dir_all(&dir) {
        eprintln!("[launcher] creating {}: {e}", dir.display());
        return;
    }
    spawn_local(async move {
        if let Err(e) = launch(&dir).await {
            eprintln!("[launcher] opening {}: {e:?}", dir.display());
        }
    });
}

/// The app's icon as pixels, decoded once here: `Image` takes a file or
/// pixels, and an installed launcher has no file of its own to point at.
fn icon() -> Option<Pixels> {
    #[cfg(not(windows))]
    let png = include_bytes!("../../packaging/icon/2ksbox-128.png");
    #[cfg(windows)]
    let png = include_bytes!("../../packaging/icon/windows/2ksbox-128.png");
    let mut reader = png::Decoder::new(std::io::Cursor::new(png.as_slice())).read_info().ok()?;
    let mut rgba = vec![0; reader.output_buffer_size()?];
    let info = reader.next_frame(&mut rgba).ok()?;
    if info.color_type != png::ColorType::Rgba || info.bit_depth != png::BitDepth::Eight {
        return None;
    }
    rgba.truncate(info.buffer_size());
    Some(Pixels::new(info.width, info.height, rgba).scale(2.0))
}

fn open_url(url: &'static str) {
    spawn_local(async move {
        if let Err(e) = launch_url(url).await {
            eprintln!("[launcher] opening {url}: {e:?}");
        }
    });
}

#[component]
pub fn AboutWindow() -> impl View {
    let about = use_store::<About>();
    view! {
        <Window
            title="About 2ksbox"
            size=Size::new(700.0, 600.0)
            min_size=Size::new(500.0, 380.0)
            modal=Modality::Application
            open=move || about.open.get()
            @close_request=move || about.open.set(false)
        >
            // The credits scroll edge to edge, between separators, with
            // their own padding inside so the scroll bar never covers them.
            // What's above them stays: on macOS in the title bar's glass,
            // the credits blurred under it, where the glass's hard edge is
            // the top separator (user).
            <Column grow=1.0 min_height=0>
                {crate::shot::arm(&["about"])}
                <Column header=true header_edge=HeaderEdge::Hard shrink=0.0>
                    <Column padding=Spacing::Lg gap=Spacing::Md>
                        <Row gap=Spacing::Lg align=Align::Center>
                            {icon().map(|p| view! { <Image source=ImageSource::Pixels(p) width=64 height=64/> })}
                            <Column gap=Spacing::Xs grow=1.0>
                                <Text text_style=TextStyle::Title>{format!("{} {}", about::NAME, about::VERSION)}</Text>
                                <Text>{about::TAGLINE}</Text>
                                <Text text_style=TextStyle::Caption color=Color::SecondaryLabel>{about::LICENSE}</Text>
                                <Row>
                                    <Button button_style=ButtonStyle::Borderless @click=|| open_url(about::SOURCE_URL)>
                                        {about::SOURCE_URL}
                                    </Button>
                                </Row>
                            </Column>
                        </Row>
                        <Text weight=FontWeight::Semibold>{about::THANKS}</Text>
                    </Column>
                    <Show when=|| !cfg!(target_os = "macos")>
                        <Separator/>
                    </Show>
                </Column>
                <ScrollView grow=1.0 min_height=0>
                    <Column gap=Spacing::Lg padding_x=Spacing::Xl padding_y=Spacing::Md>
                        {credit_groups()}
                    </Column>
                </ScrollView>
                <Separator/>
                <Row padding=Spacing::Lg gap=Spacing::Md justify=Justify::SpaceBetween>
                    <Button @click=move || about.show_licenses()>{licenses::OPEN}</Button>
                    <Button role=ButtonRole::Default @click=move || about.open.set(false)>"Close"</Button>
                </Row>
            </Column>
        </Window>
    }
}

/// The licence texts: every entry down the side, the selected one's text
/// beside it, read-only so it can still be selected and copied.
#[component]
pub fn LicensesWindow() -> impl View {
    let about = use_store::<About>();
    view! {
        <Window
            title=licenses::TITLE
            size=Size::new(860.0, 620.0)
            min_size=Size::new(560.0, 380.0)
            modal=Modality::Application
            open=move || about.licenses.get()
            @close_request=move || about.licenses.set(false)
        >
            <Column grow=1.0 min_height=0>
                {crate::shot::arm(&["licenses"])}
                <Row grow=1.0 min_height=0>
                    <List
                        each=move || about.entries.get()
                        key=|e: &Entry| e.path.clone()
                        selection_mode=SelectionMode::Single
                        selected=about.selected
                        list_style=ListStyle::Plain
                        width=ENTRIES_W
                        shrink=0.0
                        let:entry
                    >
                        <Column padding_x=Spacing::Lg padding_y=Spacing::Sm gap=Spacing::Xs min_width=0>
                            <Text max_lines=1u32>{entry.name.clone()}</Text>
                            <Text text_style=TextStyle::Caption color=Color::SecondaryLabel max_lines=1u32>
                                {entry.kind.label()}
                            </Text>
                        </Column>
                    </List>
                    <Separator orientation=Orientation::Vertical/>
                    <TextArea
                        a11y_label=licenses::TITLE
                        read_only=true
                        line_wrap=false
                        value=move || about.license_text()
                        grow=1.0
                    />
                </Row>
                <Separator/>
                <Row padding=Spacing::Lg justify=Justify::End>
                    <Button role=ButtonRole::Default @click=move || about.licenses.set(false)>"Close"</Button>
                </Row>
            </Column>
        </Window>
    }
}

/// The credits, a heading per group (the window's content is rebuilt
/// from a closure, so this is called rather than captured).
fn credit_groups() -> Vec<impl View + use<>> {
    about::GROUPS
        .iter()
        .map(|group| {
            let rows = group.credits.iter().map(credit_row).collect::<Vec<_>>();
            view! {
                <Column gap=Spacing::Xs>
                    <Text text_style=TextStyle::Headline>{group.title}</Text>
                    {rows}
                </Column>
            }
        })
        .collect()
}

/// A link button as wide as its label: Breeze gives every button an 80 px
/// minimum and centres the label in it, so names sat in the middle of the
/// name column (user). The desktop style draws the label itself (the
/// button has no content item to measure), so it's measured in the
/// button's font, and the button made that wide plus half a grid unit a
/// side. Elsewhere the button already hugs its label.
fn hug() -> Tweak<Button> {
    // `TextMetrics` isn't an item, so it has no parent to take the font
    // from: an invisible item holds it.
    #[cfg(all(target_os = "linux", feature = "kde"))]
    const HUG_METRICS: &str = r#"Item {
    id: hug
    objectName: "hugMetrics"
    visible: false
    property alias text: metrics.text
    readonly property real advanceWidth: metrics.advanceWidth
    TextMetrics { id: metrics; font: hug.parent ? hug.parent.font : Qt.application.font }
}"#;
    platform! {
        kde => mitsuami::kirigami::tweak(|button: &mitsuami::kirigami::QmlObject| {
            use mitsuami::kirigami::QmlObject;
            let metrics = button.child("hugMetrics").unwrap_or_else(|| {
                QmlObject::load_in(HUG_METRICS, *button)
            });
            metrics.set_str("text", &button.str("text"));
            let width = metrics.real("advanceWidth") + mitsuami::kirigami::grid_unit();
            button.set_real("implicitWidth", width.ceil());
        }),
        _ => Tweak::none(),
    }
}

/// One project: its name as a link, what it does for us, its licence.
fn credit_row(c: &'static Credit) -> impl View + use<> {
    view! {
        <Row gap=Spacing::Md align=Align::Center>
            <Row width=NAME_W shrink=0.0>
                <Button button_style=ButtonStyle::Borderless tooltip=c.url native=hug() @click=move || open_url(c.url)>
                    {c.name}
                </Button>
            </Row>
            <Text grow=1.0 max_lines=1u32>{c.what}</Text>
            <Text
                width=LICENSE_W
                shrink=0.0
                text_align=TextAlign::End
                text_style=TextStyle::Caption
                color=Color::SecondaryLabel
            >{c.license}</Text>
        </Row>
    }
}
