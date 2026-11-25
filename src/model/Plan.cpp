#include "model/Plan.hpp"
#include "model/Output.hpp"

#include <algorithm>
#include <cstdio>

namespace scanit::model::plan {

namespace {

std::string sign(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }

std::string cap_first(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}
}  // namespace

Book::Book() : m_journal(m_store) {
    m_store.door = [this] { return m_journal.open(); };
}

void Book::load(const escl::Caps& caps) {
    m_caps = caps;
    Plan p;
    const auto& bed = caps.platen;
    p.dpi = escl::nearest_resolution(bed, 300);
    if (p.dpi == 0) p.dpi = 300;
    p.mode = escl::supports(bed, escl::Mode::Gray) || bed.modes.empty() ? escl::Mode::Gray
                                                                       : escl::Mode::Color;
    p.region = escl::full_bed(bed);
    // Loading is not a user choice: it goes around the door on purpose, and
    // says so by not counting.
    auto door = std::move(m_store.door);
    m_store.door = nullptr;
    m_store.put(Store::kPlan, p);
    m_store.door = std::move(door);
    m_journal.clear();
}

template <class Fn>
bool Book::write(const std::string& label, Fn&& fn, const std::string& merge) {
    return m_journal.run(label, {Store::kPlan}, [&] {
        Plan p = m_store.plan;
        fn(p);
        m_store.put(Store::kPlan, p);
    }, merge);
}

bool Book::set_mode(escl::Mode m) {
    return write(std::string("Type: ") + escl::mode_label(m), [&](Plan& p) { p.mode = m; });
}

bool Book::set_dpi(int dpi) {
    int snapped = escl::nearest_resolution(m_caps.platen, dpi);
    if (snapped == 0) snapped = dpi;
    return write("Resolution " + std::to_string(snapped) + " dpi", [&](Plan& p) { p.dpi = snapped; });
}

bool Book::set_brightness(int rel) {
    rel = std::clamp(rel, -50, 50);
    return write("Brightness " + sign(rel), [&](Plan& p) { p.brightness = rel; }, "brightness");
}

bool Book::set_contrast(int rel) {
    rel = std::clamp(rel, -50, 50);
    return write("Contrast " + sign(rel), [&](Plan& p) { p.contrast = rel; }, "contrast");
}

bool Book::set_sharpen(int rel) {
    rel = std::clamp(rel, -50, 50);
    return write("Sharpen " + sign(rel), [&](Plan& p) { p.sharpen = rel; }, "sharpen");
}

bool Book::set_continuous(bool on) {
    return write(on ? "Continuous scanning on" : "Continuous scanning off",
                 [&](Plan& p) { p.continuous = on; });
}

bool Book::set_pause(int seconds) {
    seconds = std::clamp(seconds, kMinPause, kMaxPause);
    return write("Pause " + std::to_string(seconds) + " s", [&](Plan& p) { p.pause_s = seconds; }, "pause");
}

const char* turns_label(int turns) {
    switch (((turns % 4) + 4) % 4) {
        case 1: return "90 right";
        case 2: return "180";
        case 3: return "90 left";
    }
    return "none";
}

bool Book::set_rotate(int turns) {
    turns = ((turns % 4) + 4) % 4;
    return write(std::string("Rotate every page: ") + turns_label(turns), [&](Plan& p) { p.rotate = turns; });
}

bool Book::set_base(const std::string& base) {
    const std::string b = output::clean_base(base);
    return write(b.empty() ? std::string("File name: date and time") : "File name \u201c" + b + "\u201d",
                 [&](Plan& p) { p.base = b; }, "base");
}

bool Book::set_digits(int digits) {
    digits = digits <= 3 ? 3 : 4;
    return write(digits == 3 ? "Numbering 001" : "Numbering 0001", [&](Plan& p) { p.digits = digits; });
}

int Book::page_turns(const std::string& path) const {
    return m_store.get(Store::page_id(path))->turns;
}

bool Book::turn_page(const std::string& path, int delta) {
    if (path.empty() || delta == 0) return false;
    const auto id = Store::page_id(path);
    const int now = page_turns(path);
    return m_journal.run(delta > 0 ? "Rotate page right" : "Rotate page left", {id}, [&] {
        Record r;
        r.turns = now + delta;
        m_store.put(id, r);
    });
}

bool Book::whole_glass() {
    return write("Whole glass", [&](Plan& p) { p.region = escl::full_bed(m_caps.platen); });
}

bool Book::set_region(const escl::Region& r) {
    const auto c = escl::clamp(m_caps.platen, r);
    return write("Crop " + escl::inches_label(c.w, c.h), [&](Plan& p) { p.region = c; });
}

bool Book::apply(const detect::Suggestion& s) {
    if (!s.found) return false;
    const auto region = escl::clamp(m_caps.platen, s.region);
    escl::Mode mode = s.mode;
    if (!m_caps.platen.modes.empty() && !escl::supports(m_caps.platen, mode))
        mode = escl::supports(m_caps.platen, escl::Mode::Gray) ? escl::Mode::Gray : escl::Mode::Color;
    int dpi = escl::nearest_resolution(m_caps.platen, s.dpi);
    if (dpi == 0) dpi = s.dpi;
    return write("Use the preview's suggestion", [&](Plan& p) {
        p.mode = mode;
        p.dpi = dpi;
        p.region = region;
    });
}

void Book::crop_begin() { m_journal.begin("Crop", {Store::kPlan}); }

void Book::crop_move(const escl::Region& r) {
    if (!m_journal.in_gesture()) return;   // a move with no press is not a write
    Plan p = m_store.plan;
    p.region = escl::clamp(m_caps.platen, r);
    m_store.put(Store::kPlan, p);
}

bool Book::crop_commit() { return m_journal.commit(); }
void Book::crop_cancel() { m_journal.cancel(); }

bool Book::is_whole_glass() const {
    const auto b = escl::full_bed(m_caps.platen);
    const auto& r = m_store.plan.region;
    return r.x == b.x && r.y == b.y && r.w == b.w && r.h == b.h;
}

escl::Settings Book::settings() const {
    const Plan& p = m_store.plan;
    escl::Settings s;
    s.version = m_caps.version.empty() ? "2.0" : m_caps.version;
    s.mode    = p.mode;
    s.dpi     = p.dpi;
    s.region  = p.region.w > 0 && p.region.h > 0 ? p.region : escl::full_bed(m_caps.platen);
    s.format  = escl::choose_format(m_caps.platen, {"image/png", "image/tiff", "image/jpeg"});
    if (p.brightness != 0 && m_caps.brightness.present)
        s.brightness = escl::correction_value(m_caps.brightness, p.brightness);
    if (p.contrast != 0 && m_caps.contrast.present)
        s.contrast = escl::correction_value(m_caps.contrast, p.contrast);
    if (p.sharpen != 0 && m_caps.sharpen.present)
        s.sharpen = escl::correction_value(m_caps.sharpen, p.sharpen);
    return s;
}

std::string Book::summary() const {
    const Plan& p = m_store.plan;
    std::string s = cap_first(escl::mode_label(p.mode)) + "  ·  " + std::to_string(p.dpi) + " dpi  ·  ";
    s += is_whole_glass() ? "the whole glass (" + escl::inches_label(p.region.w, p.region.h) + ")"
                          : escl::inches_label(p.region.w, p.region.h);
    std::string corr;
    auto add = [&](const char* n, int v) {
        if (v) corr += (corr.empty() ? "" : ", ") + std::string(n) + " " + sign(v);
    };
    add("brightness", p.brightness);
    add("contrast", p.contrast);
    add("sharpen", p.sharpen);
    if (!corr.empty()) s += "  ·  " + corr;
    if (p.rotate) s += std::string("  ·  rotate ") + turns_label(p.rotate);
    if (p.continuous) s += "  ·  continuous, " + std::to_string(p.pause_s) + " s between pages";
    return s;
}

std::vector<Verb> verbs() {
    return {
        {"set_mode",       [](Book& b) { return b.set_mode(b.plan().mode == escl::Mode::Color ? escl::Mode::Gray : escl::Mode::Color); }},
        {"set_dpi",        [](Book& b) { return b.set_dpi(b.plan().dpi == 600 ? 300 : 600); }},
        {"set_brightness", [](Book& b) { return b.set_brightness(b.plan().brightness + 10); }},
        {"set_contrast",   [](Book& b) { return b.set_contrast(b.plan().contrast - 10); }},
        {"set_sharpen",    [](Book& b) { return b.set_sharpen(b.plan().sharpen + 20); }},
        {"set_rotate",     [](Book& b) { return b.set_rotate(b.plan().rotate + 1); }},
        {"turn_page",      [](Book& b) { return b.turn_page("/tmp/page.png", -1); }},
        {"set_base",       [](Book& b) { return b.set_base(b.plan().base + "x"); }},
        {"set_digits",     [](Book& b) { return b.set_digits(b.plan().digits == 4 ? 3 : 4); }},
        {"set_continuous", [](Book& b) { return b.set_continuous(!b.plan().continuous); }},
        {"set_pause",      [](Book& b) { return b.set_pause(b.plan().pause_s + 3); }},
        {"set_region",     [](Book& b) { return b.set_region({300, 300, 1200, 1500}); }},
        {"whole_glass",    [](Book& b) { return b.whole_glass(); },
                           [](Book& b) { b.set_region({30, 30, 600, 600}); }},
        {"crop_drag",      [](Book& b) { b.crop_begin(); b.crop_move({100, 100, 900, 900});
                                         b.crop_move({120, 140, 1000, 1100}); return b.crop_commit(); }},
        {"apply",          [](Book& b) {
             detect::Suggestion s;
             s.found = true;
             s.mode = escl::Mode::Color;
             s.dpi = 600;
             s.region = {60, 90, 1800, 2700};
             return b.apply(s);
         }},
    };
}

}  // namespace scanit::model::plan
