// scanit_selftest -- the headless model harness.
//
// Everything with no GTK and no network in it is walked here: the eSCL
// documents (capabilities in, scan settings out, job URLs), the output names,
// the look's page states and sidebar, the shortcut registry.
//
//   ./build/scanit_selftest

#include "model/Escl.hpp"
#include "model/Look.hpp"
#include "model/Output.hpp"
#include "model/Shortcuts.hpp"

#include <algorithm>
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
