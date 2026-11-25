#include "model/Escl.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// model/Escl.cpp -- eSCL as pure functions. The XML handling is deliberately
// small: eSCL documents are flat, namespaced (scan:, pwg:), and we only ever
// need "the text of element X inside section Y". Prefixes are ignored, so
// <scan:MaxWidth> and <MaxWidth> both match "MaxWidth".

namespace scanit::model::escl {

namespace {

// Is `xml[at]` the start of an open tag whose local name is `local`?
// Returns the index just past '>' or npos.
std::size_t open_tag_end(std::string_view xml, std::size_t at, std::string_view local) {
    if (at >= xml.size() || xml[at] != '<') return std::string_view::npos;
    std::size_t p = at + 1;
    if (p < xml.size() && (xml[p] == '/' || xml[p] == '?' || xml[p] == '!'))
        return std::string_view::npos;
    std::size_t name_end = p;
    while (name_end < xml.size() && xml[name_end] != '>' && xml[name_end] != ' ' &&
           xml[name_end] != '/' && xml[name_end] != '\t' && xml[name_end] != '\n' &&
           xml[name_end] != '\r')
        ++name_end;
    std::string_view qname = xml.substr(p, name_end - p);
    std::size_t colon = qname.find(':');
    std::string_view ln = colon == std::string_view::npos ? qname : qname.substr(colon + 1);
    if (ln != local) return std::string_view::npos;
    std::size_t gt = xml.find('>', name_end);
    if (gt == std::string_view::npos) return std::string_view::npos;
    if (xml[gt - 1] == '/') return std::string_view::npos;   // <x/> has no body
    return gt + 1;
}

// The body of the first element named `local` (any prefix) at or after `from`,
// matching nesting of same-named elements. `*next` gets the index after its
// close tag. Empty view + npos when there is none.
std::string_view element(std::string_view xml, std::string_view local,
                         std::size_t from = 0, std::size_t* next = nullptr) {
    for (std::size_t at = xml.find('<', from); at != std::string_view::npos;
         at = xml.find('<', at + 1)) {
        std::size_t body = open_tag_end(xml, at, local);
        if (body == std::string_view::npos) continue;
        int depth = 1;
        std::size_t scan = body;
        while (scan < xml.size()) {
            std::size_t lt = xml.find('<', scan);
            if (lt == std::string_view::npos) break;
            if (lt + 1 < xml.size() && xml[lt + 1] == '/') {
                std::size_t gt = xml.find('>', lt);
                if (gt == std::string_view::npos) break;
                std::string_view q = xml.substr(lt + 2, gt - lt - 2);
                std::size_t c = q.find(':');
                std::string_view ln = c == std::string_view::npos ? q : q.substr(c + 1);
                while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
                if (ln == local && --depth == 0) {
                    if (next) *next = gt + 1;
                    return xml.substr(body, lt - body);
                }
                scan = gt + 1;
            } else {
                if (open_tag_end(xml, lt, local) != std::string_view::npos) ++depth;
                scan = lt + 1;
            }
        }
        break;
    }
    if (next) *next = std::string_view::npos;
    return {};
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\n' || s[b] == '\r' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == '\t')) --e;
    return std::string(s.substr(b, e - b));
}

std::string text(std::string_view xml, std::string_view local) {
    return trim(element(xml, local));
}

int number(std::string_view xml, std::string_view local) {
    std::string t = text(xml, local);
    return t.empty() ? 0 : std::atoi(t.c_str());
}

// Every element named `local` in `xml`, trimmed text.
std::vector<std::string> all_text(std::string_view xml, std::string_view local) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at != std::string_view::npos && at < xml.size()) {
        std::size_t next = std::string_view::npos;
        std::string_view body = element(xml, local, at, &next);
        if (next == std::string_view::npos) break;
        std::string t = trim(body);
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        at = next;
    }
    return out;
}

Source parse_source(std::string_view section) {
    Source s;
    if (section.empty()) return s;
    s.present = true;
    s.min_w = number(section, "MinWidth");
    s.min_h = number(section, "MinHeight");
    s.max_w = number(section, "MaxWidth");
    s.max_h = number(section, "MaxHeight");
    for (const auto& r : all_text(section, "XResolution")) {
        int v = std::atoi(r.c_str());
        if (v > 0 && std::find(s.resolutions.begin(), s.resolutions.end(), v) == s.resolutions.end())
            s.resolutions.push_back(v);
    }
    std::sort(s.resolutions.begin(), s.resolutions.end());
    s.modes = all_text(section, "ColorMode");
    s.formats = all_text(section, "DocumentFormatExt");
    for (const auto& f : all_text(section, "DocumentFormat"))
        if (std::find(s.formats.begin(), s.formats.end(), f) == s.formats.end()) s.formats.push_back(f);
    return s;
}

}  // namespace

const char* mode_escl(Mode m) {
    switch (m) {
        case Mode::Gray:       return "Grayscale8";
        case Mode::Color:      return "RGB24";
        case Mode::BlackWhite: return "BlackAndWhite1";
    }
    return "Grayscale8";
}

const char* mode_label(Mode m) {
    switch (m) {
        case Mode::Gray:       return "gray";
        case Mode::Color:      return "color";
        case Mode::BlackWhite: return "black & white";
    }
    return "gray";
}

bool parse_caps(std::string_view xml, Caps& out, std::string* err) {
    out = Caps{};
    if (element(xml, "ScannerCapabilities").empty()) {
        if (err) *err = "not a ScannerCapabilities document";
        return false;
    }
    out.version    = text(xml, "Version");
    out.make_model = text(xml, "MakeAndModel");
    out.uuid       = text(xml, "UUID");
    out.platen = parse_source(element(xml, "PlatenInputCaps"));
    out.adf    = parse_source(element(xml, "AdfSimplexInputCaps"));
    if (!out.platen.present) {
        if (err) *err = "no platen (flatbed) in the capabilities";
        return false;
    }
    if (out.platen.max_w <= 0 || out.platen.max_h <= 0) {
        if (err) *err = "the platen has no size";
        return false;
    }
    return true;
}

std::string parse_state(std::string_view xml) { return text(xml, "State"); }

bool supports(const Source& s, Mode m) {
    return std::find(s.modes.begin(), s.modes.end(), mode_escl(m)) != s.modes.end();
}

int nearest_resolution(const Source& s, int want) {
    int best = 0, best_d = 1 << 30;
    for (int r : s.resolutions) {
        int d = std::abs(r - want);
        if (d < best_d || (d == best_d && r > best)) { best = r; best_d = d; }
    }
    return best;
}

std::string choose_format(const Source& s, const std::vector<std::string>& preferred) {
    for (const auto& p : preferred)
        if (std::find(s.formats.begin(), s.formats.end(), p) != s.formats.end()) return p;
    if (!s.formats.empty()) return s.formats.front();
    return "image/jpeg";
}

std::string extension_for(std::string_view mime) {
    if (mime == "image/png")       return "png";
    if (mime == "image/jpeg")      return "jpg";
    if (mime == "application/pdf") return "pdf";
    if (mime == "image/tiff")      return "tif";
    return "bin";
}

Region full_bed(const Source& s) { return Region{0, 0, s.max_w, s.max_h}; }

Region clamp(const Source& s, Region r) {
    r.x = std::clamp(r.x, 0, std::max(0, s.max_w - s.min_w));
    r.y = std::clamp(r.y, 0, std::max(0, s.max_h - s.min_h));
    r.w = std::clamp(r.w, std::max(1, s.min_w), s.max_w - r.x);
    r.h = std::clamp(r.h, std::max(1, s.min_h), s.max_h - r.y);
    return r;
}

std::string build_scan_settings(const Settings& s) {
    auto n = [](int v) { return std::to_string(v); };
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<scan:ScanSettings xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\" "
         "xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n";
    x += "  <pwg:Version>" + s.version + "</pwg:Version>\n";
    x += "  <pwg:ScanRegions>\n";
    x += "    <pwg:ScanRegion>\n";
    x += "      <pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>\n";
    x += "      <pwg:XOffset>" + n(s.region.x) + "</pwg:XOffset>\n";
    x += "      <pwg:YOffset>" + n(s.region.y) + "</pwg:YOffset>\n";
    x += "      <pwg:Width>" + n(s.region.w) + "</pwg:Width>\n";
    x += "      <pwg:Height>" + n(s.region.h) + "</pwg:Height>\n";
    x += "    </pwg:ScanRegion>\n";
    x += "  </pwg:ScanRegions>\n";
    x += "  <pwg:InputSource>Platen</pwg:InputSource>\n";
    x += "  <pwg:DocumentFormat>" + s.format + "</pwg:DocumentFormat>\n";
    x += "  <scan:DocumentFormatExt>" + s.format + "</scan:DocumentFormatExt>\n";
    x += "  <scan:ColorMode>" + std::string(mode_escl(s.mode)) + "</scan:ColorMode>\n";
    x += "  <scan:XResolution>" + n(s.dpi) + "</scan:XResolution>\n";
    x += "  <scan:YResolution>" + n(s.dpi) + "</scan:YResolution>\n";
    x += "</scan:ScanSettings>\n";
    return x;
}

std::string job_url(std::string_view base, std::string_view location) {
    std::string loc = trim(location);
    while (!loc.empty() && loc.back() == '/') loc.pop_back();
    if (loc.rfind("http://", 0) == 0 || loc.rfind("https://", 0) == 0) return loc;
    std::string b(base);
    while (!b.empty() && b.back() == '/') b.pop_back();
    if (!loc.empty() && loc.front() == '/') {
        std::size_t scheme = b.find("://");
        std::size_t path = scheme == std::string::npos ? std::string::npos : b.find('/', scheme + 3);
        return (path == std::string::npos ? b : b.substr(0, path)) + loc;
    }
    return b + "/" + loc;
}

std::string base_url(std::string_view host, int port, std::string_view rs, bool tls) {
    std::string h(host);
    if (h.find(':') != std::string::npos && h.front() != '[') h = "[" + h + "]";
    std::string r(rs);
    while (!r.empty() && r.front() == '/') r.erase(r.begin());
    while (!r.empty() && r.back() == '/') r.pop_back();
    if (r.empty()) r = "eSCL";
    return std::string(tls ? "https://" : "http://") + h + ":" + std::to_string(port) + "/" + r;
}

int pixels(int units, int dpi) {
    return static_cast<int>((static_cast<long long>(units) * dpi + kUnitsPerInch / 2) / kUnitsPerInch);
}

std::string inches_label(int w_units, int h_units) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f x %.2f in", w_units / double(kUnitsPerInch),
                  h_units / double(kUnitsPerInch));
    return buf;
}

}  // namespace scanit::model::escl
