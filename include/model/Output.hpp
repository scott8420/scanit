#pragma once
// model/Output.hpp -- where a scan lands and what it is called. Pure.
//
// s001: one name pattern, scan-YYYYMMDD-HHMMSS.<ext>, in one folder
// (~/Pictures/Scanit). Book naming (Author / Title / 0001.tif) is milestone 4.

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace scanit::model::output {

// "scan-20261125-160312.jpg" for that local time.
std::string scan_name(const std::tm& local, const std::string& ext);

// dir/name, or dir/stem-2.ext, -3 ... -- the first that `exists` says is free.
std::string unique_path(const std::string& dir, const std::string& name,
                        const std::function<bool(const std::string&)>& exists);

// ── numbered names (Scott: "a naming base ... like name_0001.jpg") ────────
// The base as it goes into a file name: trimmed, '/' and control characters
// gone, no leading dots. "" means "no base" (the date-and-time name).
std::string clean_base(const std::string& base);

// "name_0001.jpg" (digits = width, zero-padded; a number wider than the width
// is written in full: name_10000.jpg).
std::string numbered_name(const std::string& base, int digits, int n, const std::string& ext);

// The number after the highest "<base>_<digits>.<anything>" among `names`
// (file names, no directory) -- 1 if there is none. Any width counts, so a
// folder begun at 001 carries on at 0004 if the width changes. Resuming a book
// is just keeping the base: the count picks up where the folder left off.
int next_number(const std::string& base, const std::vector<std::string>& names);

// "1.4 MB", "820 KB", "512 B"
std::string human_bytes(long long n);

}  // namespace scanit::model::output
