# Scanit

Scott's book scanner for GNOME: a gtkmm-4 / C++23 app that drives an AirScan
(eSCL) flatbed -- the Epson ET-8550 -- over the network, with no vendor
driver. Built for scanning whole books, page after page, into his library
pipeline (scan -> Optik -> ODT -> EPUB -> Librioteca).

Seeded from Cairn (s002), wearing Rouster's look.

## Where it stands (s001)

- Finds the scanner by mDNS (`_uscan._tcp` / `_uscans._tcp`), or by an
  address you type on the Scanner page (or `SCANIT_HOST=ip[:port]`).
- Reads what it can do (glass size, resolutions, colour modes, formats) and
  keeps the document at `~/.cache/scanit/ScannerCapabilities.xml`.
- Scans the whole glass at 300 dpi gray (nearest the scanner offers), PNG if
  it takes PNG, else TIFF, else JPEG, into `~/Pictures/Scanit/scan-<time>.<ext>`.
- Stop cancels and deletes the job on the scanner.

Next: preview + crop to the book (milestone 2), presets / type / dpi (3),
book naming + continuous scanning (4), thumbnails / redo / zip (5).

## Build

```bash
# Fedora
sudo dnf install gtkmm4.0-devel spdlog-devel libsoup3-devel avahi-devel cmake gcc-c++
./build.sh
./build/scanit_selftest     # the model, headless
./build/scanit
./install.sh                # ~/.local, app grid entry; --uninstall to remove
```

`SCANIT_LOOK=0 ./build/scanit` shows the plain GTK shell instead of the look.

## Layout

| Layer | Where | Links |
|---|---|---|
| model -- eSCL as data, output names, the look's data, shortcuts | `include/model/`, `src/model/` | nothing (selftest) |
| io -- mDNS (Avahi), eSCL over HTTP (libsoup 3) | `include/io/`, `src/io/` | soup, avahi, spdlog |
| the app -- Shell split by category, About, Shortcuts | `include/`, `src/` | gtkmm |
