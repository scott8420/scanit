#pragma once
// model/Plan.hpp -- what the next scan will send, as the user chose it: the
// quick settings (type, resolution, corrections) and the crop box. GTK-free.
//
// ── every choice here is a step ────────────────────────────────────────────
// The plan is the first thing in Scanit a user writes, so the undo stone
// (model/Undo.hpp, docs/undo.md) ships with it. The plan is ONE record in a
// one-record store; every verb below goes through Journal::run (or a gesture
// for the crop drag), so Ctrl+Z puts the whole plan back the way it was. The
// store counts a write made with the door shut, and the selftest walks
// verbs() -- every verb the UI offers -- and fails on a stray.
//
// ── corrections are relative ───────────────────────────────────────────────
// Brightness / contrast / sharpen are held as -50..+50 around the scanner's
// "normal", and mapped onto whatever range the scanner lists only when the
// settings are built (escl::correction_value). 0 sends nothing at all.

#include "model/Detect.hpp"
#include "model/Escl.hpp"
#include "model/Undo.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace scanit::model::plan {

inline constexpr int kMinPause = 5;    // Scott: "5-25 seconds wait is fine"
inline constexpr int kMaxPause = 25;

struct Plan {
    escl::Mode   mode = escl::Mode::Gray;
    int          dpi  = 300;
    escl::Region region;            // 1/300"; the crop box
    int          brightness = 0;    // -50..+50, 0 = the scanner's normal
    int          contrast   = 0;
    int          sharpen    = 0;
    bool         continuous = false;  // scan, wait pause_s, scan again, until Stop
    int          pause_s    = 8;      // seconds to turn the page (kMinPause..kMaxPause)
    int          rotate     = 0;      // quarter turns clockwise for every page as it arrives
    std::string  base;                // file name base ("" = scan-<date>-<time>)
    int          digits     = 4;      // the counter's width: 3 (name_001) or 4 (name_0001)
    bool operator==(const Plan& o) const {
        return mode == o.mode && dpi == o.dpi && region.x == o.region.x && region.y == o.region.y &&
               region.w == o.region.w && region.h == o.region.h && brightness == o.brightness &&
               contrast == o.contrast && sharpen == o.sharpen &&
               continuous == o.continuous && pause_s == o.pause_s && rotate == o.rotate &&
               base == o.base && digits == o.digits;
    }
};

// Quarter turns clockwise: 0 none, 1 = 90 right, 2 = 180, 3 = 90 left.
const char* turns_label(int turns);   // "none" / "90 right" / "180" / "90 left"

// One record as the journal sees it. The plan record uses `plan`; a saved
// page's record ("page:<path>") uses `turns` -- how far that file is turned
// from how it was saved.
struct Record {
    Plan plan;
    int  turns = 0;
    Record() = default;
    Record(const Plan& p) : plan(p) {}   // NOLINT: a plan IS the plan record
    bool operator==(const Record& o) const { return plan == o.plan && turns == o.turns; }
};

// The store the journal writes through: the plan, and every page the user
// turned after it was saved. A page's put() turns the FILE (through on_page,
// which the app supplies; the model never touches disk), so undoing a turn
// turns the file back.
struct Store {
    using Id    = std::string;
    using Value = Record;
    static inline const Id kPlan = "plan";
    static Id page_id(const std::string& path) { return "page:" + path; }

    std::optional<Record> get(const Id& id) const {
        if (id == kPlan) return Record(plan);
        if (id.rfind("page:", 0) == 0) {
            Record r;
            auto it = std::find_if(pages.begin(), pages.end(), [&](const auto& p) { return p.first == id; });
            r.turns = it == pages.end() ? 0 : it->second;
            return r;
        }
        return std::nullopt;
    }
    void put(const Id& id, const std::optional<Record>& v) {
        if (door && !door()) ++strays;   // the gate: a write that went around the door
        if (id == kPlan) { if (v) plan = v->plan; return; }
        if (id.rfind("page:", 0) == 0) {
            const int t = v ? ((v->turns % 4) + 4) % 4 : 0;
            auto it = std::find_if(pages.begin(), pages.end(), [&](const auto& p) { return p.first == id; });
            if (it == pages.end()) pages.emplace_back(id, t); else it->second = t;
            if (on_page) on_page(id.substr(5), t);
        }
    }

    Plan                                      plan;
    std::vector<std::pair<std::string, int>>  pages;   // "page:<path>" -> turns
    int                                       strays = 0;
    std::function<bool()>                     door;     // Journal::open
    std::function<void(const std::string& path, int turns)> on_page;   // turn the file
};

// The plan, its journal and the verbs. One per window.
class Book {
public:
    Book();
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;

    const Plan&         plan()    const { return m_store.plan; }
    const escl::Caps&   caps()    const { return m_caps; }
    Journal<Store>&     journal()       { return m_journal; }
    int                 strays()  const { return m_store.strays; }

    // A scanner was found: fresh defaults for it (300 dpi gray, whole glass),
    // and the history forgotten. NOT a user choice -- a load, by name.
    void load(const escl::Caps& caps);

    // ── the verbs (each one step; the sliders merge while they move) ──────
    bool set_mode(escl::Mode m);
    bool set_dpi(int dpi);                 // snapped to what the glass offers
    bool set_brightness(int rel);
    bool set_contrast(int rel);
    bool set_sharpen(int rel);
    bool set_continuous(bool on);
    bool set_pause(int seconds);           // clamped to kMinPause..kMaxPause
    bool set_rotate(int turns);
    bool set_base(const std::string& base);   // typing folds into one step
    bool set_digits(int digits);              // 3 or 4            // every page from now on (0..3 quarter turns clockwise)

    // A saved page, turned after the fact (delta: +1 right, -1 left). The
    // store calls on_page(path, turns) to do it to the file; undo turns it back.
    bool turn_page(const std::string& path, int delta);
    int  page_turns(const std::string& path) const;
    void set_on_page(std::function<void(const std::string&, int)> f) { m_store.on_page = std::move(f); }
    bool whole_glass();                    // the crop box back to the whole bed
    bool set_region(const escl::Region& r);   // one step (a typed / nudged crop)
    // The preview's suggestion: type, resolution and crop, as ONE step.
    bool apply(const detect::Suggestion& s);

    // The crop drag: press .. move .. release is ONE step. Escape cancels.
    void crop_begin();
    void crop_move(const escl::Region& r);
    bool crop_commit();
    void crop_cancel();

    // What a scan sends, from the plan and the caps.
    escl::Settings settings() const;

    // "Gray · 300 dpi · 6.00 x 9.00 in" / "... · the whole glass"
    std::string summary() const;
    bool is_whole_glass() const;

private:
    template <class Fn>
    bool write(const std::string& label, Fn&& fn, const std::string& merge = {});

    escl::Caps     m_caps;
    Store          m_store;
    Journal<Store> m_journal;
};

// Every verb the UI offers, for the selftest's gate: each must make a step,
// undo exactly, redo exactly, and never write around the door.
struct Verb {
    std::string                 name;
    std::function<bool(Book&)>  run;
    std::function<void(Book&)>  prep = nullptr;   // puts the book where the verb means something
};
std::vector<Verb> verbs();

}  // namespace scanit::model::plan
