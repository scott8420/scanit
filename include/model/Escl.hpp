#pragma once
// model/Escl.hpp -- the eSCL (AirScan) protocol as data. No GTK, no network.
//
// eSCL is HTTP + XML. The scanner advertises itself over mDNS as _uscan._tcp
// (or _uscans._tcp for HTTPS) with a TXT record "rs" naming the root (almost
// always "eSCL"). Then:
//
//   GET  /eSCL/ScannerCapabilities   what it can do      -> parse_caps()
//   GET  /eSCL/ScannerStatus         Idle / Processing   -> parse_state()
//   POST /eSCL/ScanJobs              a ScanSettings doc  <- build_scan_settings()
//        201 Created, Location: .../ScanJobs/<id>         -> job_url()
//   GET  <job>/NextDocument          the image bytes (blocks while it scans);
//        404 once there is nothing more
//
// Lengths in eSCL are in 1/300 inch ("ThreeHundredthsOfInches"); this file
// keeps them that way and converts only for display. Everything here is a
// pure function, so the selftest walks it headless. io/Escl* does the HTTP.

#include <string>
#include <string_view>
#include <vector>

namespace scanit::model::escl {

inline constexpr int kUnitsPerInch = 300;

enum class Mode { Gray, Color, BlackWhite };

const char* mode_escl(Mode m);    // "Grayscale8" / "RGB24" / "BlackAndWhite1"
const char* mode_label(Mode m);   // "gray" / "color" / "black & white"

// One input source's limits (the platen; the ADF if there is one).
struct Source {
    bool present = false;
    int  min_w = 0, min_h = 0;          // 1/300"
    int  max_w = 0, max_h = 0;          // 1/300"
    std::vector<int>         resolutions;   // discrete X resolutions, ascending
    std::vector<std::string> modes;         // "RGB24", "Grayscale8", ...
    std::vector<std::string> formats;       // "image/jpeg", "application/pdf", ...
};

struct Caps {
    std::string version;        // pwg:Version, e.g. "2.63"
    std::string make_model;     // "EPSON ET-8550 Series"
    std::string uuid;
    Source platen;
    Source adf;
};

// Parse ScannerCapabilities. On failure returns false and says why in *err.
bool parse_caps(std::string_view xml, Caps& out, std::string* err = nullptr);

// pwg:State from ScannerStatus ("Idle", "Processing", "Stopped", ...), "" if none.
std::string parse_state(std::string_view xml);

bool supports(const Source& s, Mode m);

// The supported resolution closest to `want` (ties go up). 0 if none listed.
int nearest_resolution(const Source& s, int want);

// The format to ask for: the first of `preferred` the source lists, else its
// first format, else "image/jpeg".
std::string choose_format(const Source& s, const std::vector<std::string>& preferred);

// File extension (no dot) for a MIME type: "png", "jpg", "pdf", "tif", else "bin".
std::string extension_for(std::string_view mime);

struct Region {
    int x = 0, y = 0, w = 0, h = 0;     // 1/300", from the bed's top-left
};

Region full_bed(const Source& s);

// Clamp a region onto the bed: inside it, at least its minimum size.
Region clamp(const Source& s, Region r);

struct Settings {
    std::string version = "2.0";
    Mode        mode    = Mode::Gray;
    int         dpi     = 300;
    Region      region;
    std::string format  = "image/jpeg";
};

std::string build_scan_settings(const Settings& s);

// Resolve the Location header of a 201 against the base URL
// ("http://10.0.0.5:80/eSCL"): absolute stays, "/eSCL/ScanJobs/7" gets the
// scheme+host, a bare "ScanJobs/7" gets the whole base.
std::string job_url(std::string_view base, std::string_view location);

// "http://<host>:<port>/<rs>" with IPv6 hosts bracketed and rs trimmed of
// slashes ("" -> "eSCL").
std::string base_url(std::string_view host, int port, std::string_view rs, bool tls);

// Pixel size of a region at a resolution (for "2550 x 3300 px" and the
// expected-bytes hint).
int pixels(int units, int dpi);

// "8.50 x 11.69 in"
std::string inches_label(int w_units, int h_units);

}  // namespace scanit::model::escl
