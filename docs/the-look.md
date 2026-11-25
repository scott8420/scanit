# The look -- an opt-in skin (Cairn s002)

Rouster's modern look, lifted into the seed (from Rouster s021) so any app
can wear it without copying Rouster by hand. Scanit is the first. The CleanMyMac spirit:
a deep gradient per page, a frosted sidebar with section headers, a hero on
each page, one big round action (the orb), results as cards. Dark, always.

**Opt-in.** One switch, `look::kLookDefault` in `include/Look.hpp`. Cairn ships
it ON so the seed shows it. A Plunk-style tool sets it `false` and gets the
plain shell: the stock StackSidebar, the theme's colours, `Appearance`
following the desktop's light/dark. Without rebuilding, `CAIRN_LOOK=0 ./build/cairn`
(or `=1`) shows the other shell. `off`/`on`, `no`/`yes` work too.

## What is where

| Part | File | Tested |
|---|---|---|
| The switch + loading the CSS + asking GTK for dark | `include/Look.hpp`, `src/Look.cpp` | eyes |
| The skin | `resources/look.css` (in the gresource, prefix `/io/github/example/Cairn`) | selftest: every tint has its window, orb and act rules; none extra; no double hyphen in a comment |
| Hero emblems (full colour, 128 px) | `resources/icons/scalable/apps/cairn-hero-*.svg` | eyes |
| Page states: start / running / results | `core/Look.hpp` (`PageStates`) | selftest: every legal move, every illegal one refused |
| The sidebar as data: sections + items (id, icon, title, tint) | `core/Look.hpp` (`NavEntry`, `cairn_nav()`, `lint()`) | selftest: lints clean; lint catches dups / unknown tints / no id |
| The sidebar built from it | `Shell_zones.cpp` `build_nav` | eyes |
| The demo's work (a 25 ms timer to 100) | `Shell_helpers.cpp` `look_tick` | eyes |
| Page -> stack child + window tint | `Shell_handlers.cpp` `on_nav` | eyes |
| The demo page using all three states | `Shell_zones.cpp` `build_look_page`, `Shell_helpers.cpp` `paint_look` | eyes |

## The vocabulary (look.css classes)

Everything hangs off `window.look`, so a window that did not ask is untouched.

- Window: `.look`, plus `.tint-<name>` for the page showing (set by `on_nav`
  from the page's `NavEntry`).
- Sidebar: `.sidebar-pane` (the box), `.sidebar-brand`, `.sidebar-section`
  (a header row's label; the row carries `.section-row`).
- Hero: `.hero-title`, `.hero-sub`, `.hero-big`, `.hero-caption`.
- Buttons: `button.orb` (the one big action), `button.ghost` (quiet;
  `.small`, `.mode` for a checked toggle), `button.act` in an `.act-bar`.
- Results: `.card`, `.card-title`, `.card-big`, `.card-sub`, `.dot`,
  `.detail`, `.dim`, `.status-chip`, `.ok` / `.note` / `.stop`.
- `spinner.big` for the running state. Stock lists frost on the gradient.

**Tints:** magenta, green, orange, sky, rose, amber, violet, lime, teal,
indigo, slate, emerald. Each is three rules: the window gradient, the orb, the
act button. Rouster's module colours, renamed by hue (scan = magenta, sweep =
green, dev = orange, dups = sky, archive = rose, projects = amber, rules =
violet, profile = lime, preflight = teal, activity = indigo, system = slate,
tidy = emerald). Add a tint: three rules in look.css, its name in
`core::look::tints()`; the selftest fails until both agree. (look.css was
generated once from a table; it is a plain file now -- edit it by hand.)

## Starting an app with the look

1. Keep `kLookDefault = true`. Rename the env var with the app (`TIDY_LOOK`).
2. Replace `cairn_nav()` with the app's sidebar: sections and items, one tint
   per page. The page id is the stack child name.
3. Rename the emblems: `cairn-hero-*.svg` -> `<app>-hero-<page>.svg`, listed in
   the gresource. Sidebar icons: `<app>-<page>-symbolic.svg` (a 16 px
   `currentColor`-style symbolic; Rouster's are the pattern), or stock names.
4. The gresource prefix and `load_from_resource` path move with the app id
   (`/io/github/scott8420/<App>/look.css`) -- the one-pass rename.
5. Each module page: a `widgets::Stack` with children named by
   `core::look::name(State)`, a `PageStates` member, verbs that ask it first
   and repaint only when it changed (`on_look_begin` / `on_look_stop` /
   `on_look_reset` are the shape). A real page runs a worker thread and a
   Dispatcher where the demo has a timer.
6. Delete the demo page (`build_look_page`, its members and actions) once the
   app's first real page exists.

## Leaving it off

`kLookDefault = false`. Nothing else: the look's sidebar and demo page are not
built, look.css is not loaded, the StackSidebar comes back, Appearance follows
the desktop again. The code can stay or go; nothing references it at run time.

## Known

- **Dark, always.** The colours are literals designed on a dark ground (GTK
  4.14 has no `var()`), so a light-desktop user gets the dark skin. Rouster
  made the same call. A light variant would be a second set of rules under
  `window.look.light` -- not built.
- **The emblems are SVG.** GTK needs the gdk-pixbuf SVG loader (Fedora:
  `librsvg2`, installed with GNOME; Ubuntu: `librsvg2-common`). Without it
  every SVG icon -- the logo too -- draws as a broken image.
- Stock sidebar icons (`starred-symbolic`, `edit-undo-symbolic`...) come from
  the icon theme; Adwaita has them all.

## Not carried from Rouster

Rouster's page-specific classes stay in Rouster: the kind dots
(`.kind-*`), preflight colours (`.pf-*`), sweep rows, dev verdicts (`.dv-*`),
duplicates (`.du-*`), profile and system extras, the Help and About window
skins. The look carries the vocabulary every page shares; an app adds its own
classes beside it, under `window.look`.
