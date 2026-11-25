#pragma once
// io/EsclClient.hpp -- the eSCL conversation over HTTP (libsoup 3).
//
// Blocking calls: run them on a worker thread. The UI asks through a Job and
// watches its atomics; nothing here touches GTK. model/Escl.hpp builds and
// parses the XML.

#include "model/Escl.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace scanit::io {

// What a running scan reports, read by the UI thread while it runs.
struct Progress {
    std::atomic<int>       phase{0};        // see Phase
    std::atomic<long long> bytes{0};        // received so far
    std::atomic<bool>      cancel{false};   // the UI sets it; the worker obeys
};

enum Phase : int { Idle = 0, Asking = 1, Scanning = 2, Receiving = 3, Done = 4 };

struct ScanResult {
    bool              ok = false;
    std::string       error;
    std::string       mime;      // what the scanner says it sent
    std::vector<char> data;
};

class EsclClient {
public:
    explicit EsclClient(std::string base_url);   // "http://10.0.0.5:80/eSCL"
    ~EsclClient();
    EsclClient(const EsclClient&) = delete;
    EsclClient& operator=(const EsclClient&) = delete;

    const std::string& base() const { return m_base; }

    // `raw` (optional) gets the document as sent -- Scanit keeps a copy for
    // the selftest fixture and for diagnosing a scanner we have not met.
    bool capabilities(model::escl::Caps& out, std::string* err, std::string* raw = nullptr);
    std::string state(std::string* err);   // "Idle", ... ("" on failure)

    // POST the job, then pull NextDocument until it arrives. Honours
    // p.cancel (the job is DELETEd).
    ScanResult scan(const model::escl::Settings& s, Progress& p);

private:
    struct Impl;
    std::string           m_base;
    std::unique_ptr<Impl> m;
};

}  // namespace scanit::io
