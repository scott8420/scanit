#include "model/Output.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

std::string clean_base(const std::string& base) {
    std::string o;
    for (unsigned char c : base) {
        if (c < 0x20 || c == '/' || c == 0x7f) continue;
        o += static_cast<char>(c);
    }
    std::size_t b = 0, e = o.size();
    while (b < e && (o[b] == ' ' || o[b] == '.')) ++b;
    while (e > b && o[e - 1] == ' ') --e;
    return o.substr(b, e - b);
}

std::string numbered_name(const std::string& base, int digits, int n, const std::string& ext) {
    char num[32];
    std::snprintf(num, sizeof num, "%0*d", std::clamp(digits, 1, 9), std::max(n, 0));
    return clean_base(base) + "_" + num + (ext.empty() ? "" : "." + ext);
}

int next_number(const std::string& base, const std::vector<std::string>& names) {
    const std::string prefix = clean_base(base) + "_";
    int best = 0;
    for (const auto& f : names) {
        if (f.size() <= prefix.size() || f.compare(0, prefix.size(), prefix) != 0) continue;
        std::size_t i = prefix.size(), j = i;
        while (j < f.size() && f[j] >= '0' && f[j] <= '9') ++j;
        if (j == i || j - i > 9) continue;                       // no digits, or absurd
        if (j < f.size() && f[j] != '.') continue;               // name_0001x.png is not ours
        best = std::max(best, std::atoi(f.substr(i, j - i).c_str()));
    }
    return best + 1;
}

std::string human_bytes(long long n) {
    char buf[32];
    if (n >= 1024LL * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
    else if (n >= 1024)     std::snprintf(buf, sizeof buf, "%lld KB", n / 1024);
    else                    std::snprintf(buf, sizeof buf, "%lld B", n);
    return buf;
}

}  // namespace scanit::model::output
