# ARC.md -- what Scanit is doing now

## Phase

**s002 done (trace channel): preview, crop box, quick settings, smart
preview.** Next: s003, presets + format + splitting two pages.

The road (Scott, 2026-11-25), one milestone per session unless Scott says gtg:

1. Find the scanner + one full-glass scan to a file -- **s001**
2. Preview (fast low-res pass) + drag a crop box over the glass
3. Presets (named book sizes), type (colour / gray / B&W), DPI, format
4. Naming (Author / Title / 0001), save folder per book, continuous mode
   (turn the page, press a key), resume point
5. Thumbnails strip, redo / delete a page, zip to Archive.zip for Optik

## s002 -- preview, crop, quick settings, smart preview (2026-11-25)

Scott: "quick settings like type/dpi/correction, etc. a preview scan if
wanted that defines what size and best mode to be in, ie, book found, 2
pages up, landscape, size: 12 x 9 inches." So milestone 2 took the type /
DPI half of milestone 3 with it, plus corrections and a detector.

- `model/Plan` -- the plan (type, dpi, crop, corrections) as one record
  behind the undo journal; verbs + the selftest gate. Undo arrives with the
  first user choice, as RULES says.
- `model/Detect` -- reads a preview raster: content box, two pages (spine
  shadow or gutter), orientation, nearest book size, best type and dpi.
- `model/Escl` -- Brightness / Contrast / Sharpen support parsed and sent.
- `CropView` -- the glass, the preview, the detected box, the crop box.
- Shell -- the Scan page's start state is now glass | quick settings; Preview
  (Ctrl+P) runs a worker job, decodes with gdk-pixbuf, detects, applies the
  suggestion as one step. Ctrl+Z / Ctrl+Shift+Z on the window, bubble phase.

Later in s002: continuous scanning (`model::Loop`, 5-25 s wait that starts
only once the page has arrived) and rotation (every page as it arrives, or
a saved page after, undoable from a kept original; `src/Rotate`).

Calls (Claude's): colour preview (may calibrate -- Scott to time it); the
suggestion applies itself; text -> gray not B&W; a spread reads "landscape"
even when it lies turned on the glass.

## s001 -- eSCL, first scan (2026-11-25)

Scott: "It would be nice to interface with my epson printer from the laptop
... size to the book so there is less area and time devoted to scanning empty
space." GNOME's Document Scanner already sees it as "eSCL EPSON ET-8550
Series", so Scanit speaks eSCL directly (libsoup) rather than through SANE.
Colour scans calibrate every time (Scott); gray is the default partly to
learn whether gray skips it.

What landed:

- Seeded from Cairn s002 (the look on), renamed, demo pages and the tile,
  drawers, recents, stones and text-map stones removed. `core/` -> `model/`.
- `model/Escl` -- caps parse (platen + ADF apart), settings build, job URLs,
  base URLs, resolution / format choice, regions, clamp.
- `model/Output` -- `scan-YYYYMMDD-HHMMSS.ext`, collision numbering, sizes.
- `io/Discovery` (Avahi threaded poll, 3 s), `io/EsclClient` (soup 3 sync:
  caps, status, scan with 503 retry, streamed NextDocument, cancel -> DELETE,
  drain -> 404; self-signed TLS accepted).
- Shell: Scan page (three states), Scanner page (cards, address field),
  worker + Dispatcher, Ctrl+Enter / Ctrl+. / Ctrl+O / Ctrl+R.
- PNG preferred; a scanner that lists PNG then refuses the job gets one JPEG
  retry.
- `tools/fake_escl.py` -- a stand-in scanner.

### Seen / not seen

Seen (Ubuntu 24.04 sandbox, gtkmm 4.10, Xvfb, fake scanner on 127.0.0.1):
selftest 79 / 0; caps -> POST 201 -> 503 -> 200 PNG -> 404, file written,
results page shows the page; Stop -> DELETE -> start page "Stopped."; no
Avahi -> "No scanner / Avahi: Daemon not running". Not seen: the ET-8550,
mDNS on a real network, Fedora, Scott's eyes.

Calls (Claude's): talk eSCL directly, not SANE; PNG > TIFF > JPEG; files to
~/Pictures/Scanit until book naming (4); no undo yet -- s001 has no user
choice to undo (the first is the crop box, s002, and undo ships with it).

## Backlog ("Note." items land here)

- The real logo from Curvz (the symbolic one is a placeholder).
- Learn whether gray skips the calibration on the ET-8550 (time both).
- If the ET-8550 only gives JPEG: decide on JPEG quality vs converting.
- Split a two-pages-up scan into two files at the detected spine (s003/s004).
- Deskew (image work on the saved file).
- Gray preview if the colour one is slow (loses colour detection).
- Replace the detector's synthetic fixtures with Scott's real previews.
