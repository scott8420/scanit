#pragma once
// io/Discovery.hpp -- find eSCL scanners on the network (mDNS / DNS-SD).
//
// Browses _uscan._tcp (HTTP) and _uscans._tcp (HTTPS) through Avahi, resolves
// each, and returns what it found after `timeout_ms`. Blocking: call it from a
// worker thread, never the UI thread. No GTK.

#include <string>
#include <vector>

namespace scanit::io {

struct Found {
    std::string name;      // the service name, e.g. "EPSON ET-8550 Series"
    std::string host;      // "EPSON123456.local"
    std::string address;   // "192.168.1.40" (IPv4 preferred)
    int         port = 0;
    std::string rs;        // TXT "rs", the eSCL root ("eSCL")
    bool        tls = false;
};

// Empty + *err set when Avahi is not reachable (avahi-daemon not running).
std::vector<Found> discover(int timeout_ms, std::string* err = nullptr);

}  // namespace scanit::io
