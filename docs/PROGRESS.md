# Scanit -- progress

Updated at every ship. **Done** = shipped. **crc** = Scott's machine compiled
and ran it; without crc it has only been seen in the sandbox.

Last updated: s002 (2026-11-25). Selftest **188 pass / 0 fail** (Ubuntu 24.04
sandbox, gtkmm 4.10, GTK 4.14, soup 3.4).

| # | Milestone | Status |
|---|---|---|
| 1 | Find the scanner + one full-glass scan to a file | **Done s001** -- fake scanner end to end; awaiting crc + a real scan |
| 2 | Preview + crop box (undo ships here) | **Done s002** -- plus smart preview (detect book / pages / size / type); fake scanner only, awaiting crc + a real preview |
| 3 | Presets, type, DPI, format | Type, DPI, corrections **shipped s002** (quick settings); presets + format next (s003) |
| 4 | Book naming, folders, continuous mode | Continuous mode + base_0001 naming **shipped s002**; folder per book still to do |
| 5 | Thumbnails, redo / delete, Archive.zip | Rotate (setting + per page, undoable) **shipped s002** |
