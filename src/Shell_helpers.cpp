#include "Shell.hpp"
#include "Log.hpp"

#include <gtkmm/label.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

// Shell_helpers.cpp -- HELPERS. The worker (find / scan), the settings a scan
// sends, where files go, and the two painters.

namespace scanit {

namespace fs = std::filesystem;
namespace escl = model::escl;

namespace {
auto slog() { return log::get(log::Area::Scan); }

// "192.168.1.40", "192.168.1.40:8080", "http://host/eSCL" -> an eSCL root.
std::string base_from_address(std::string a) {
    while (!a.empty() && (a.back() == '/' || a.back() == ' ')) a.pop_back();
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    if (a.rfind("http://", 0) == 0 || a.rfind("https://", 0) == 0) {
        const auto host_start = a.find("://") + 3;
        if (a.find('/', host_start) == std::string::npos) a += "/eSCL";
        return a;
    }
    int port = 80;
    std::string host = a;
    // host:port, but not a bare IPv6 address
    if (auto c = a.rfind(':'); c != std::string::npos && a.find(':') == c) {
        host = a.substr(0, c);
        port = std::atoi(a.c_str() + c + 1);
        if (port <= 0) port = 80;
    }
    return escl::base_url(host, port, "eSCL", false);
}

std::string cache_dir() {
    return Glib::get_user_cache_dir() + "/scanit";
}
}  // namespace

void Shell::join_worker() {  // helper: join if it ran
    if (m_worker.joinable()) m_worker.join();
}

std::string Shell::out_dir() const {  // helper: ~/Pictures/Scanit
    std::string pics = Glib::get_user_special_dir(Glib::UserDirectory::PICTURES);
    if (pics.empty()) pics = Glib::get_home_dir() + "/Pictures";
    return pics + "/Scanit";
}

// s001's one fixed choice: 300 dpi (or the nearest the glass offers), gray
// (or colour if it has no gray), the whole bed, PNG if it will, else TIFF,
// else JPEG. Presets, cropping and the mode switch are milestones 2 and 3.
escl::Settings Shell::scan_settings() const {  // helper: what a scan sends
    const auto& p = m_dev.caps.platen;
    escl::Settings s;
    s.version = m_dev.caps.version.empty() ? "2.0" : m_dev.caps.version;
    s.dpi     = escl::nearest_resolution(p, 300);
    if (s.dpi == 0) s.dpi = 300;
    s.mode    = escl::supports(p, escl::Mode::Gray) ? escl::Mode::Gray : escl::Mode::Color;
    s.region  = escl::full_bed(p);
    s.format  = escl::choose_format(p, {"image/png", "image/tiff", "image/jpeg"});
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// The worker. One at a time. It writes m_job_* under m_mu and m_progress's
// atomics, then emits m_done; on_worker_done reads it all on the UI thread.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::start_find(const std::string& address) {  // helper: worker: discover + caps
    if (m_worker.joinable()) return;
    m_finding = true;
    m_find_note.clear();
    paint_scan();
    paint_scanner();
    {
        std::lock_guard lk(m_mu);
        m_job = Job::Find;
        m_job_dev = Device{};
        m_job_found.clear();
        m_job_error.clear();
    }
    std::string addr = address;
    if (addr.empty())
        if (const char* env = std::getenv("SCANIT_HOST")) addr = env;   // a fixed address, for tests

    m_worker = std::thread([this, addr] {
        Device dev;
        std::vector<io::Found> found;
        std::string err;
        if (addr.empty()) {
            found = io::discover(3000, &err);
            if (found.empty() && err.empty())
                err = "No scanner answered on the network. Is it on and on the same Wi-Fi? "
                      "You can type its address below.";
            if (!found.empty()) {
                dev.name = found.front().name;
                dev.base = escl::base_url(found.front().address, found.front().port,
                                          found.front().rs, found.front().tls);
            }
        } else {
            dev.base = base_from_address(addr);
            dev.name = addr;
        }
        if (!dev.base.empty()) {
            io::EsclClient c(dev.base);
            std::string raw, cerr;
            if (c.capabilities(dev.caps, &cerr, &raw)) {
                dev.ready = true;
                if (!dev.caps.make_model.empty()) dev.name = dev.caps.make_model;
                // Keep the document: the selftest's fixture-to-be, and the first
                // thing to read when a scanner misbehaves.
                std::error_code ec;
                fs::create_directories(cache_dir(), ec);
                std::ofstream(cache_dir() + "/ScannerCapabilities.xml") << raw;
                if (auto l = slog())
                    l->info("ready: {} at {} (glass {}, {} resolutions, modes: {})", dev.name,
                            dev.base, escl::inches_label(dev.caps.platen.max_w, dev.caps.platen.max_h),
                            dev.caps.platen.resolutions.size(), dev.caps.platen.modes.size());
            } else {
                err = "Found " + dev.name + " but could not read what it can do: " + cerr;
            }
        }
        {
            std::lock_guard lk(m_mu);
            m_job_dev = dev;
            m_job_found = found;
            m_job_error = err;
        }
        m_done.emit();
    });
}

void Shell::start_scan() {  // helper: worker: the job, the bytes, the file
    if (m_worker.joinable() || !m_dev.ready) return;
    if (!m_scan.begin()) return;
    m_progress.cancel = false;
    m_progress.bytes = 0;
    m_progress.phase = io::Asking;
    m_scan_err.set_text("");
    {
        std::lock_guard lk(m_mu);
        m_job = Job::Scan;
        m_job_error.clear();
        m_job_path.clear();
        m_job_bytes = 0;
    }
    paint_scan();
    m_tick = Glib::signal_timeout().connect(sigc::mem_fun(*this, &Shell::scan_tick), 150);

    const escl::Settings s = scan_settings();
    const std::string base = m_dev.base;
    const std::string dir = out_dir();
    m_worker = std::thread([this, s, base, dir] {
        io::EsclClient c(base);
        io::ScanResult r = c.scan(s, m_progress);
        // A scanner that lists a format and then refuses it is not unheard of:
        // one retry in JPEG, which every eSCL scanner takes.
        if (!r.ok && !m_progress.cancel && s.format != "image/jpeg" &&
            r.error.find("refused the job") != std::string::npos) {
            if (auto l = slog()) l->warn("{} refused; retrying as image/jpeg", s.format);
            escl::Settings j = s;
            j.format = "image/jpeg";
            r = c.scan(j, m_progress);
        }
        std::string path, err = r.error;
        if (r.ok) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_r(&now, &local);
            std::string mime = r.mime;
            if (auto semi = mime.find(';'); semi != std::string::npos) mime.resize(semi);
            const std::string name = model::output::scan_name(local, escl::extension_for(mime));
            path = model::output::unique_path(dir, name, [](const std::string& p) {
                std::error_code e;
                return fs::exists(p, e);
            });
            std::ofstream out(path, std::ios::binary);
            out.write(r.data.data(), static_cast<std::streamsize>(r.data.size()));
            if (!out) { err = "Could not write " + path; path.clear(); }
            else if (auto l = slog()) l->info("saved {} ({} bytes)", path, r.data.size());
        }
        {
            std::lock_guard lk(m_mu);
            m_job_path = path;
            m_job_error = err;
            m_job_bytes = static_cast<long long>(r.data.size());
        }
        m_done.emit();
    });
}

bool Shell::scan_tick() {  // helper: running-state numbers from m_progress
    if (m_scan.state() != model::look::State::Running) return false;
    if (m_progress.cancel) return true;   // "Stopping..." stays put
    switch (m_progress.phase.load()) {
        case io::Asking:
            m_run_big.set_text("Starting");
            m_run_caption.set_text("Sending the job to the scanner");
            break;
        case io::Scanning:
            m_run_big.set_text("Scanning");
            m_run_caption.set_text("The head is moving (in colour the lamp calibrates first)");
            break;
        case io::Receiving:
            m_run_big.set_text(model::output::human_bytes(m_progress.bytes.load()));
            m_run_caption.set_text("Receiving the page");
            break;
        default: break;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Painters. Each repaints its page from state; called after every change.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::paint_scan() {  // helper: the Scan page
    using model::look::State;
    const State st = m_scan.state();
    m_scan_states.set_visible_child(model::look::name(st));
    m_run_spinner.set_spinning(st == State::Running);
    if (st == State::Running) scan_tick();

    if (m_finding) {
        m_scan_sub.set_text("Looking for the scanner…");
    } else if (m_dev.ready) {
        const auto s = scan_settings();
        m_scan_sub.set_text(m_dev.name + "\n" + std::to_string(s.dpi) + " dpi  ·  " +
                            escl::mode_label(s.mode) + "  ·  the whole glass (" +
                            escl::inches_label(s.region.w, s.region.h) + ")  ·  " +
                            escl::extension_for(s.format) + "\nto " + out_dir());
    } else {
        m_scan_sub.set_text("No scanner yet. The Scanner page says why.");
    }
    m_orb.set_sensitive(m_dev.ready && !m_finding);
}

void Shell::paint_scanner() {  // helper: the Scanner page
    while (auto* c = m_dev_cards.get_first_child()) m_dev_cards.remove(*c);
    m_find_button.set_sensitive(!m_finding);

    if (m_finding) {
        m_dev_title.set_text("Looking…");
        m_dev_sub.set_text("Asking the network for AirScan (eSCL) scanners.");
        return;
    }
    if (!m_dev.ready) {
        m_dev_title.set_text("No scanner");
        m_dev_sub.set_text(m_find_note.empty() ? "Press Find again." : m_find_note);
        return;
    }
    m_dev_title.set_text(m_dev.name);
    m_dev_sub.set_text(m_dev.base);

    auto card = [this](const std::string& title, const std::string& value) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        box->add_css_class("card");
        auto t = Gtk::make_managed<Gtk::Label>(title);
        t->add_css_class("card-title");
        t->set_xalign(0.0f);
        t->set_size_request(150, -1);
        t->set_valign(Gtk::Align::START);
        auto v = Gtk::make_managed<Gtk::Label>(value);
        v->add_css_class("card-sub");
        v->set_xalign(0.0f);
        v->set_wrap(true);
        v->set_hexpand(true);
        v->set_selectable(true);
        box->append(*t);
        box->append(*v);
        m_dev_cards.append(*box);
    };
    auto join = [](const auto& v, const std::string& sep) {
        std::string s;
        for (const auto& x : v) {
            if (!s.empty()) s += sep;
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, int>) s += std::to_string(x);
            else s += x;
        }
        return s.empty() ? std::string("(none listed)") : s;
    };
    const auto& p = m_dev.caps.platen;
    const auto s = scan_settings();
    card("Glass", escl::inches_label(p.max_w, p.max_h) + "  ·  " +
                      std::to_string(escl::pixels(p.max_w, s.dpi)) + " x " +
                      std::to_string(escl::pixels(p.max_h, s.dpi)) + " px at " +
                      std::to_string(s.dpi) + " dpi");
    card("Resolutions", join(p.resolutions, ", ") + " dpi");
    card("Colour modes", join(p.modes, ", "));
    card("Formats", join(p.formats, ", "));
    card("Feeder", m_dev.caps.adf.present ? "yes" : "no, flatbed only");
    card("eSCL", "version " + (m_dev.caps.version.empty() ? std::string("?") : m_dev.caps.version));
    card("A scan sends", std::to_string(s.dpi) + " dpi, " + escl::mode_escl(s.mode) + ", " + s.format +
                             ", the whole glass");
    if (m_found.size() > 1) {
        std::string others;
        for (std::size_t i = 1; i < m_found.size(); ++i)
            others += (others.empty() ? "" : "\n") + m_found[i].name + "  (" + m_found[i].address + ")";
        card("Also found", others);
    }
    card("Saved", "Capabilities kept at " + cache_dir() + "/ScannerCapabilities.xml");
}

}  // namespace scanit
