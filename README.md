# Scanit

Scott's book scanner for GNOME: a gtkmm-4 / C++23 app that drives an AirScan
(eSCL) flatbed -- the Epson ET-8550 -- over the network, with no vendor
driver. Built for scanning whole books, page after page, into his library
pipeline (scan -> Optik -> ODT -> EPUB -> Librioteca).

Seeded from Cairn (s002), wearing Rouster's look.

## Where it stands (s002)

- Finds the scanner by mDNS (`_uscan._tcp` / `_uscans._tcp`), or by an
  address you type on the Scanner page (or `SCANIT_HOST=ip[:port]`).
- Reads what it can do (glass size, resolutions, colour modes, formats) and
  keeps the document at `~/.cache/scanit/ScannerCapabilities.xml`.
- Scans the whole glass at 300 dpi gray (nearest the scanner offers), PNG if
  it takes PNG, else TIFF, else JPEG, into `~/Pictures/Scanit/scan-<time>.<ext>`.
- Stop cancels and deletes the job on the scanner.
- **Quick settings** beside the glass: type (colour / gray / B & W),
  resolution (what the glass offers), corrections (brightness / contrast /
  sharpen, when the scanner offers them).
- **Preview** (Ctrl+P): a fast pass of the whole glass, read for what is on
  it -- "Book found · 2 pages up · landscape · 12.04 x 9.05 in" -- and the
  crop, type and resolution it suggests, applied as one step.
- **Crop box**: drag on the glass to draw, move or size it; only that region
  is scanned.
- **Continuous scanning**: switch it on in the quick settings, pick a wait
  (5-25 s), press Scan once. Each page is saved, then a countdown runs
  ("Next page in 6 s -- turn the page"), then the next page scans -- until
  Stop (or Ctrl+.). The countdown starts only when the page has fully
  arrived from the scanner, so a slow scan never eats into the wait.
- **File names**: a base and a counter -- `Book_0001.png` (or `Book_001`).
  The number is counted from the folder at each save: the highest
  `Book_NNNN` already there, plus one. Delete a mis-scan and scan again and
  it takes that number back; continuous scanning just counts on. Nothing is
  overwritten. Blank base: `scan-<date>-<time>`.
- **Rotate**: "Rotate each page" (none / left / right / 180) turns every
  page as it arrives, continuous mode included. On a scanned page, the
  rotate buttons (Ctrl+[ / Ctrl+]) turn the saved file; the as-saved bytes
  are kept in ~/.cache/scanit/originals/ so repeated turns never re-encode.
- **Undo**: every choice above is a step. Ctrl+Z / Ctrl+Shift+Z; the menu
  names the step.

Next: presets + format + splitting two pages (rest of 3), book naming +
continuous scanning (4), thumbnails / redo / zip (5).

## Build

```bash
# Fedora
sudo dnf install gtkmm4.0-devel spdlog-devel libsoup3-devel avahi-devel cmake gcc-c++
./build.sh
./build/scanit_selftest     # the model, headless
./build/scanit
./install.sh                # build (Release) + install for you under ~/.local: app grid entry, icons
./install.sh --system       # ... or for everyone under /usr/local (sudo)
./uninstall.sh [--system]   # remove exactly what was installed; your scans stay
```

`SCANIT_LOOK=0 ./build/scanit` shows the plain GTK shell instead of the look.

## Layout

| Layer | Where | Links |
|---|---|---|
| model -- eSCL as data, the plan + undo, the preview detector, output names, the look's data, shortcuts | `include/model/`, `src/model/` | nothing (selftest) |
| io -- mDNS (Avahi), eSCL over HTTP (libsoup 3) | `include/io/`, `src/io/` | soup, avahi, spdlog |
| the app -- Shell split by category, About, Shortcuts | `include/`, `src/` | gtkmm |
