// scanit_selftest -- the headless model harness.
//
// Everything with no GTK and no network in it is walked here: the eSCL
// documents (capabilities in, scan settings out, job URLs), the output names,
// the look's page states and sidebar, the shortcut registry.
//
//   ./build/scanit_selftest

#include "model/Detect.hpp"
#include "model/Escl.hpp"
#include "model/Plan.hpp"
#include "model/Look.hpp"
#include "model/Loop.hpp"
#include "model/Output.hpp"
#include "model/Shortcuts.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

// A capabilities document in the shape AirScan scanners send. SYNTHETIC --
// written from the eSCL spec, not captured from the ET-8550. Scanit saves the
// real one (~/.cache/scanit/ScannerCapabilities.xml) on first contact; it
// replaces this fixture once Scott uploads it.
const char* kCaps = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<scan:ScannerCapabilities xmlns:scan="http://schemas.hp.com/imaging/escl/2011/05/03" xmlns:pwg="http://www.pwg.org/schemas/2010/12/sm">
  <pwg:Version>2.63</pwg:Version>
  <pwg:MakeAndModel>EPSON ET-8550 Series</pwg:MakeAndModel>
  <scan:UUID>cfe92100-67c4-11d4-a45f-e0bb9e000000</scan:UUID>
  <scan:Platen>
    <scan:PlatenInputCaps>
      <scan:MinWidth>16</scan:MinWidth>
      <scan:MaxWidth>2550</scan:MaxWidth>
      <scan:MinHeight>16</scan:MinHeight>
      <scan:MaxHeight>3508</scan:MaxHeight>
      <scan:SettingProfiles>
        <scan:SettingProfile>
          <scan:ColorModes>
            <scan:ColorMode>BlackAndWhite1</scan:ColorMode>
            <scan:ColorMode>Grayscale8</scan:ColorMode>
            <scan:ColorMode>RGB24</scan:ColorMode>
          </scan:ColorModes>
          <scan:DocumentFormats>
            <pwg:DocumentFormat>application/pdf</pwg:DocumentFormat>
            <pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>
            <scan:DocumentFormatExt>application/pdf</scan:DocumentFormatExt>
            <scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>
          </scan:DocumentFormats>
          <scan:SupportedResolutions>
            <scan:DiscreteResolutions>
              <scan:DiscreteResolution><scan:XResolution>600</scan:XResolution><scan:YResolution>600</scan:YResolution></scan:DiscreteResolution>
              <scan:DiscreteResolution><scan:XResolution>100</scan:XResolution><scan:YResolution>100</scan:YResolution></scan:DiscreteResolution>
              <scan:DiscreteResolution><scan:XResolution>300</scan:XResolution><scan:YResolution>300</scan:YResolution></scan:DiscreteResolution>
              <scan:DiscreteResolution><scan:XResolution>200</scan:XResolution><scan:YResolution>200</scan:YResolution></scan:DiscreteResolution>
              <scan:DiscreteResolution><scan:XResolution>1200</scan:XResolution><scan:YResolution>1200</scan:YResolution></scan:DiscreteResolution>
            </scan:DiscreteResolutions>
          </scan:SupportedResolutions>
        </scan:SettingProfile>
      </scan:SettingProfiles>
    </scan:PlatenInputCaps>
  </scan:Platen>
  <scan:Adf>
    <scan:AdfSimplexInputCaps>
      <scan:MaxWidth>2550</scan:MaxWidth>
      <scan:MaxHeight>4200</scan:MaxHeight>
      <scan:SettingProfiles><scan:SettingProfile>
        <scan:ColorModes><scan:ColorMode>RGB24</scan:ColorMode></scan:ColorModes>
        <scan:SupportedResolutions><scan:DiscreteResolutions>
          <scan:DiscreteResolution><scan:XResolution>150</scan:XResolution></scan:DiscreteResolution>
        </scan:DiscreteResolutions></scan:SupportedResolutions>
      </scan:SettingProfile></scan:SettingProfiles>
    </scan:AdfSimplexInputCaps>
  </scan:Adf>
</scan:ScannerCapabilities>
)XML";
}  // namespace

// A synthetic glass for the detector: RGB, filled, with rectangles painted on.
struct Glass {
    int w, h, dpi;
    std::vector<std::uint8_t> px;
    Glass(double w_in, double h_in, int d, std::uint8_t bg)
        : w(int(w_in * d)), h(int(h_in * d)), dpi(d), px(std::size_t(w) * h * 3, bg) {}
    void rect(double x_in, double y_in, double w_in, double h_in, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int y = int(y_in * dpi); y < std::min(h, int((y_in + h_in) * dpi)); ++y)
            for (int x = int(x_in * dpi); x < std::min(w, int((x_in + w_in) * dpi)); ++x) {
                auto* p = &px[(std::size_t(y) * w + x) * 3];
                p[0] = r; p[1] = g; p[2] = b;
            }
    }
    // Lines of "text": dark strokes 1 px high every 0.17", broken into words.
    void text(double x_in, double y_in, double w_in, double h_in) {
        for (double y = y_in; y < y_in + h_in; y += 0.17)
            for (double x = x_in; x < x_in + w_in; x += 0.45)
                rect(x, y, 0.36, 0.03, 30, 30, 30);
    }
    scanit::model::detect::Raster raster() const {
        return {w, h, 3, w * 3, px.data(), dpi};
    }
};

int main() {
    // ── eSCL: capabilities in ───────────────────────────────────────────────
    {
        namespace e = scanit::model::escl;
        std::cout << "\n[escl]\n";
        e::Caps c;
        std::string err;
        const bool ok = e::parse_caps(kCaps, c, &err);
        check("escl: caps parse", ok, err);
        check("escl: version", c.version == "2.63", c.version);
        check("escl: make and model", c.make_model == "EPSON ET-8550 Series", c.make_model);
        check("escl: platen max size (1/300 in)", c.platen.max_w == 2550 && c.platen.max_h == 3508,
              std::to_string(c.platen.max_w) + "x" + std::to_string(c.platen.max_h));
        check("escl: platen min size", c.platen.min_w == 16 && c.platen.min_h == 16);
        check("escl: resolutions sorted, deduped, platen only",
              c.platen.resolutions == std::vector<int>{100, 200, 300, 600, 1200});
        check("escl: three colour modes", c.platen.modes.size() == 3);
        check("escl: formats deduped across Format/FormatExt",
              c.platen.formats == std::vector<std::string>{"image/jpeg", "application/pdf"} ||
              c.platen.formats == std::vector<std::string>{"application/pdf", "image/jpeg"});
        check("escl: the ADF parsed on its own (not the platen's values)",
              c.adf.present && c.adf.max_h == 4200 && c.adf.resolutions == std::vector<int>{150});
        check("escl: gray supported", e::supports(c.platen, e::Mode::Gray));
        check("escl: colour supported", e::supports(c.platen, e::Mode::Color));

        e::Caps bad;
        check("escl: a non-caps document is refused", !e::parse_caps("<html>nope</html>", bad, &err) && !err.empty(), err);
        check("escl: no platen is refused",
              !e::parse_caps("<scan:ScannerCapabilities><pwg:Version>2.0</pwg:Version></scan:ScannerCapabilities>", bad, &err));
        check("escl: unprefixed tags parse too",
              e::parse_caps("<ScannerCapabilities><PlatenInputCaps><MaxWidth>100</MaxWidth><MaxHeight>200</MaxHeight></PlatenInputCaps></ScannerCapabilities>", bad, &err) &&
              bad.platen.max_w == 100);

        check("escl: state from ScannerStatus",
              e::parse_state("<scan:ScannerStatus><pwg:Version>2.6</pwg:Version><pwg:State>Idle</pwg:State></scan:ScannerStatus>") == "Idle");

        // Choosing.
        check("escl: nearest resolution exact", e::nearest_resolution(c.platen, 300) == 300);
        check("escl: nearest resolution between (ties go up)", e::nearest_resolution(c.platen, 250) == 300);
        check("escl: nearest resolution above the top", e::nearest_resolution(c.platen, 4800) == 1200);
        check("escl: no resolutions -> 0", e::nearest_resolution(e::Source{}, 300) == 0);
        check("escl: format: first preferred the scanner has",
              e::choose_format(c.platen, {"image/png", "image/tiff", "image/jpeg"}) == "image/jpeg");
        check("escl: format: none preferred -> its first",
              e::choose_format(c.platen, {"image/png"}) == c.platen.formats.front());
        check("escl: format: nothing listed -> jpeg", e::choose_format(e::Source{}, {}) == "image/jpeg");
        check("escl: extensions", e::extension_for("image/png") == "png" &&
              e::extension_for("image/jpeg") == "jpg" && e::extension_for("application/pdf") == "pdf" &&
              e::extension_for("x/y") == "bin");

        // Regions.
        auto bed = e::full_bed(c.platen);
        check("escl: full bed", bed.x == 0 && bed.y == 0 && bed.w == 2550 && bed.h == 3508);
        auto cl = e::clamp(c.platen, {2500, 3500, 500, 500});
        check("escl: clamp keeps a region on the bed",
              cl.x + cl.w <= 2550 && cl.y + cl.h <= 3508 && cl.w >= 16 && cl.h >= 16);
        auto tiny = e::clamp(c.platen, {10, 10, 1, 1});
        check("escl: clamp enforces the minimum size", tiny.w == 16 && tiny.h == 16);
        check("escl: pixels at 300 dpi = units", e::pixels(2550, 300) == 2550);
        check("escl: pixels at 600 dpi", e::pixels(2550, 600) == 5100);
        check("escl: inches label", e::inches_label(2550, 3300) == "8.50 x 11.00 in", e::inches_label(2550, 3300));

        // Settings out: what we send, then read back with our own parser's eyes.
        e::Settings s;
        s.version = c.version;
        s.mode = e::Mode::Gray;
        s.dpi = 300;
        s.region = {150, 300, 1650, 2550};
        s.format = "image/jpeg";
        const std::string x = e::build_scan_settings(s);
        auto has = [&](const std::string& frag) { return x.find(frag) != std::string::npos; };
        check("escl: settings carry the version", has("<pwg:Version>2.63</pwg:Version>"));
        check("escl: settings: region in 1/300 in",
              has("escl:ThreeHundredthsOfInches") && has("<pwg:XOffset>150</pwg:XOffset>") &&
              has("<pwg:YOffset>300</pwg:YOffset>") && has("<pwg:Width>1650</pwg:Width>") &&
              has("<pwg:Height>2550</pwg:Height>"));
        check("escl: settings: platen, gray, 300 x 300",
              has("<pwg:InputSource>Platen</pwg:InputSource>") &&
              has("<scan:ColorMode>Grayscale8</scan:ColorMode>") &&
              has("<scan:XResolution>300</scan:XResolution>") &&
              has("<scan:YResolution>300</scan:YResolution>"));
        check("escl: settings: format both ways",
              has("<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>") &&
              has("<scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>"));
        check("escl: settings: both namespaces declared",
              has("xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\"") &&
              has("xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\""));
        check("escl: mode names", std::string(e::mode_escl(e::Mode::Color)) == "RGB24" &&
              std::string(e::mode_escl(e::Mode::BlackWhite)) == "BlackAndWhite1");

        // URLs.
        const std::string base = "http://192.168.1.40:80/eSCL";
        check("escl: job url, absolute Location",
              e::job_url(base, "http://192.168.1.40:80/eSCL/ScanJobs/7") == "http://192.168.1.40:80/eSCL/ScanJobs/7");
        check("escl: job url, path Location",
              e::job_url(base, "/eSCL/ScanJobs/7") == "http://192.168.1.40:80/eSCL/ScanJobs/7",
              e::job_url(base, "/eSCL/ScanJobs/7"));
        check("escl: job url, relative Location",
              e::job_url(base, "ScanJobs/7") == "http://192.168.1.40:80/eSCL/ScanJobs/7");
        check("escl: job url, trailing slash dropped",
              e::job_url(base, "/eSCL/ScanJobs/7/") == "http://192.168.1.40:80/eSCL/ScanJobs/7");
        check("escl: base url", e::base_url("192.168.1.40", 80, "eSCL", false) == base);
        check("escl: base url, https + slashes in rs",
              e::base_url("10.0.0.2", 443, "/eSCL/", true) == "https://10.0.0.2:443/eSCL");
        check("escl: base url, empty rs -> eSCL", e::base_url("h", 80, "", false) == "http://h:80/eSCL");
        check("escl: base url, IPv6 bracketed",
              e::base_url("fe80::1", 80, "eSCL", false) == "http://[fe80::1]:80/eSCL");
    }

    // ── Corrections (eSCL 2.6 *Support) ───────────────────────────────────
    {
        namespace e = scanit::model::escl;
        std::cout << "\n[corrections]\n";
        std::string caps = kCaps;
        caps.insert(caps.rfind("</scan:ScannerCapabilities>"),
                    "<scan:BrightnessSupport><scan:Min>-100</scan:Min><scan:Max>100</scan:Max>"
                    "<scan:Normal>0</scan:Normal><scan:Step>1</scan:Step></scan:BrightnessSupport>"
                    "<scan:SharpenSupport><scan:Min>0</scan:Min><scan:Max>4</scan:Max>"
                    "<scan:Normal>2</scan:Normal><scan:Step>1</scan:Step></scan:SharpenSupport>");
        e::Caps c;
        check("corr: caps with *Support still parse", e::parse_caps(caps, c));
        check("corr: brightness range read", c.brightness.present && c.brightness.min == -100 &&
              c.brightness.max == 100 && c.brightness.normal == 0);
        check("corr: sharpen range read", c.sharpen.present && c.sharpen.normal == 2 && c.sharpen.max == 4);
        check("corr: contrast not listed -> absent", !c.contrast.present);
        check("corr: 0 -> normal", e::correction_value(c.sharpen, 0) == 2);
        check("corr: +50 -> max, -50 -> min", e::correction_value(c.brightness, 50) == 100 &&
              e::correction_value(c.brightness, -50) == -100);
        check("corr: +25 on an asymmetric range snaps to its step", e::correction_value(c.sharpen, 25) == 3,
              std::to_string(e::correction_value(c.sharpen, 25)));
        check("corr: out of range clamps", e::correction_value(c.brightness, 400) == 100);
        e::Settings st;
        st.brightness = 40;
        const std::string x = e::build_scan_settings(st);
        check("corr: a set correction is sent", x.find("<scan:Brightness>40</scan:Brightness>") != std::string::npos);
        check("corr: an unset one is not", x.find("Contrast") == std::string::npos && x.find("Sharpen") == std::string::npos);

        // The plan sends a correction only when the scanner lists it.
        scanit::model::plan::Book b;
        b.load(c);
        b.set_brightness(25);
        b.set_contrast(25);
        auto ss = b.settings();
        check("corr: plan sends brightness the scanner lists", ss.brightness && *ss.brightness == 50,
              ss.brightness ? std::to_string(*ss.brightness) : "none");
        check("corr: plan drops contrast the scanner does not list", !ss.contrast);
    }

    // ── The plan: quick settings + crop, every verb a step ──────────────────
    {
        namespace e = scanit::model::escl;
        namespace pl = scanit::model::plan;
        std::cout << "\n[plan]\n";
        e::Caps c;
        e::parse_caps(kCaps, c);
        {
            pl::Book b;
            b.load(c);
            check("plan: load -> 300 dpi gray, whole glass", b.plan().dpi == 300 &&
                  b.plan().mode == e::Mode::Gray && b.is_whole_glass());
            check("plan: load is not a step and not a stray", !b.journal().can_undo() && b.strays() == 0);
            check("plan: dpi snaps to the glass's list", b.set_dpi(500) && b.plan().dpi == 600);
            check("plan: the same value again is no step", !b.set_dpi(600));
            check("plan: settings carry the plan", b.settings().dpi == 600 && b.settings().mode == e::Mode::Gray);
            check("plan: summary reads", b.summary().rfind("Gray  ·  600 dpi  ·  the whole glass", 0) == 0, b.summary());
            // Sliders merge while they move: five ticks, one step.
            const auto steps = b.journal().size();
            for (int i = 1; i <= 5; ++i) b.set_brightness(i * 5);
            check("plan: a moving slider is one step", b.journal().size() == steps + 1,
                  std::to_string(b.journal().size() - steps));
            b.journal().undo();
            check("plan: one undo takes the whole slide back", b.plan().brightness == 0);
            // A drag is one step; Escape puts it back.
            const auto kept = b.journal().size();
            b.crop_begin();
            b.crop_move({10, 10, 500, 500});
            b.crop_move({20, 20, 700, 900});
            b.crop_cancel();
            check("plan: a cancelled drag leaves nothing", b.is_whole_glass() && b.journal().size() == kept &&
                  b.strays() == 0);
            b.crop_move({20, 20, 700, 900});
            check("plan: a move with no press writes nothing", b.is_whole_glass() && b.strays() == 0);
            b.set_region({2500, 3500, 400, 400});
            check("plan: a crop is clamped onto the glass", b.plan().region.x + b.plan().region.w <= 2550 &&
                  b.plan().region.y + b.plan().region.h <= 3508);
            check("plan: undo label names the step", b.journal().undo_label().rfind("Crop ", 0) == 0,
                  b.journal().undo_label());
            scanit::model::detect::Suggestion none;
            check("plan: an empty suggestion is not applied", !b.apply(none));
            scanit::model::detect::Suggestion bw;
            bw.found = true; bw.mode = e::Mode::BlackWhite; bw.dpi = 250; bw.region = {0, 0, 900, 900};
            b.apply(bw);
            check("plan: a suggestion snaps dpi and keeps a mode the glass has",
                  b.plan().dpi == 300 && b.plan().mode == e::Mode::BlackWhite);
        }
        // The gate: every verb the UI offers.
        for (const auto& v : pl::verbs()) {
            pl::Book b;
            b.load(c);
            if (v.prep) v.prep(b);
            // The whole state a verb can touch: the plan, and the gate's page.
            auto state = [&] { return std::make_pair(b.plan(), b.page_turns("/tmp/page.png")); };
            const auto before = state();
            const auto steps = b.journal().size();
            const bool made = v.run(b);
            const auto after = state();
            const bool step = made && b.journal().size() == steps + 1 && !(after == before);
            b.journal().undo();
            const bool undone = state() == before;
            b.journal().redo();
            const bool redone = state() == after;
            check("plan gate: " + v.name + " is a step, undoes, redoes, no strays",
                  step && undone && redone && b.strays() == 0,
                  std::string(step ? "" : "no step ") + (undone ? "" : "undo ") + (redone ? "" : "redo ") +
                      (b.strays() ? "STRAY" : ""));
        }
        {
            // The gate itself: a write around the door is caught.
            pl::Store s;
            scanit::model::Journal<pl::Store> j(s);
            s.door = [&] { return j.open(); };
            s.put(pl::Store::kPlan, pl::Record(pl::Plan{}));
            check("plan gate: a write around the door is counted", s.strays == 1);
        }
    }

    // ── Continuous scanning ─────────────────────────────────────────────────
    {
        using scanit::model::Loop;
        std::cout << "\n[loop]\n";
        Loop l;
        check("loop: starts idle", !l.running() && l.phase() == Loop::Phase::Idle);
        check("loop: tick while idle does nothing", !l.tick());
        check("loop: start -> scanning", l.start(3) && l.phase() == Loop::Phase::Scanning);
        check("loop: start twice refused", !l.start(3));
        check("loop: tick while scanning does nothing", !l.tick() && l.phase() == Loop::Phase::Scanning);
        l.scanned();
        check("loop: a page -> waiting, counted, full countdown",
              l.phase() == Loop::Phase::Waiting && l.pages() == 1 && l.left() == 3);
        check("loop: ticks count down without firing", !l.tick() && l.left() == 2 && !l.tick() && l.left() == 1);
        check("loop: the last tick fires the next scan", l.tick() && l.phase() == Loop::Phase::Scanning);
        l.scanned();
        check("loop: skip during the wait scans now", l.skip() && l.phase() == Loop::Phase::Scanning && l.pages() == 2);
        l.scanned();
        check("loop: stop during the wait ends it", l.stop() && !l.running() && !l.tick());
        check("loop: stop when idle says so", !l.stop());
        l.start(5);
        l.failed();
        check("loop: a failed scan ends the run", !l.running());
        l.start(0);
        l.scanned();
        check("loop: pause clamps to at least a second", l.left() == 1);
        check("loop: a fresh start counts from zero", l.pages() == 1);

        namespace e = scanit::model::escl;
        namespace pl = scanit::model::plan;
        e::Caps c;
        e::parse_caps(kCaps, c);
        pl::Book b;
        b.load(c);
        check("loop: plan default is single scans, 8 s", !b.plan().continuous && b.plan().pause_s == 8);
        b.set_pause(500);
        check("loop: pause clamps to the max", b.plan().pause_s == pl::kMaxPause);
        b.set_pause(0);
        check("loop: pause clamps to the min", b.plan().pause_s == pl::kMinPause);
        b.set_continuous(true);
        check("loop: summary says continuous", b.summary().find("continuous, 5 s between pages") != std::string::npos,
              b.summary());
    }

    // ── Rotation ────────────────────────────────────────────────────────────
    {
        namespace e = scanit::model::escl;
        namespace pl = scanit::model::plan;
        std::cout << "\n[rotate]\n";
        e::Caps c;
        e::parse_caps(kCaps, c);
        pl::Book b;
        b.load(c);
        std::vector<std::pair<std::string, int>> calls;
        b.set_on_page([&](const std::string& p, int t) { calls.emplace_back(p, t); });
        check("rotate: default none", b.plan().rotate == 0 && std::string(pl::turns_label(0)) == "none");
        b.set_rotate(-1);
        check("rotate: -1 wraps to 90 left", b.plan().rotate == 3 && b.summary().find("rotate 90 left") != std::string::npos,
              b.summary());
        check("rotate: undo label", b.journal().undo_label() == "Rotate every page: 90 left", b.journal().undo_label());
        check("rotate: a page turned right asks the file to turn", b.turn_page("/p/a.png", +1) &&
              calls.size() == 1 && calls.back() == std::make_pair(std::string("/p/a.png"), 1));
        b.turn_page("/p/a.png", +1);
        check("rotate: turns add up", b.page_turns("/p/a.png") == 2 && calls.back().second == 2);
        b.turn_page("/p/a.png", +1);
        b.turn_page("/p/a.png", +1);
        check("rotate: four turns is back to none", b.page_turns("/p/a.png") == 0 && calls.back().second == 0);
        b.journal().undo();
        check("rotate: undo turns the file back", b.page_turns("/p/a.png") == 3 && calls.back().second == 3);
        b.journal().redo();
        check("rotate: redo turns it again", b.page_turns("/p/a.png") == 0 && calls.back().second == 0);
        check("rotate: another page has its own turns", b.page_turns("/p/b.png") == 0);
        check("rotate: no empty path, no zero turn", !b.turn_page("", 1) && !b.turn_page("/p/a.png", 0));
        check("rotate: no strays", b.strays() == 0);
    }

    // ── Detect: reading the preview ─────────────────────────────────────────
    {
        namespace d = scanit::model::detect;
        namespace e = scanit::model::escl;
        std::cout << "\n[detect]\n";
        const int bw = 2550, bh = 3508;   // 8.5 x 11.69 in
        {
            Glass g(8.5, 11.69, 75, 245);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: an empty glass is empty", !s.found, s.why);
            check("detect: headline says so", d::headline(s) == "The glass looks empty");
        }
        {
            // A 6 x 9 page face down at the corner, its text block inside.
            Glass g(8.5, 11.69, 75, 245);
            g.rect(0.0, 0.0, 6.0, 9.0, 235, 235, 232);   // the paper (barely off the lid)
            g.text(0.8, 0.9, 4.4, 7.2);
            g.rect(0.05, 0.05, 6.0, 0.04, 120, 120, 120); // the page edge's shadow, top
            g.rect(6.0, 0.05, 0.04, 9.0, 120, 120, 120);  // and right
            g.rect(0.05, 9.0, 6.0, 0.04, 120, 120, 120);  // and bottom
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: a page is found", s.found && s.kind == d::Kind::Page, d::kind_label(s.kind));
            check("detect: one page, portrait", s.pages == 1 && !s.landscape);
            check("detect: the page's size (from its edge)", std::abs(s.content.w / 300.0 - 6.0) < 0.3 &&
                  std::abs(s.content.h / 300.0 - 9.0) < 0.3, e::inches_label(s.content.w, s.content.h));
            check("detect: named trade 6 x 9", s.size_name == "trade 6 x 9", s.size_name);
            check("detect: text -> gray or b&w, 300 dpi", s.mode != e::Mode::Color && s.dpi == 300,
                  std::string(e::mode_label(s.mode)) + " " + s.why);
            check("detect: the crop holds the content with a margin",
                  s.region.x <= s.content.x && s.region.y <= s.content.y &&
                  s.region.x + s.region.w >= s.content.x + s.content.w && s.region.w <= bw);
        }
        {
            // An open book face down, rotated: 11 x 8.5 spread across the glass,
            // dark-red boards, two pages, a spine shadow down the middle.
            Glass g(8.5, 11.69, 75, 250);
            // the glass is portrait; the spread lies along its long side
            g.rect(0.1, 0.3, 8.2, 11.2, 110, 40, 40);     // boards
            g.rect(0.3, 0.5, 7.8, 5.3, 240, 238, 230);    // page 1
            g.rect(0.3, 5.9, 7.8, 5.4, 240, 238, 230);    // page 2
            g.rect(0.3, 5.6, 7.8, 0.5, 90, 85, 80);       // the spine shadow
            g.text(0.9, 1.0, 6.6, 4.4);
            g.text(0.9, 6.4, 6.6, 4.4);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: a book is found", s.found && s.kind == d::Kind::Book, d::kind_label(s.kind));
            check("detect: 2 pages up", s.pages == 2);
            check("detect: box is the boards", std::abs(s.content.w / 300.0 - 8.2) < 0.3 &&
                  std::abs(s.content.h / 300.0 - 11.2) < 0.3, e::inches_label(s.content.w, s.content.h));
            check("detect: boards do not make it colour", s.mode != e::Mode::Color,
                  std::to_string(s.color_fraction));
            check("detect: headline", d::headline(s).rfind("Book found  ·  2 pages up  ·  landscape, turned on the glass", 0) == 0,
                  d::headline(s));
        }
        {
            // Scott's example: a book open face down, 12 x 9, spine across --
            // on a (hypothetical) glass big enough to hold it.
            Glass g(12.6, 9.6, 75, 248);
            g.rect(0.3, 0.3, 12.0, 9.0, 236, 234, 226);   // two 6 x 9 pages
            g.rect(6.05, 0.3, 0.35, 9.0, 120, 115, 108);  // the spine shadow
            g.rect(0.3, 0.3, 12.0, 0.04, 110, 110, 110);  // page edges
            g.rect(0.3, 9.26, 12.0, 0.04, 110, 110, 110);
            g.rect(0.3, 0.3, 0.04, 9.0, 110, 110, 110);
            g.rect(12.26, 0.3, 0.04, 9.0, 110, 110, 110);
            g.text(0.9, 1.0, 4.6, 7.4);
            g.text(6.9, 1.0, 4.6, 7.4);
            auto s = d::detect(g.raster(), int(12.6 * 300), int(9.6 * 300));
            check("detect: 'book found, 2 pages up, landscape, 12 x 9'",
                  d::headline(s).rfind("Book found  ·  2 pages up  ·  landscape  ·  12.0", 0) == 0 &&
                  std::abs(s.content.h / 300.0 - 9.0) < 0.15, d::headline(s));
            check("detect: each page a trade 6 x 9", s.size_name == "trade 6 x 9", s.size_name);
            std::cout << "        " << d::advice(s) << "\n";
        }
        {
            // Two loose pages side by side (landscape box), a blank gutter, no shadow.
            Glass g(8.5, 11.69, 75, 245);
            g.text(0.4, 0.5, 3.4, 4.5);
            g.text(4.6, 0.5, 3.4, 4.5);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: two pages by their gutter", s.found && s.kind == d::Kind::TwoPages && s.pages == 2,
                  d::kind_label(s.kind));
            check("detect: landscape", s.landscape, e::inches_label(s.content.w, s.content.h));
        }
        {
            // A colour photo, 4 x 6.
            Glass g(8.5, 11.69, 75, 245);
            g.rect(1.0, 1.0, 6.0, 4.0, 40, 110, 200);
            g.rect(1.0, 3.0, 6.0, 2.0, 60, 160, 60);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: a photo is a photo", s.found && s.kind == d::Kind::Photo, d::kind_label(s.kind));
            check("detect: colour", s.mode == e::Mode::Color, std::to_string(s.color_fraction));
            check("detect: a small photo gets 600 dpi", s.dpi == 600);
            check("detect: landscape 6 x 4, named", s.landscape && s.size_name == "4 x 6 photo", s.size_name);
        }
        {
            // A speck of dust far from the page does not stretch the box.
            Glass g(8.5, 11.69, 75, 245);
            g.text(0.5, 0.5, 4.0, 6.0);
            g.rect(8.0, 11.2, 0.08, 0.08, 20, 20, 20);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: dust ignored", s.found && s.content.x + s.content.w < 5 * 300,
                  e::inches_label(s.content.w, s.content.h));
        }
        {
            // Lid up: a black background, a white page on it.
            Glass g(8.5, 11.69, 75, 15);
            g.rect(1.0, 1.0, 5.5, 8.5, 240, 240, 240);
            auto s = d::detect(g.raster(), bw, bh);
            check("detect: a page on a black background", s.found &&
                  std::abs(s.content.w / 300.0 - 5.5) < 0.25 && std::abs(s.content.h / 300.0 - 8.5) < 0.25,
                  e::inches_label(s.content.w, s.content.h));
        }
        check("detect: nearest size, either way round", d::nearest_size(9.0, 6.0) == "trade 6 x 9" &&
              d::nearest_size(3.0, 3.0).empty());
        check("detect: no raster, no crash", !d::detect(d::Raster{}, bw, bh).found);
    }

    // ── Output: names and paths ─────────────────────────────────────────────
    {
        namespace o = scanit::model::output;
        std::cout << "\n[output]\n";
        std::tm t{};
        t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 25;
        t.tm_hour = 16; t.tm_min = 3; t.tm_sec = 9;
        check("output: scan name", o::scan_name(t, "jpg") == "scan-20261125-160309.jpg", o::scan_name(t, "jpg"));
        std::set<std::string> taken = {"/d/a.jpg", "/d/a-2.jpg"};
        auto exists = [&](const std::string& p) { return taken.count(p) > 0; };
        check("output: free name kept", o::unique_path("/d", "b.jpg", exists) == "/d/b.jpg");
        check("output: taken name numbered past every collision",
              o::unique_path("/d/", "a.jpg", exists) == "/d/a-3.jpg", o::unique_path("/d/", "a.jpg", exists));
        check("output: human bytes", o::human_bytes(512) == "512 B" && o::human_bytes(2048) == "2 KB" &&
              o::human_bytes(1572864) == "1.5 MB");
    }

    // ── Numbered names ──────────────────────────────────────────────────────
    {
        namespace o = scanit::model::output;
        namespace pl = scanit::model::plan;
        std::cout << "\n[naming]\n";
        check("naming: name_0001.jpg", o::numbered_name("name", 4, 1, "jpg") == "name_0001.jpg");
        check("naming: 3 digits", o::numbered_name("name", 3, 7, "png") == "name_007.png");
        check("naming: past the width, written in full", o::numbered_name("b", 3, 1000, "png") == "b_1000.png");
        check("naming: base cleaned", o::clean_base("  ../War/Peace\t ") == "WarPeace", o::clean_base("  ../War/Peace\t "));
        check("naming: spaces inside kept", o::numbered_name("War and Peace", 4, 2, "tif") == "War and Peace_0002.tif");
        check("naming: empty folder starts at 1", o::next_number("name", {}) == 1);
        std::vector<std::string> f = {"name_0001.png", "name_0002.png", "name_0010.jpg", "other_0099.png",
                                      "name_0003x.png", "name_.png", "name_005.png", "scan-20261125-1.png",
                                      "name_0004-2.png"};
        check("naming: next after the highest, any width", o::next_number("name", f) == 11,
              std::to_string(o::next_number("name", f)));
        check("naming: another base is not counted", o::next_number("other", f) == 100);
        check("naming: a prefix base is not fooled", o::next_number("nam", f) == 1);
        scanit::model::escl::Caps c;
        scanit::model::escl::parse_caps(kCaps, c);
        pl::Book b;
        b.load(c);
        check("naming: default is the date-and-time name, 4 digits", b.plan().base.empty() && b.plan().digits == 4);
        auto t = std::chrono::steady_clock::now();
        (void)t;
        const auto steps = b.journal().size();
        for (const char* typed : {"B", "Bo", "Boo", "Book"}) b.set_base(typed);
        check("naming: typing the base is one step", b.journal().size() == steps + 1 && b.plan().base == "Book");
        b.set_digits(2);
        check("naming: width snaps to 3", b.plan().digits == 3);
        b.journal().undo();
        b.journal().undo();
        check("naming: undo takes the base back", b.plan().base.empty() && b.plan().digits == 4);
    }

    // ── Shortcuts ───────────────────────────────────────────────────────────
    std::cout << "\n[shortcuts]\n";
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = scanit::model;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click the orb";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click the orb");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }

    // ── The look (from Cairn s002) ────────────────────────────────────────────────
    {
        namespace look = scanit::model::look;
        using look::State;
        std::cout << "\n[look]\n";
        // Page states: the legal moves, and every illegal one refused.
        {
            look::PageStates p;
            check("look: starts at start", p.state() == State::Start);
            check("look: finish from start refused", !p.finish() && p.state() == State::Start);
            check("look: stop from start refused", !p.stop() && p.state() == State::Start);
            check("look: reset from start refused", !p.reset() && p.state() == State::Start);
            check("look: begin -> running", p.begin() && p.state() == State::Running);
            check("look: begin twice refused (one run at a time)", !p.begin());
            check("look: reset while running refused", !p.reset() && p.state() == State::Running);
            check("look: stop -> start", p.stop() && p.state() == State::Start);
            p.begin();
            check("look: finish -> results", p.finish() && p.state() == State::Results);
            check("look: begin from results refused", !p.begin() && p.state() == State::Results);
            check("look: reset -> start", p.reset() && p.state() == State::Start);
        }
        check("look: state names are the stack children",
              std::string(look::name(State::Start)) == "start" &&
              std::string(look::name(State::Running)) == "running" &&
              std::string(look::name(State::Results)) == "results");
        // The sidebar as data.
        {
            auto probs = look::lint(look::scanit_nav());
            check("look: scanit_nav lints clean", probs.empty(),
                  probs.empty() ? "" : probs.front());
            auto bad = look::scanit_nav();
            bad.push_back({false, "scan", "x", "Again", "magenta"});
            check("look: lint catches a duplicate id", !look::lint(bad).empty());
            bad = look::scanit_nav();
            bad.push_back({false, "extra", "x", "Extra", "chartreuse"});
            check("look: lint catches an unknown tint", !look::lint(bad).empty());
            bad = look::scanit_nav();
            bad.push_back({false, "", "x", "No id", "teal"});
            check("look: lint catches an item with no id", !look::lint(bad).empty());
            check("look: lint catches a sidebar with no items",
                  !look::lint({{true, "", "", "ONLY A HEADER", ""}}).empty());
        }
        check("look: twelve tints", look::tints().size() == 12);
        // The stylesheet agrees with the list: every tint has its window rule,
        // and nothing extra.
        {
            std::ifstream in(std::string(SCANIT_SOURCE_DIR) + "/resources/look.css");
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string css = ss.str();
            check("look: look.css readable", !css.empty());
            auto defined = look::css_tints(css);
            bool all = true;
            std::string missing;
            for (const auto& t : look::tints())
                if (std::find(defined.begin(), defined.end(), t) == defined.end()) { all = false; missing = t; }
            check("look: every tint has a window rule in look.css", all, missing);
            bool extra_ok = true;
            std::string extra;
            for (const auto& t : defined) if (!look::is_tint(t)) { extra_ok = false; extra = t; }
            check("look: look.css defines no tint the list lacks", extra_ok, extra);
            check("look: every tint also styles the orb and the act button", [&] {
                for (const auto& t : look::tints()) {
                    if (css.find("window.look.tint-" + t + " button.orb") == std::string::npos) return false;
                    if (css.find("window.look.tint-" + t + " button.act") == std::string::npos) return false;
                }
                return true;
            }());
            check("look: no double hyphen inside a CSS comment",
                  !look::css_comment_has_double_hyphen(css));
            check("look: the comment check itself catches one",
                  look::css_comment_has_double_hyphen("a {} /* x -- y */"));
        }
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
