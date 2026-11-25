#include "model/Look.hpp"

#include <algorithm>
#include <cctype>
#include <set>

// model/Look.cpp -- PageStates, the tint list, the sidebar as data. No GTK.

namespace scanit::model::look {

const char* name(State s) {
    switch (s) {
        case State::Start:   return "start";
        case State::Running: return "running";
        case State::Results: return "results";
    }
    return "start";
}

bool PageStates::begin() {
    if (m_state != State::Start) return false;
    m_state = State::Running;
    return true;
}
bool PageStates::finish() {
    if (m_state != State::Running) return false;
    m_state = State::Results;
    return true;
}
bool PageStates::stop() {
    if (m_state != State::Running) return false;
    m_state = State::Start;
    return true;
}
bool PageStates::reset() {
    if (m_state != State::Results) return false;
    m_state = State::Start;
    return true;
}

const std::vector<std::string>& tints() {
    static const std::vector<std::string> t = {
        "magenta", "green", "orange", "sky",    "rose",  "amber",
        "violet",  "lime",  "teal",   "indigo", "slate", "emerald",
    };
    return t;
}

bool is_tint(std::string_view t) {
    const auto& all = tints();
    return std::find(all.begin(), all.end(), t) != all.end();
}

std::vector<NavEntry> scanit_nav() {
    return {
        {true,  "",        "",                         "SCAN",    ""},
        {false, "scan",    "image-x-generic-symbolic", "Scan",    "sky"},
        {true,  "",        "",                         "DEVICE",  ""},
        {false, "scanner", "scanner-symbolic",         "Scanner", "slate"},
    };
}

std::vector<std::string> lint(const std::vector<NavEntry>& nav) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    int items = 0;
    for (const auto& e : nav) {
        if (e.section) {
            if (e.title.empty()) out.push_back("a section with no title");
            continue;
        }
        ++items;
        if (e.id.empty()) { out.push_back("an item with no id (\"" + e.title + "\")"); continue; }
        if (!seen.insert(e.id).second) out.push_back("duplicate id \"" + e.id + "\"");
        if (!is_tint(e.tint)) out.push_back("\"" + e.id + "\": unknown tint \"" + e.tint + "\"");
        if (e.title.empty()) out.push_back("\"" + e.id + "\": no title");
    }
    if (items == 0) out.push_back("no items");
    return out;
}

std::vector<std::string> css_tints(std::string_view css) {
    std::vector<std::string> out;
    const std::string_view key = "window.look.tint-";
    std::size_t at = 0;
    while ((at = css.find(key, at)) != std::string_view::npos) {
        std::size_t b = at + key.size();
        std::size_t e = b;
        while (e < css.size() && (std::isalnum(static_cast<unsigned char>(css[e])) || css[e] == '_')) ++e;
        // Only the window rule itself: the next non-blank is '{'.
        std::size_t k = e;
        while (k < css.size() && css[k] == ' ') ++k;
        if (k < css.size() && css[k] == '{') {
            std::string t(css.substr(b, e - b));
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        }
        at = e;
    }
    return out;
}

bool css_comment_has_double_hyphen(std::string_view css) {
    std::size_t at = 0;
    while ((at = css.find("/*", at)) != std::string_view::npos) {
        std::size_t end = css.find("*/", at + 2);
        if (end == std::string_view::npos) end = css.size();
        if (css.substr(at, end - at).find("--") != std::string_view::npos) return true;
        at = end;
    }
    return false;
}

}  // namespace scanit::model::look
