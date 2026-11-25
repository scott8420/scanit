#include "model/Detect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

// model/Detect.cpp -- the heuristics in Detect.hpp's header comment, in that
// order. Every threshold is named here so a field report ("it called my photo
// a page") can be traced to one number.

namespace scanit::model::detect {

namespace {

constexpr int    kDiffGray       = 40;    // |gray - background| that counts as content
constexpr int    kDiffChroma     = 45;    // chroma above the background's that counts
constexpr double kCellOn         = 0.08;  // fraction of a cell's pixels that makes it content
constexpr int    kCloseCells     = 2;     // blobs this many cells apart are one blob
constexpr double kKeepBlob       = 0.10;  // a blob this fraction of the largest is kept
constexpr double kSpineDark      = 35;    // spine shadow: this much darker than the pages
constexpr double kSpineMaxWidth  = 0.15;  // ... and narrower than this much of the long side
constexpr double kGutterRatio    = 0.15;  // blank gutter: ink density under this x the halves'
constexpr double kColorChroma    = 60;    // a pixel this saturated is "colour"
constexpr double kColorFraction  = 0.04;  // of content pixels, to call it colour
constexpr double kPhotoCoverage  = 0.45;  // content covering this much of its box is a picture
constexpr double kBwMidtones     = 0.12;  // fewer mid-tones than this: black & white
constexpr int    kMarginUnits    = 30;    // 0.1" around the content in the suggested crop

struct Img {
    const Raster& r;
    int gray(int x, int y) const {
        const std::uint8_t* p = r.data + static_cast<std::size_t>(y) * r.stride + x * r.channels;
        if (r.channels < 3) return p[0];
        return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
    }
    int chroma(int x, int y) const {
        if (r.channels < 3) return 0;
        const std::uint8_t* p = r.data + static_cast<std::size_t>(y) * r.stride + x * r.channels;
        return std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
    }
};

int median(std::vector<int>& v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

double median_d(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

int to_units(int px, int dpi) {
    return static_cast<int>((static_cast<long long>(px) * escl::kUnitsPerInch + dpi / 2) / dpi);
}

struct Size { double w, h; const char* name; };
const Size kSizes[] = {
    {4.25, 6.87,  "mass-market paperback"},
    {4.0,  6.0,   "4 x 6 photo"},
    {5.0,  7.0,   "5 x 7 photo"},
    {5.0,  8.0,   "5 x 8"},
    {5.5,  8.5,   "digest 5.5 x 8.5"},
    {5.83, 8.27,  "A5"},
    {6.0,  9.0,   "trade 6 x 9"},
    {7.0,  10.0,  "7 x 10"},
    {8.0,  10.0,  "8 x 10 photo"},
    {8.5,  11.0,  "letter"},
    {8.27, 11.69, "A4"},
};

std::string fmt2(double a, double b) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f x %.2f in", a, b);
    return buf;
}

}  // namespace

const char* kind_label(Kind k) {
    switch (k) {
        case Kind::Empty:    return "nothing";
        case Kind::Page:     return "page";
        case Kind::TwoPages: return "two pages";
        case Kind::Book:     return "book";
        case Kind::Photo:    return "photo";
    }
    return "nothing";
}

std::string nearest_size(double w_in, double h_in, double tol) {
    const double a = std::min(w_in, h_in), b = std::max(w_in, h_in);
    const char* best = nullptr;
    double best_err = 1e9;
    for (const auto& s : kSizes) {
        const double ea = std::abs(a - s.w), eb = std::abs(b - s.h);
        if (ea <= tol && eb <= tol && ea + eb < best_err) { best_err = ea + eb; best = s.name; }
    }
    return best ? best : "";
}

Suggestion detect(const Raster& r, int bed_w, int bed_h) {
    Suggestion out;
    if (!r.data || r.width < 8 || r.height < 8 || r.dpi <= 0) {
        out.why = "no preview to read";
        return out;
    }
    const Img im{r};
    const int W = r.width, H = r.height;

    // 1. The background: the median of the outer ring.
    const int ring = std::max(2, r.dpi / 25);
    std::vector<int> ring_g, ring_c;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (x < ring || y < ring || x >= W - ring || y >= H - ring) {
                ring_g.push_back(im.gray(x, y));
                ring_c.push_back(im.chroma(x, y));
            }
    const int bg   = median(ring_g);
    const int bg_c = median(ring_c);

    // 2. Content: the per-pixel mask, then 1/10" cells.
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            mask[static_cast<std::size_t>(y) * W + x] =
                std::abs(im.gray(x, y) - bg) > kDiffGray || im.chroma(x, y) - bg_c > kDiffChroma;

    const int cell = std::max(2, r.dpi / 10);
    const int gw = (W + cell - 1) / cell, gh = (H + cell - 1) / cell;
    std::vector<std::uint8_t> on(static_cast<std::size_t>(gw) * gh);
    for (int cy = 0; cy < gh; ++cy)
        for (int cx = 0; cx < gw; ++cx) {
            int n = 0, hit = 0;
            for (int y = cy * cell; y < std::min(H, (cy + 1) * cell); ++y)
                for (int x = cx * cell; x < std::min(W, (cx + 1) * cell); ++x) {
                    ++n;
                    hit += mask[static_cast<std::size_t>(y) * W + x];
                }
            on[static_cast<std::size_t>(cy) * gw + cx] = n > 0 && hit >= kCellOn * n;
        }

    // Blobs: label the cells grown by kCloseCells, count only real cells.
    std::vector<std::uint8_t> grown(on.size());
    for (int cy = 0; cy < gh; ++cy)
        for (int cx = 0; cx < gw; ++cx) {
            if (!on[static_cast<std::size_t>(cy) * gw + cx]) continue;
            for (int dy = -kCloseCells; dy <= kCloseCells; ++dy)
                for (int dx = -kCloseCells; dx <= kCloseCells; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x >= 0 && y >= 0 && x < gw && y < gh) grown[static_cast<std::size_t>(y) * gw + x] = 1;
                }
        }
    std::vector<int> label(on.size(), -1);
    struct Blob { int cells = 0, x0, y0, x1, y1; };
    std::vector<Blob> blobs;
    for (int start = 0; start < gw * gh; ++start) {
        if (!grown[start] || label[start] >= 0) continue;
        Blob b{0, gw, gh, -1, -1};
        const int id = static_cast<int>(blobs.size());
        std::deque<int> q{start};
        label[start] = id;
        while (!q.empty()) {
            const int i = q.front();
            q.pop_front();
            const int cx = i % gw, cy = i / gw;
            if (on[i]) {
                ++b.cells;
                b.x0 = std::min(b.x0, cx); b.y0 = std::min(b.y0, cy);
                b.x1 = std::max(b.x1, cx); b.y1 = std::max(b.y1, cy);
            }
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= gw || y >= gh) continue;
                    const int j = y * gw + x;
                    if (grown[j] && label[j] < 0) { label[j] = id; q.push_back(j); }
                }
        }
        blobs.push_back(b);
    }
    int largest = 0;
    for (const auto& b : blobs) largest = std::max(largest, b.cells);
    if (largest < 3) {
        out.why = "nothing on the glass (background " + std::to_string(bg) + ")";
        return out;
    }
    int bx0 = gw, by0 = gh, bx1 = -1, by1 = -1;
    for (const auto& b : blobs)
        if (b.cells >= std::max(3.0, kKeepBlob * largest)) {
            bx0 = std::min(bx0, b.x0); by0 = std::min(by0, b.y0);
            bx1 = std::max(bx1, b.x1); by1 = std::max(by1, b.y1);
        }
    // The box in pixels, [x0, x1) x [y0, y1).
    const int x0 = bx0 * cell, y0 = by0 * cell;
    const int x1 = std::min(W, (bx1 + 1) * cell), y1 = std::min(H, (by1 + 1) * cell);
    const int bw = x1 - x0, bh = y1 - y0;

    out.found = true;
    out.landscape = bw > bh;
    out.content = {to_units(x0, r.dpi), to_units(y0, r.dpi), to_units(bw, r.dpi), to_units(bh, r.dpi)};

    // 3. Pages: a profile along the long side.
    const bool across = bw >= bh;   // the spine runs across the long side
    const int len = across ? bw : bh, depth = across ? bh : bw;
    std::vector<double> shade(len), ink(len);
    for (int i = 0; i < len; ++i) {
        long long g = 0;
        int k = 0;
        for (int j = 0; j < depth; ++j) {
            const int x = across ? x0 + i : x0 + j, y = across ? y0 + j : y0 + i;
            g += im.gray(x, y);
            k += mask[static_cast<std::size_t>(y) * W + x];
        }
        shade[i] = double(g) / depth;
        ink[i] = double(k) / depth;
    }
    auto smooth = [&](const std::vector<double>& v, int win) {
        std::vector<double> o(v.size());
        for (int i = 0; i < int(v.size()); ++i) {
            double s = 0;
            int n = 0;
            for (int d = -win; d <= win; ++d)
                if (i + d >= 0 && i + d < int(v.size())) { s += v[i + d]; ++n; }
            o[i] = s / n;
        }
        return o;
    };
    // Wide enough that lines of text average out (a line every ~1/6") while a
    // real spine shadow (a quarter inch and up) survives.
    shade = smooth(shade, std::max(1, r.dpi / 8));
    // A gutter is a blank band a third of an inch and wider; the gaps between
    // lines of text are not.
    ink   = smooth(ink, std::max(1, r.dpi / 6));

    bool spine = false, gutter = false;
    if (len >= 4 * cell) {
        const int m0 = len * 35 / 100, m1 = len * 65 / 100;
        // Spine shadow.
        const double pages = median_d(shade);
        int at = m0;
        for (int i = m0; i < m1; ++i) if (shade[i] < shade[at]) at = i;
        const double drop = pages - shade[at];
        if (drop > kSpineDark) {
            const double half = pages - drop / 2;
            int a = at, b = at;
            while (a > 0 && shade[a - 1] < half) --a;
            while (b < len - 1 && shade[b + 1] < half) ++b;
            spine = (b - a + 1) < kSpineMaxWidth * len;
        }
        // Blank gutter between two inked halves.
        auto mean = [&](int a, int b) {
            double s = 0;
            for (int i = a; i < b; ++i) s += ink[i];
            return b > a ? s / (b - a) : 0.0;
        };
        const double left = mean(len * 5 / 100, len * 35 / 100), right = mean(len * 65 / 100, len * 95 / 100);
        double valley = 1e9;
        for (int i = m0; i < m1; ++i) valley = std::min(valley, ink[i]);
        gutter = !spine && left > 0.02 && right > 0.02 && valley < kGutterRatio * std::min(left, right) &&
                 bw * bh > 0;
    }

    // 4. Type, from the inside of the box (inset, so a book's coloured boards
    //    and the page edges do not decide it).
    const int ix0 = x0 + bw * 8 / 100, ix1 = x1 - bw * 8 / 100;
    const int iy0 = y0 + bh * 8 / 100, iy1 = y1 - bh * 8 / 100;
    long long content = 0, colour = 0, mid = 0, area = 0;
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            ++area;
            if (!mask[static_cast<std::size_t>(y) * W + x]) continue;
            ++content;
            if (im.chroma(x, y) > kColorChroma) ++colour;
            const int g = im.gray(x, y);
            if (g > 70 && g < 185) ++mid;
        }
    const double coverage = area ? double(content) / area : 0;
    out.color_fraction   = content ? double(colour) / content : 0;
    out.midtone_fraction = content ? double(mid) / content : 0;

    out.sideways = (spine || gutter) && !across;
    if (spine)       { out.kind = Kind::Book;     out.pages = 2; }
    else if (gutter) { out.kind = Kind::TwoPages; out.pages = 2; }
    else if (coverage > kPhotoCoverage) { out.kind = Kind::Photo; out.pages = 0; }
    else             { out.kind = Kind::Page;     out.pages = 1; }

    const double w_in = bw / double(r.dpi), h_in = bh / double(r.dpi);
    if (out.color_fraction > kColorFraction) {
        out.mode = escl::Mode::Color;
        out.why = "colour on the page";
    } else if (out.kind != Kind::Photo && content > 0 && out.midtone_fraction < kBwMidtones) {
        out.mode = escl::Mode::BlackWhite;
        out.why = "clean black on white, no mid-tones";
    } else {
        out.mode = escl::Mode::Gray;
        out.why = out.kind == Kind::Photo ? "a picture without colour" : "text, no colour";
    }
    out.dpi = (out.kind == Kind::Photo && w_in * h_in < 40) ? 600 : 300;

    // 5. Size, and the crop it suggests.
    if (out.pages == 2)
        out.size_name = across ? nearest_size(w_in / 2, h_in) : nearest_size(w_in, h_in / 2);
    else
        out.size_name = nearest_size(w_in, h_in);
    escl::Region g = out.content;
    g.x = std::max(0, g.x - kMarginUnits);
    g.y = std::max(0, g.y - kMarginUnits);
    g.w = std::min(bed_w - g.x, out.content.w + 2 * kMarginUnits);
    g.h = std::min(bed_h - g.y, out.content.h + 2 * kMarginUnits);
    out.region = g;
    return out;
}

std::string headline(const Suggestion& s) {
    if (!s.found) return "The glass looks empty";
    std::string k = kind_label(s.kind);
    k[0] = char(k[0] - 'a' + 'A');
    std::string h = k + " found";
    if (s.pages == 2) h += "  ·  2 pages up";
    // Two pages up is a landscape spread however it lies; say so when it lies
    // turned on the glass.
    if (s.pages == 2) h += s.sideways ? "  ·  landscape, turned on the glass" : "  ·  landscape";
    else              h += s.landscape ? "  ·  landscape" : "  ·  portrait";
    h += "  ·  " + escl::inches_label(s.content.w, s.content.h);
    return h;
}

std::string advice(const Suggestion& s) {
    if (!s.found) return "Nothing to crop to; the scan will take the whole glass.";
    std::string a = "Suggest " + std::string(escl::mode_label(s.mode)) + ", " + std::to_string(s.dpi) +
                    " dpi, crop to " + fmt2(s.region.w / 300.0, s.region.h / 300.0) + " (" + s.why + ")";
    if (!s.size_name.empty()) a += (s.pages == 2 ? ". Each page looks like " : ". Looks like ") + s.size_name;
    return a + ".";
}

}  // namespace scanit::model::detect
