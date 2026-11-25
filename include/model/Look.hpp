#pragma once
// model/Look.hpp -- the GTK-free half of the look stone (s002).
//
// Two things a page in the look needs that are pure data, so the selftest
// guards them headless:
//
//   * PageStates -- every module page is one stack with three children:
//       start    the hero, one line of promise, the orb
//       running  a big spinner, a number, a caption, Stop
//       results  cards / a list, an act bar, "Again"
//     The verbs ask PageStates first and repaint only when it said yes, so a
//     double click on the orb cannot start two runs.
//
//   * The sidebar as data -- sections and items (id, icon, title, tint). The
//     Shell builds the rows from it; lint() catches a duplicate id, an unknown
//     tint, an item with no id, before anyone sees a broken sidebar.
//
// The tint names are the CSS classes in resources/look.css (.tint-<name>).
// docs/the-look.md.

#include <string>
#include <string_view>
#include <vector>

namespace scanit::model::look {

enum class State { Start, Running, Results };

// The stack child name for a state: "start" / "running" / "results".
const char* name(State s);

class PageStates {
public:
    State state() const { return m_state; }

    // Each returns true when the state changed (repaint), false when the verb
    // made no sense from here (ignore it).
    bool begin();    // start -> running
    bool finish();   // running -> results
    bool stop();     // running -> start   (cancelled; nothing to show)
    bool reset();    // results -> start   ("Again")

private:
    State m_state = State::Start;
};

// The twelve tints, by hue. Rouster's module colours, renamed.
const std::vector<std::string>& tints();
bool is_tint(std::string_view t);

struct NavEntry {
    bool        section = false;   // a header row: title only, not selectable
    std::string id;                // the stack child name (items)
    std::string icon;              // icon name (items)
    std::string title;
    std::string tint;              // a name from tints() (items)
};

// Scanit's sidebar: Scan, then the Scanner (device) page.
std::vector<NavEntry> scanit_nav();

// Problems, one line each; empty = clean.
std::vector<std::string> lint(const std::vector<NavEntry>& nav);

// The tint names a stylesheet defines a window gradient for
// (`window.look.tint-<name> {`). The selftest compares this to tints().
std::vector<std::string> css_tints(std::string_view css);

// True when a CSS comment contains a double hyphen (GTK's parser has been
// unkind to them; the house rule is none).
bool css_comment_has_double_hyphen(std::string_view css);

}  // namespace scanit::model::look
