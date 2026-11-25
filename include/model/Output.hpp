#pragma once
// model/Output.hpp -- where a scan lands and what it is called. Pure.
//
// s001: one name pattern, scan-YYYYMMDD-HHMMSS.<ext>, in one folder
// (~/Pictures/Scanit). Book naming (Author / Title / 0001.tif) is milestone 4.

#include <ctime>
#include <functional>
#include <string>

namespace scanit::model::output {

// "scan-20261125-160312.jpg" for that local time.
std::string scan_name(const std::tm& local, const std::string& ext);

// dir/name, or dir/stem-2.ext, -3 ... -- the first that `exists` says is free.
std::string unique_path(const std::string& dir, const std::string& name,
                        const std::function<bool(const std::string&)>& exists);

// "1.4 MB", "820 KB", "512 B"
std::string human_bytes(long long n);

}  // namespace scanit::model::output
