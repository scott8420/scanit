#include "model/Output.hpp"

#include <cstdio>

namespace scanit::model::output {

std::string scan_name(const std::tm& t, const std::string& ext) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "scan-%04d%02d%02d-%02d%02d%02d", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return std::string(buf) + (ext.empty() ? "" : "." + ext);
}

std::string unique_path(const std::string& dir, const std::string& name,
                        const std::function<bool(const std::string&)>& exists) {
    std::string d = dir;
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    const std::string first = d + "/" + name;
    if (!exists(first)) return first;
    const std::size_t dot = name.rfind('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    const std::string ext  = dot == std::string::npos ? "" : name.substr(dot);
    for (int i = 2; i < 10000; ++i) {
        std::string p = d + "/" + stem + "-" + std::to_string(i) + ext;
        if (!exists(p)) return p;
    }
    return first;   // 10,000 collisions in one second: give up politely
}

std::string human_bytes(long long n) {
    char buf[32];
    if (n >= 1024LL * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
    else if (n >= 1024)     std::snprintf(buf, sizeof buf, "%lld KB", n / 1024);
    else                    std::snprintf(buf, sizeof buf, "%lld B", n);
    return buf;
}

}  // namespace scanit::model::output
