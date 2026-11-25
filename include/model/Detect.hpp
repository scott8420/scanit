#pragma once
// model/Detect.hpp -- reading a preview of the glass: what is on it, how big,
// which way round, and what to scan it as. GTK-free: the app decodes the
// preview into a Raster and hands it here, so the selftest drives it with
// synthetic glasses.
//
// What it looks for, in order:
//
//   1. The background -- the median of the image's outer ring (the lid's
//      white, or black with the lid up for a thick book).
//   2. Content -- cells (1/10") where enough pixels differ from that
//      background in brightness or colour. Specks are dropped: the largest
//      connected blob, plus any blob at least a tenth its size, make the box.
//   3. Pages -- along the box's long side, in its middle third: a spine
//      shadow (a narrow band clearly darker than the pages) or a blank
//      gutter between two inked halves. Either means two pages up; a shadow
//      means a book.
//   4. Type -- colour when enough of the content is saturated; black & white
//      when the content has almost no mid-tones (clean line art / text);
//      gray otherwise. Resolution follows (300 for text, 600 for a small
//      photo).
//   5. Size -- the box in inches, the nearest common book size when one is
//      close, and the crop it suggests (the box plus a small margin).
//
// It is a heuristic and says so: the summary is a suggestion the user takes
// or leaves (and Ctrl+Z takes back).

#include "model/Escl.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace scanit::model::detect {

// 8-bit pixels, row-major. channels = 1 (gray), 3 (RGB) or 4 (RGBA).
struct Raster {
    int width = 0, height = 0, channels = 3, stride = 0;
    const std::uint8_t* data = nullptr;
    int dpi = 75;   // the preview's resolution, to turn pixels into 1/300"
};

enum class Kind { Empty, Page, TwoPages, Book, Photo };

struct Suggestion {
    bool         found = false;
    Kind         kind  = Kind::Empty;
    int          pages = 0;               // 0, 1 or 2
    bool         landscape = false;       // the box on the glass is wider than tall
    bool         sideways  = false;       // two pages whose spine runs across the glass:
                                          // a landscape spread lying turned 90 degrees
    escl::Region content;                 // the box, 1/300"
    escl::Region region;                  // the suggested crop (box + margin, on the bed)
    escl::Mode   mode = escl::Mode::Gray;
    int          dpi  = 300;
    std::string  size_name;               // "6 x 9 (trade)" per page, or ""
    double       color_fraction = 0;      // of content pixels, for the log
    double       midtone_fraction = 0;
    std::string  why;                     // one line: what decided the type
};

// bed_w / bed_h: the glass in 1/300", to clamp the suggested crop.
Suggestion detect(const Raster& img, int bed_w, int bed_h);

// "Book found · 2 pages up · landscape · 12.00 x 9.00 in"
std::string headline(const Suggestion& s);
// "Suggest gray, 300 dpi, crop 12.20 x 9.20 in -- text, no colour"
std::string advice(const Suggestion& s);

const char* kind_label(Kind k);   // "book", "two pages", "page", "photo", "nothing"

// The nearest common size within `tol` inches either way ("6 x 9 (trade)"),
// either orientation; "" if none.
std::string nearest_size(double w_in, double h_in, double tol = 0.35);

}  // namespace scanit::model::detect
