#include "io/EsclClient.hpp"
#include "Log.hpp"

#include <libsoup/soup.h>

#include <chrono>
#include <thread>

// io/EsclClient.cpp -- libsoup 3, synchronous API on the worker thread. One
// SoupSession per client (it owns a GMainContext-free sync path in soup 3).
// Printers serve eSCL over HTTPS with self-signed certificates; on the LAN we
// accept them -- the same call every AirScan client makes.

namespace scanit::io {

namespace {

auto lg() { return log::get(log::Area::Io); }

gboolean accept_any_cert(SoupMessage*, GTlsCertificate*, GTlsCertificateFlags, gpointer) {
    return TRUE;
}

SoupMessage* new_message(const char* method, const std::string& url) {
    SoupMessage* m = soup_message_new(method, url.c_str());
    if (m) g_signal_connect(m, "accept-certificate", G_CALLBACK(accept_any_cert), nullptr);
    return m;
}

std::string bytes_to_string(GBytes* b) {
    if (!b) return {};
    gsize n = 0;
    const char* d = static_cast<const char*>(g_bytes_get_data(b, &n));
    return std::string(d ? d : "", n);
}

}  // namespace

struct EsclClient::Impl {
    SoupSession* session = nullptr;
    Impl() {
        session = soup_session_new();
        // A platen scan at 600 dpi can take a minute before the first byte.
        soup_session_set_timeout(session, 180);
        soup_session_set_idle_timeout(session, 30);
        soup_session_set_user_agent(session, "Scanit/0.1");
    }
    ~Impl() { if (session) g_object_unref(session); }

    // GET or POST; fills status + body + Location. false on transport error.
    bool call(const char* method, const std::string& url, const std::string& body,
              guint& status, std::string& out, std::string* location, std::string* err) {
        SoupMessage* m = new_message(method, url);
        if (!m) { if (err) *err = "bad URL: " + url; return false; }
        if (!body.empty()) {
            GBytes* b = g_bytes_new(body.data(), body.size());
            soup_message_set_request_body_from_bytes(m, "text/xml", b);
            g_bytes_unref(b);
        }
        GError* e = nullptr;
        GBytes* reply = soup_session_send_and_read(session, m, nullptr, &e);
        if (e) {
            if (err) *err = e->message;
            g_error_free(e);
            if (reply) g_bytes_unref(reply);
            g_object_unref(m);
            return false;
        }
        status = soup_message_get_status(m);
        out = bytes_to_string(reply);
        if (location) {
            const char* loc = soup_message_headers_get_one(
                soup_message_get_response_headers(m), "Location");
            *location = loc ? loc : "";
        }
        if (reply) g_bytes_unref(reply);
        g_object_unref(m);
        return true;
    }
};

EsclClient::EsclClient(std::string base_url) : m_base(std::move(base_url)), m(std::make_unique<Impl>()) {}
EsclClient::~EsclClient() = default;

bool EsclClient::capabilities(model::escl::Caps& out, std::string* err, std::string* raw) {
    guint status = 0;
    std::string body;
    if (!m->call("GET", m_base + "/ScannerCapabilities", {}, status, body, nullptr, err)) return false;
    if (status != 200) {
        if (err) *err = "ScannerCapabilities: HTTP " + std::to_string(status);
        return false;
    }
    if (raw) *raw = body;
    if (auto l = lg()) l->debug("caps: {} bytes", body.size());
    return model::escl::parse_caps(body, out, err);
}

std::string EsclClient::state(std::string* err) {
    guint status = 0;
    std::string body;
    if (!m->call("GET", m_base + "/ScannerStatus", {}, status, body, nullptr, err)) return {};
    if (status != 200) { if (err) *err = "ScannerStatus: HTTP " + std::to_string(status); return {}; }
    return model::escl::parse_state(body);
}

ScanResult EsclClient::scan(const model::escl::Settings& s, Progress& p) {
    ScanResult r;
    p.bytes = 0;
    p.phase = Asking;

    // 1. The job. A busy scanner answers 503; give it a few tries.
    const std::string settings = model::escl::build_scan_settings(s);
    if (auto l = lg()) l->info("POST ScanJobs: {} dpi {} {} region {}x{}+{}+{}", s.dpi,
                               model::escl::mode_escl(s.mode), s.format, s.region.w,
                               s.region.h, s.region.x, s.region.y);
    std::string job;
    for (int attempt = 0; attempt < 5; ++attempt) {
        guint status = 0;
        std::string body, location, err;
        if (!m->call("POST", m_base + "/ScanJobs", settings, status, body, &location, &err)) {
            r.error = "Could not reach the scanner: " + err;
            return r;
        }
        if (status == 201 && !location.empty()) { job = model::escl::job_url(m_base, location); break; }
        if (status == 503) { std::this_thread::sleep_for(std::chrono::seconds(2)); continue; }
        r.error = "The scanner refused the job (HTTP " + std::to_string(status) + ")";
        if (auto l = lg()) l->warn("{}; body: {}", r.error, body.substr(0, 400));
        return r;
    }
    if (job.empty()) { r.error = "The scanner stayed busy."; return r; }
    if (auto l = lg()) l->info("job: {}", job);

    // 2. NextDocument: blocks while the head travels, then streams the image.
    p.phase = Scanning;
    GCancellable* cancel = g_cancellable_new();
    std::thread watch([&] {
        while (p.phase != Done && !g_cancellable_is_cancelled(cancel)) {
            if (p.cancel) { g_cancellable_cancel(cancel); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    for (int attempt = 0; attempt < 30 && !r.ok && r.error.empty(); ++attempt) {
        SoupMessage* m2 = new_message("GET", job + "/NextDocument");
        GError* e = nullptr;
        GInputStream* in = soup_session_send(m->session, m2, cancel, &e);
        if (!in) {
            r.error = p.cancel ? "Stopped." : std::string("Lost the scanner: ") + (e ? e->message : "?");
            if (e) g_error_free(e);
            g_object_unref(m2);
            break;
        }
        const guint status = soup_message_get_status(m2);
        if (status == 503) {   // still warming up / calibrating
            g_input_stream_close(in, nullptr, nullptr);
            g_object_unref(in);
            g_object_unref(m2);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (status != 200) {
            r.error = "NextDocument: HTTP " + std::to_string(status);
            g_input_stream_close(in, nullptr, nullptr);
            g_object_unref(in);
            g_object_unref(m2);
            break;
        }
        const char* ct = soup_message_headers_get_content_type(
            soup_message_get_response_headers(m2), nullptr);
        r.mime = ct ? ct : s.format;
        p.phase = Receiving;
        char buf[64 * 1024];
        for (;;) {
            gssize n = g_input_stream_read(in, buf, sizeof buf, cancel, &e);
            if (n < 0) {
                r.error = p.cancel ? "Stopped." : std::string("Transfer broke: ") + (e ? e->message : "?");
                if (e) { g_error_free(e); e = nullptr; }
                break;
            }
            if (n == 0) { r.ok = !r.data.empty(); break; }
            r.data.insert(r.data.end(), buf, buf + n);
            p.bytes = static_cast<long long>(r.data.size());
        }
        if (r.error.empty() && !r.ok) r.error = "The scanner sent an empty page.";
        g_input_stream_close(in, nullptr, nullptr);
        g_object_unref(in);
        g_object_unref(m2);
    }
    if (!r.ok && r.error.empty()) r.error = "The scanner never sent the page.";

    p.phase = Done;
    watch.join();
    g_object_unref(cancel);

    // 3. Tidy the job: a stopped job is DELETEd; a finished one is drained
    // (the next NextDocument answers 404 and the scanner lets go of it).
    {
        guint status = 0;
        std::string body, err;
        if (p.cancel || !r.ok) m->call("DELETE", job, {}, status, body, nullptr, &err);
        else                   m->call("GET", job + "/NextDocument", {}, status, body, nullptr, &err);
        if (auto l = lg()) l->info("job closed (HTTP {})", status);
    }
    if (auto l = lg()) {
        if (r.ok) l->info("received {} bytes ({})", r.data.size(), r.mime);
        else      l->warn("scan failed: {}", r.error);
    }
    return r;
}

}  // namespace scanit::io
