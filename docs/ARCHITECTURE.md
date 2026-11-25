# Scanit -- architecture

`main` -> `App::create()` -> `App` (`io.github.scott8420.Scanit`) ->
`ensure_shell()` builds the one `Shell` window. The Shell is the look's
frosted sidebar + a stack of pages (Scan, Scanner). On start it looks for the
scanner.

## Three layers, enforced by the link line

| Layer | Library | Knows | Tested by |
|---|---|---|---|
| model | `scanit_model` | eSCL as data (`Escl`), the plan + undo (`Plan`, `Undo.hpp`), the preview detector (`Detect`), output names (`Output`), the look's data (`Look`), shortcuts | `scanit_selftest`, headless |
| io | `scanit_io` | mDNS via Avahi (`Discovery`), eSCL over HTTP via libsoup 3 (`EsclClient`) | `tools/fake_escl.py` + Scott's scanner |
| app | `scanit` | gtkmm: Shell (split by category), `CropView`, About, Shortcuts, Look install | Scott's eyes |

`scanit_model` links nothing; a GTK or soup include there fails to build.
`scanit_io` links no GTK.

## The eSCL conversation

```
discover()                      _uscan._tcp / _uscans._tcp -> host, port, rs
GET  {base}/ScannerCapabilities -> model::escl::parse_caps   (kept in ~/.cache/scanit)
POST {base}/ScanJobs            <- model::escl::build_scan_settings
     201, Location              -> model::escl::job_url
GET  {job}/NextDocument         503 while it warms up (retry 1 s), then the image, streamed
GET  {job}/NextDocument         404 -- the scanner lets go of the job
DELETE {job}                    on Stop or failure
```

Lengths stay in eSCL's 1/300 inch everywhere in the model; conversion is for
display only.

## Threads

One worker at a time (`Shell::m_worker`): finding, or scanning. It writes
`m_job_*` under `m_mu` and `io::Progress`'s atomics, then emits `m_done`;
`on_worker_done` (UI thread) joins and repaints. A 150 ms tick shows
progress while a scan runs. No widget is touched off the UI thread.

## Pages (the look: `docs/the-look.md`)

- **Scan** (sky) -- `PageStates`: start (hero, what will be sent, the orb) ->
  running (spinner, phase, bytes, Stop) -> results (path, the page, Open
  folder / Done / Scan next).
- **Scanner** (slate) -- what was found and what it can do; an address field
  for when mDNS finds nothing.

## The plan and undo (s002)

```
 a control (toggle, drop-down, slider, crop drag, Use suggestion, Whole glass)
   -> Shell::on_* -> model::plan::Book verb -> Journal::run / begin..commit
   -> Store::put (counts a write with the door shut) -> paint_plan()
 Ctrl+Z / Ctrl+Shift+Z -> Shell::on_key (window, BUBBLE, not in a text box)
   -> win.undo / win.redo -> Journal::undo / redo -> paint_plan()
```

The plan is one record (`Plan`: mode, dpi, region, brightness, contrast,
sharpen). `Book::load(caps)` on finding a scanner is a load, not a choice:
fresh defaults, history cleared. `paint_plan` sets widgets under
`m_painting` so a repaint never records. `Book::settings()` is what a scan
sends.

## The preview (s002)

```
on_preview -> start_preview (worker): POST full bed, nearest 75 dpi, RGB24
  -> NextDocument bytes -> ~/.cache/scanit/preview.<ext>
  -> Gdk::PixbufLoader -> model::detect::Raster -> detect() -> Suggestion
on_worker_done (UI): CropView shows it + the dashed content box,
  m_book.apply(suggestion) -- one undo step, the card shows headline + advice
```

The preview rides the Scan page's running state and comes back to start
(`PageStates::stop`) -- the glass is its result, not a results page.
