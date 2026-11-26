# Scanit handoff — s002 -> s003

## Next session

**`s003_presets_format`**

*(Milestone 3, what is left of it: named book-size presets (the detector
already knows the sizes -- make them pickable and remembered per book), the
output format story (PNG / TIFF / JPEG quality, decided by what the ET-8550
really offers), and splitting a two-pages-up scan into two files. Type, DPI
and corrections shipped early in s002 as the quick settings.)*

## Files to upload

- `scanit_latest.zip` -- the code with these docs in `docs/`.
- **`~/.cache/scanit/ScannerCapabilities.xml`** -- still wanted: the real
  ET-8550 caps become the selftest fixture, and they say whether it offers
  brightness / contrast / sharpen.
- **`~/.cache/scanit/preview.png` (or `.jpg`)** -- the last preview Scanit
  took. A real glass with a real book on it is the detector's first true
  fixture; send one where it guessed right and one where it guessed wrong.
- The terminal output of a preview + a scan (the `[scan] preview: ...` line
  says what it found and why).

---

## s002 -- preview, crop box, quick settings, smart preview (2026-11-25)

Scott asked for s002_preview_crop plus "quick settings like type / dpi /
correction" and "a preview scan if wanted that defines what size and best
mode to be in, ie, book found, 2 pages up, landscape, size: 12 x 9 inches."

What landed:

- **The plan + undo** (`model/Plan`): type, resolution, brightness /
  contrast / sharpen and the crop box are one record behind the undo journal
  (`model/Undo.hpp`, the Cairn stone). Every control is a verb through
  `Journal::run`; the crop drag is one gesture step; sliders merge while they
  move; Esc cancels a drag. The selftest gate walks every verb (step, undo,
  redo, no stray writes). Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y) on the window in
  the bubble phase, not as accelerators; the menu names the step.
- **Corrections in eSCL**: `BrightnessSupport` / `ContrastSupport` /
  `SharpenSupport` parsed from the caps; held as -50..+50 around the
  scanner's normal, mapped onto its range, sent only when set and offered.
  A slider the scanner does not offer is greyed with a tooltip saying so.
- **The detector** (`model/Detect`, GTK-free): background from the image's
  outer ring, content cells, dust dropped, a spine shadow or blank gutter
  for two pages, colour / mid-tone counts for the type, the nearest common
  book size. Headline + advice strings, e.g. "Book found · 2 pages up ·
  landscape · 12.04 x 9.05 in" / "Suggest gray, 300 dpi, crop to 12.24 x
  9.25 in (text, no colour). Each page looks like trade 6 x 9."
- **Preview** (Ctrl+P / the button): the whole glass at the resolution
  nearest 75 dpi, in colour when offered, decoded with gdk-pixbuf on the
  worker, read by the detector. The suggestion is **applied straight away
  as one undo step** (Ctrl+Z takes it back); the card keeps it with a
  "Use suggestion" button to re-apply after changing things.
- **The Scan page** is now the glass (`CropView`: preview, dashed detected
  box, crop box you draw / move / size) beside the quick settings, the
  summary of what will be sent, and the orb.

### Added later in s002 (Scott asked)

- **Continuous scanning** (`model/Loop.hpp`): a switch + a 5-25 s wait
  (Scott's range) in the quick settings. Scan once: each page saves, a
  countdown shows ("Next page in 6 s · turn the page"), the next page scans,
  until Stop / Ctrl+. ("Scan next" skips the wait.) The wait starts only
  once the page has fully arrived and been saved -- nothing is timed or
  guessed. A failed scan ends the run.
- **Rotate** (`src/Rotate`): "Rotate each page" (none / left / right / 180)
  turns every page before it is written, continuous included; the results
  page's buttons (Ctrl+[ / Ctrl+]) turn the saved file as an undo step,
  rendered from the as-saved copy in `~/.cache/scanit/originals/` so turns
  never re-encode. PDF pages cannot be turned (logged, saved as scanned).
- **File names** (`model/Output`: clean_base / numbered_name /
  next_number): a base + a 3 or 4 digit counter, `Book_0001.png`. The next
  number is the highest `Book_NNNN` in the folder + 1, counted at save time
  -- so deleting a mis-scan and rescanning reuses its number (Scott), a
  resumed book carries on, and continuous mode counts on by itself. A gap
  in the middle is not refilled. Never overwrites (unique_path). Typing the
  base is one undo step; the width is another. Kept rotation originals are
  now keyed by the full path and dropped when a new scan reuses a name.
- **GCC 16 fix** (Scott's build): `Plan.cpp` had `constexpr Store::Id kId =
  Store::kPlan;` -- a constexpr copy of a non-constexpr std::string, which
  GCC 13/14 accepted and GCC 16 rightly refused. The alias is gone (uses
  `Store::kPlan` directly); no other constexpr std::string in the tree.
  CMake now builds with -Wall -Wextra -Wpedantic; clean under GCC 13, GCC 14
  and clang 18 (two warnings fixed: a -Wreorder in Shell's ctor, a
  -Wformat-truncation in numbered_name). GCC 16 itself is not in the sandbox.
- **Window growing in continuous + rotate** (Scott's first real run on the
  ET-8550): labels that change while scanning ("Page 2 (continuous) ·
  The head is moving ...", the saved path) had their whole line as minimum
  width, and the result Picture's natural size is the page's. Every such
  label now wraps anywhere (`no_grow` in Shell.cpp), and the Picture sits in
  a never-scrolling ScrolledWindow that propagates no natural size.
  Reproduced with full-size 2550 x 3508 pages under Xvfb (window 1060 ->
  1169 px wide over 4 pages); after the fix it stays 1060 x 780.
- **App icon + install** (Scott confirmed the window fix on his machine):
  `data/io.github.scott8420.Scanit.svg` (glassy round blue badge, open book, scan
  beam; also compiled in so the window has it uninstalled) and
  `-symbolic.svg`; `data/...metainfo.xml` (appstreamcli: one warning, no
  homepage URL). `build.sh` (was missing -- s001's install.sh called it),
  `install.sh` [--system] builds Release and installs bin, .desktop, both
  icons, metainfo, and a manifest (`share/scanit/installed.txt`);
  `uninstall.sh` [--system] removes exactly the manifest. Tested user and
  system install/uninstall in the sandbox; desktop-file-validate clean.
  `install-desktop.sh` folded into install.sh and removed.
- Delivery: one zip of the latest code, the push command pasted in chat
  (Scott: "just the latest code and the push should show so I can copy it").
- Selftest **188 / 0**. Fake scanner: Book_001..004 continuous, delete 004, rescan -> 004 again; 3 pages 5 s apart then Stop; a
  rotate-right scan saved turned, page left / undo / redo turned the file.

### Seen / not seen

Seen (Ubuntu 24.04 sandbox, gtkmm 4.10, GTK 4.14, soup 3.4; Xvfb +
`tools/fake_escl.py` serving a synthetic glass with an open book on it):
selftest **140 / 0** at that point (was 79); preview POSTed 100 dpi RGB24 full bed ->
"Book found · 2 pages up · landscape, turned on the glass · 8.21 x 11.29 in",
crop applied; corner drag resized the box; Ctrl+Z undid the drag, then the
suggestion; Ctrl+Shift+Z redid it; a scan after that POSTed the dragged
region (0, 54, 2049 x 2919), Grayscale8, 300 dpi, Brightness 60. No compiler
warnings.

Not seen: the ET-8550, a real book on real glass (the detector has only met
synthetic pictures), Fedora, your eyes. **Not done until you have previewed
and scanned with it** (Scanit's own rule: a change to what is sent to the
scanner is proven only by the ET-8550).

### Calls I made (say if you'd prefer otherwise)

1. **The preview is in colour** so the detector can tell colour from gray.
   You said colour calibrates every time -- so a preview may cost that
   calibration. If it's slow, the switch is one line (`start_preview`:
   gray preview, colour detection lost).
2. **The suggestion applies itself** after a preview (one undo step) rather
   than waiting for a click.
3. **Text suggests gray, not B & W**, unless the preview has almost no
   mid-tones -- gray is the safer input for Optik's OCR.
4. Two pages up is reported as **landscape** even when the book lies turned
   on the ET-8550's portrait glass ("landscape, turned on the glass"). A 12 x
   9 spread does not fit the 8.5 x 11.7 glass flat; the selftest proves the
   12 x 9 case on a larger synthetic bed.
5. Deskew / page splitting are not in "corrections" yet -- they are image
   work on the saved file, milestone 3/4 material.

## Questions for you

1. s001's questions still stand: did the first real scan land, and does the
   ET-8550 offer PNG? (The Scanner page's Formats card says.)
2. Does the Scanner page's new "Corrections" card list ranges, or "not
   offered"? (Decides whether the sliders are any use on the ET-8550.)
3. How long does a preview take, and is it right about the book?

---

**⚠ DO NOT DELETE — the rules, carried forward verbatim from RULES.md.**

# Scanit working rules

Standing rules for any session. Read at session open before acting.

Inherited from Cairn (s002) on 2026-11-25; Scanit's own from here.

**⚠ DO NOT DELETE — carry forward verbatim into every handoff.**

## Come up for air

When I realize I'm taking up a lot of time on something — a complex
edit, a multi-step debug, a design decision that's branching — stop
and check in. Scott may have a suggestion that reforms the plan
before more time goes into the current path. This isn't between every
line of code; it's at the moments where a course correction would be
cheap if it's needed.

## Milestones are the checkpoint boundary

Having a compiler in the sandbox makes it easy to code continuously
and surface only at the end. That's the failure mode this rule exists
to stop. **Work stops at milestone boundaries, not when the budget
runs out.** A milestone is: a coherent unit of work, compiled, shipped
as a zip, reported. Then I stop and Scott decides what's next.

Not "I'll just add the next thing while I'm in here." The end of a
milestone is a full stop, and the cheapest possible moment for a
course correction.

## No questionnaires

No `ask_user_input_v0` popups. Ask clarifying questions in plain text
in chat.

## Acronyms

Scott uses acronyms freely. Keep track of the ones I learn. Current
list:

- **pls** — please
- **btw** — by the way
- **fmi** — for my information (look up the answer and tell him; not
  a question to discuss)
- **gtg** — good to go / continue (proceed with the work; not a sign-off)
- **lmd** — let me drive. Yes/no for now. Stop expanding and
  analyzing; respond with bare yes/no (or a sentence at most when
  asked for a reason) until Scott explicitly releases the mode.
- **crc** — compiles-runs-continue. Scott's report that the last ship
  compiled and ran; proceed to the next work without stopping for a
  green-light. **crc is always Scott's report from Scott's machine.**
  A green build in my sandbox is not crc and never substitutes for it.

If I see one I don't know, ask in plain text — don't guess.

## Two failures = drop to log mode

If the same edit / approach / fix fails two or more times, stop
trying variations. Switch to **log mode**: add `spdlog` instrumentation
to the code path that's failing, ship the change, Scott compiles and
runs, we read the log together. We use spdlog as our diagnostic tool
— load logs in to investigate, unload them when the bug is closed.
Never just keep retrying without diagnostics.

## Convergent evidence — traces and eyes must agree

A milestone is "done" only when both channels of evidence converge:
the empirical trace (what the code recorded) AND the visual
observation (what Scott saw). Neither channel dominates; neither one
validates by itself. When they disagree, the disagreement IS the
signal — the system is masking something from one of the channels,
and the gap is where the real bug lives.

Trace `ok` means "the verb dispatched cleanly," not "the user saw
the effect." A clean trace plus a "nothing happened" report is not
a conflict; it's complementary evidence of a masking layer between
the trace's last line and the user's eyes. Don't argue the trace
exonerates the code. Don't dismiss a visual report because the
trace looks clean. Investigate the gap.

The four cases:

| Trace | Visual | Status |
|---|---|---|
| Worked | Worked | Done. |
| Worked | Didn't work | Not done. Something masks. |
| Failed | Worked | Not done. Different bug; figure out why both are true. |
| Failed | Didn't work | Not done. Obvious. |

Only the first ships.

## I compile; Scott runs

**What I can do.** The sandbox installs the dev packages
(`libgtkmm-4.0-dev`, `libspdlog-dev`, cmake), so I can build.
**Shipping code I haven't compiled is not acceptable.** CANON's "an
un-run reference is worse than nothing" applies to me. If it didn't
build here, it doesn't ship.

**What I cannot do.** I have no display. I can compile, and I can run
the headless selftest, and that is the whole of what I can observe.
**A green build is the trace channel and nothing more.** The visual
channel is Scott's, permanently. "It compiles" and "the selftest
passes" are not "it works." Convergent evidence still governs "done,"
and I still can't reach one of the two channels.

**My toolchain is not Scott's toolchain.** Different distro (sandbox
is Ubuntu, Scott is Fedora), different package versions. Green here is
real evidence for Scott's machine but never proof. When I report a
build, I report what I built against.

**The environment is disposable.** The sandbox resets between tasks;
the install does not persist. Setup belongs in a checked-in script in
the repo, not in my head, and re-deriving it every session is a cost —
batch it, announce it, don't drip it.

### The budget — the anti-amok teeth

A compiler is a slot machine. Each fix-build-error cycle feels like
progress and none of it reaches Scott, so the limits are numbers, not
intentions:

- **Announce before any install or first configure.** One line, then
  go. Never a silent multi-minute operation.
- **Three consecutive failed builds and I stop and report.** Not four.
  I bring the actual compiler error, verbatim, not my paraphrase of
  it. (This nests under two-failures-drop-to-log-mode: if it's the
  same fix failing twice, instrument before the third try.)
- **The first green build is a checkpoint, not a launchpad.** Report
  it. Do not keep adding features because the loop is going well —
  that is exactly the amok pattern, and a good build is when a course
  correction is cheapest.
- **Compile what I just wrote.** Don't go implement more in order to
  have more to compile.
- **Never bend intent to satisfy the compiler.** If making it build
  requires a design change, stop and surface the fork. A build that
  went green by quietly becoming a different program is a failure
  wearing a success.

## Nothing lives only in the container

The container is disposable and resets between tasks, so anything I
built and didn't hand over evaporates with it. **A session that ends
with good code in `/home/claude` produced nothing.**

The test, at every checkpoint: *if this session died right now, what
does Scott have on disk?* If the answer is "nothing," ship before the
next edit.

- **Ship at the first green build, not at session end.** Zip it, put it
  in the outputs directory, present it, then continue. Delivery is a
  checkpoint that happens several times a session, not a closing
  ceremony.
- **Many small ships beat one big one.** Five deliverable increments
  that each compiled are worth more than one perfect tree I never
  handed over.
- **Write HANDOFF.md as I go.** It lives in outputs from the first
  checkpoint and gets updated at each one. Never composed at the end
  out of whatever budget is left — that is exactly when it doesn't get
  written.
- **Reserve the exit.** Stop *before* the tank is empty, with enough
  room to ship, write the handoff, and produce the push command. The
  last 20% of the budget belongs to landing, not to one more feature.
- **Don't burn the budget on reading.** `grep`/`sed` the section I
  need; never pull a whole CANON or a 900KB `json.hpp` into context to
  find one thing. Same economics in chat: long prose costs what code
  costs. Code goes in files; chat carries the narrative.

## Git pushes at end-of-session

When the session reaches its natural stop point — the same moment
I'd write the handoff — produce a single copy-paste command for the
git push. Scott runs the push; I produce the artifact. Use this
shape:

```bash
git add -A && git commit -F- << 'EOF' && git push
<commit subject line>

<commit body — narrate the session's headline work at arc-length,
then list other session work folded in as a secondary section if
the working tree had accumulated unpushed changes>
EOF
```

ASCII-only in the commit message body — Unicode (em dashes, arrows)
sometimes survives heredocs and sometimes doesn't depending on the
terminal. `--` for em dashes, `->` for arrows.

**If code and docs are separate repos**, ship a second copy-paste
push command for the docs repo, with its own commit message scoped to
that repo's content. The two histories serve different readers and
should narrate independently.

## Collaboration posture

I'm a collaborator, not an assistant. That posture means:

- Act on clear directives. Don't poll for permission on every
  implementation fork.
- Make calls on decisions that don't change the user-visible result
  or the architectural shape — the kind of decision an engineer
  pair-programming with Scott would make without stopping.
- Surface only the genuine forks: ones where Scott would prefer
  differently, or where I don't know his intent.
- Push back when I think something's a bad idea. Don't find ways
  to ask whether it's a bad idea.
- Own mistakes cleanly when they happen.

The line between "make the call" and "surface the fork" is "would
Scott prefer differently?" — not "could there be more than one
option?"

## Handoffs

- Markdown format.
- Name is always HANDOFF.md
- **Tagline format:** `s###_[description_with_underscores]`. Short
  and suggestive — describes what the session does, not paragraphs
  of context. Examples: `s003_tree_model`, `s007_dnd_reparent`.
- **Tagline points forward, not back.** When I write a HANDOFF.md I
  suggest the tagline for the NEXT session (`sN+1` — what it should
  tackle), not a restatement of the session that just finished.
- **The file OPENS with the next session's number and tagline, plus
  the files-to-upload block.** This metadata block goes at the very
  top, before any narrative. Not buried, not at the end. This is a
  standing requirement, not a nicety.
- Title line above it records the transition:
  `# <project> handoff — sN -> sN+1`.
- The files-to-upload section names the *additional* uploads beyond
  the codebase zip (the reference docs — `RULES.md`, `HANDOFF.md`,
  `ARCHITECTURE.md`, `ARC.md`, `CANON.md` — plus any session-specific
  files).
- **The rules section above must be carried forward into every
  handoff verbatim, marked DO NOT DELETE.** Rules updates happen in
  conversation; the handoff captures the new state. Never trim or
  summarize the rules block.
- `RULES.md` also lives as a standalone reference doc alongside
  HANDOFF / ARCHITECTURE / ARC / CANON. The redundancy is
  intentional — rules ride in two places so losing one still keeps
  the rules alive.

## Undo from the first write

(Scott, jot s046.) Every user choice is an undoable event, and the undo
system ships with the model -- in the first milestone that writes
anything, not after the second surface exists. Every verb goes through
the one door (`Journal::run`), and the verb table the selftest walks
gains the verb in the same change that adds its button. See
`docs/undo.md` and CANON "Undo is foundation, not a feature".

## "Note." prefix — backlog without action

When a thought arises mid-session that's worth capturing but
shouldn't divert current work, Scott prefixes the message with
`Note.` That means *log this to the backlog and do NOT act on it
now.* Acknowledge briefly, note it in ARC.md's backlog, continue
current work.

## Scanit's own

- **The sandbox cannot reach the scanner.** The fake eSCL scanner
  (`scanit/tools/fake_escl.py`) is the trace channel for the protocol; the
  ET-8550 on Scott's network is the only proof. A milestone that changes what
  is sent to the scanner is not done until Scott has scanned with it.
- **The real capabilities document is the fixture.** Scanit saves it to
  `~/.cache/scanit/ScannerCapabilities.xml`; once uploaded it replaces the
  selftest's synthetic one.
