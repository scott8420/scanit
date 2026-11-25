#include "Shell.hpp"
#include "Log.hpp"
#include "Rotate.hpp"

#include <gtkmm/label.h>
#include <gdkmm/pixbufloader.h>
#include <giomm/simpleaction.h>
#include <gtk/gtk.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <cmath>
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

// What a scan sends: the plan -- the quick settings and the crop box, as the
// user left them (model::plan::Book; every change an undo step).
escl::Settings Shell::scan_settings() const {  // helper: what a scan sends
    return m_book.settings();
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
    const int turns = m_book.plan().rotate;
    const std::string name_base = m_book.plan().base;
    const int digits = m_book.plan().digits;
    m_worker = std::thread([this, s, base, dir, turns, name_base, digits] {
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
            // Rotate each page: turned before it is written (eSCL cannot).
            if (turns != 0) {
                std::string rerr;
                if (rotate::rotate_bytes(r.data, mime, turns, &rerr)) {
                    if (auto l = slog()) l->info("rotated {}", model::plan::turns_label(turns));
                } else if (auto l = slog()) {
                    l->warn("not rotated ({}); saved as scanned", rerr);
                }
            }
            // A base: name_0001.ext, numbered after the highest already in the
            // folder (counted now, at save time, so continuous scanning and a
            // resumed book both just carry on). No base: the date and time.
            std::string name;
            if (!name_base.empty()) {
                std::vector<std::string> names;
                for (const auto& e : fs::directory_iterator(dir, ec)) names.push_back(e.path().filename().string());
                name = model::output::numbered_name(name_base, digits,
                                                    model::output::next_number(name_base, names),
                                                    escl::extension_for(mime));
            } else {
                name = model::output::scan_name(local, escl::extension_for(mime));
            }
            path = model::output::unique_path(dir, name, [](const std::string& p) {
                std::error_code e;
                return fs::exists(p, e);
            });
            std::ofstream out(path, std::ios::binary);
            out.write(r.data.data(), static_cast<std::streamsize>(r.data.size()));
            if (!out) { err = "Could not write " + path; path.clear(); }
            else {
                rotate::forget(path);
                if (auto l = slog()) l->info("saved {} ({} bytes)", path, r.data.size());
            }
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

// The preview: the whole glass at the lowest useful resolution (nearest 75
// dpi), in colour when the scanner has it so the detector can see colour.
// Decoded here (gdk-pixbuf, no GTK), read by model::detect, handed back.
// The raw bytes are kept at ~/.cache/scanit/preview.<ext> -- a real glass to
// replay when the detector guesses wrong.
void Shell::start_preview() {  // helper: worker: low-res pass, decode, detect
    if (m_worker.joinable() || !m_dev.ready) return;
    if (!m_scan.begin()) return;
    m_previewing = true;
    m_progress.cancel = false;
    m_progress.bytes = 0;
    m_progress.phase = io::Asking;
    m_scan_err.set_text("");
    {
        std::lock_guard lk(m_mu);
        m_job = Job::Preview;
        m_job_error.clear();
        m_job_pix.reset();
        m_job_sugg = {};
    }
    paint_scan();
    m_tick = Glib::signal_timeout().connect(sigc::mem_fun(*this, &Shell::scan_tick), 150);

    const auto& p = m_dev.caps.platen;
    escl::Settings s;
    s.version = m_dev.caps.version.empty() ? "2.0" : m_dev.caps.version;
    s.dpi     = escl::nearest_resolution(p, 75);
    if (s.dpi == 0) s.dpi = 75;
    s.mode    = escl::supports(p, escl::Mode::Color) || p.modes.empty() ? escl::Mode::Color : escl::Mode::Gray;
    s.region  = escl::full_bed(p);
    s.format  = escl::choose_format(p, {"image/png", "image/jpeg"});
    const std::string base = m_dev.base;
    const int bed_w = p.max_w, bed_h = p.max_h;
    if (auto l = slog()) l->info("preview: {} dpi {} {}", s.dpi, escl::mode_escl(s.mode), s.format);

    m_worker = std::thread([this, s, base, bed_w, bed_h] {
        io::EsclClient c(base);
        io::ScanResult r = c.scan(s, m_progress);
        if (!r.ok && !m_progress.cancel && s.format != "image/jpeg" &&
            r.error.find("refused the job") != std::string::npos) {
            escl::Settings j = s;
            j.format = "image/jpeg";
            r = c.scan(j, m_progress);
        }
        Glib::RefPtr<Gdk::Pixbuf> pix;
        model::detect::Suggestion sugg;
        std::string err = r.error;
        if (r.ok) {
            std::string mime = r.mime;
            if (auto semi = mime.find(';'); semi != std::string::npos) mime.resize(semi);
            std::error_code ec;
            fs::create_directories(cache_dir(), ec);
            std::ofstream(cache_dir() + "/preview." + escl::extension_for(mime), std::ios::binary)
                .write(r.data.data(), static_cast<std::streamsize>(r.data.size()));
            try {
                auto loader = Gdk::PixbufLoader::create();
                loader->write(reinterpret_cast<const guint8*>(r.data.data()), r.data.size());
                loader->close();
                pix = loader->get_pixbuf();
            } catch (const Glib::Error& e) {
                err = "The preview came back but could not be read (" + mime + "): " + e.what();
            }
            if (pix) {
                model::detect::Raster ras;
                ras.width    = pix->get_width();
                ras.height   = pix->get_height();
                ras.channels = pix->get_n_channels();
                ras.stride   = pix->get_rowstride();
                ras.data     = pix->get_pixels();
                // The resolution the image really has across the glass (a
                // scanner may round): pixels per 1/300" of bed, times 300.
                ras.dpi = std::max(1, static_cast<int>(std::lround(ras.width * 300.0 / bed_w)));
                sugg = model::detect::detect(ras, bed_w, bed_h);
            }
        }
        {
            std::lock_guard lk(m_mu);
            m_job_pix = pix;
            m_job_sugg = sugg;
            m_job_error = pix ? std::string() : (err.empty() ? "The preview failed." : err);
        }
        m_done.emit();
    });
}

bool Shell::scan_tick() {  // helper: running-state numbers from m_progress
    if (m_scan.state() != model::look::State::Running) return false;
    if (m_progress.cancel) return true;   // "Stopping..." stays put
    if (m_loop.running() && !m_previewing) {
        scan_tick_body();
        m_run_caption.set_text("Page " + std::to_string(m_loop.pages() + 1) + " (continuous)  ·  " +
                               m_run_caption.get_text());
        return true;
    }
    scan_tick_body();
    return true;
}

void Shell::scan_tick_body() {  // helper: the phase words
    switch (m_progress.phase.load()) {
        case io::Asking:
            m_run_big.set_text(m_previewing ? "Preview" : "Starting");
            m_run_caption.set_text("Sending the job to the scanner");
            break;
        case io::Scanning:
            m_run_big.set_text(m_previewing ? "Previewing" : "Scanning");
            m_run_caption.set_text("The head is moving (in colour the lamp calibrates first)");
            break;
        case io::Receiving:
            m_run_big.set_text(model::output::human_bytes(m_progress.bytes.load()));
            m_run_caption.set_text(m_previewing ? "Receiving the preview" : "Receiving the page");
            break;
        default: break;
    }
    return;
}

// Continuous scanning: one second of the wait. When it runs out, the next
// page scans (results -> start -> running, the ordinary path).
bool Shell::loop_tick() {  // helper: continuous: one second of the countdown
    if (m_loop.phase() != model::Loop::Phase::Waiting) return false;
    if (m_loop.tick()) {
        if (m_worker.joinable() || !m_dev.ready) { end_loop("Stopped: the scanner is busy or gone"); return false; }
        m_scan.reset();
        start_scan();
        paint_loop();
        return false;
    }
    paint_loop();
    return true;
}

void Shell::end_loop(const std::string& why) {  // helper: continuous: stop the run
    const int n = m_loop.pages();
    m_loop.stop();
    m_countdown.disconnect();
    m_res_big.set_text(why + " after " + std::to_string(n) + (n == 1 ? " page" : " pages"));
    if (auto lg = log::get(log::Area::Scan)) lg->info("continuous: {} after {} pages", why, n);
    paint_loop();
}

void Shell::paint_loop() {  // helper: continuous: countdown + Stop on the results page
    const bool waiting = m_loop.phase() == model::Loop::Phase::Waiting;
    m_res_loop.set_visible(waiting);
    m_loop_stop.set_visible(waiting);
    if (waiting)
        m_res_loop.set_text("Next page in " + std::to_string(m_loop.left()) + " s  ·  turn the page");
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
        m_scan_sub.set_text(m_dev.name + "\n" + m_book.summary() + "  ·  " +
                            escl::extension_for(s.format) + "\nto " + out_dir());
    } else {
        m_scan_sub.set_text("No scanner yet. The Scanner page says why.");
    }
    m_orb.set_sensitive(m_dev.ready && !m_finding);
    if (auto a = std::dynamic_pointer_cast<Gio::SimpleAction>(lookup_action("preview")))
        a->set_enabled(m_dev.ready && !m_finding);
}

// The quick settings and the crop view, from the plan. Guarded: a widget set
// here fires its signal, and a repaint is not a user choice.
void Shell::paint_plan() {  // helper: quick settings + crop view from m_book
    if (!m_type_color) return;   // before the page exists
    m_painting = true;
    const auto& p = m_book.plan();
    const auto& caps = m_book.caps();
    const bool ready = m_dev.ready;
    m_type_color->set_active(p.mode == escl::Mode::Color);
    m_type_gray->set_active(p.mode == escl::Mode::Gray);
    m_type_bw->set_active(p.mode == escl::Mode::BlackWhite);
    m_type_color->set_sensitive(ready && escl::supports(caps.platen, escl::Mode::Color));
    m_type_gray->set_sensitive(ready && escl::supports(caps.platen, escl::Mode::Gray));
    m_type_bw->set_sensitive(ready && escl::supports(caps.platen, escl::Mode::BlackWhite));
    for (std::size_t i = 0; i < m_dpi_values.size(); ++i)
        if (m_dpi_values[i] == p.dpi) m_dpi->set_selected(static_cast<guint>(i));
    m_dpi->set_sensitive(ready);
    auto slide = [&](widgets::Scale& sc, int v, const escl::Range& r, const char* what) {
        sc.set_value(v);
        sc.set_sensitive(ready && r.present);
        sc.set_tooltip_text(r.present ? std::string("0 is the scanner's normal ") + what
                                      : std::string("This scanner does not offer ") + what);
    };
    slide(m_bright, p.brightness, caps.brightness, "brightness");
    slide(m_contrast, p.contrast, caps.contrast, "contrast");
    slide(m_sharpen, p.sharpen, caps.sharpen, "sharpening");

    for (int t = 0; t < 4; ++t)
        if (m_rot[t]) {
            m_rot[t]->set_active(p.rotate == t);
            m_rot[t]->set_sensitive(ready);
        }
    // Compare cleaned: typing "War " (a trailing space on the way to "War and")
    // is the same base, and must not be snapped back mid-word.
    if (model::output::clean_base(m_base_entry->get_text()) != p.base)
        m_base_entry->set_text(p.base);
    m_base_entry->set_sensitive(ready);
    m_dig3->set_active(p.digits == 3);
    m_dig4->set_active(p.digits == 4);
    if (p.base != m_named_base || p.digits != m_named_digits) paint_next_name();
    m_cont->set_active(p.continuous);
    m_cont->set_sensitive(ready);
    m_pause->set_value(p.pause_s);
    m_pause->set_sensitive(ready);

    m_crop.set_region(p.region);
    m_crop.set_sensitive(ready);
    m_crop_label.set_text(
        (m_book.is_whole_glass() ? std::string("Whole glass, ") : std::string("Crop ")) +
        escl::inches_label(p.region.w, p.region.h) + " at " + std::to_string(p.dpi) + " dpi  ·  " +
        std::to_string(escl::pixels(p.region.w, p.dpi)) + " x " +
        std::to_string(escl::pixels(p.region.h, p.dpi)) + " px");

    m_sugg_card.set_visible(m_sugg.has_value());
    if (m_sugg) {
        m_sugg_head.set_text(model::detect::headline(*m_sugg));
        m_sugg_text.set_text(model::detect::advice(*m_sugg));
        m_use_sugg.set_visible(m_sugg->found);
    }
    m_painting = false;
    paint_scan();
}

// "Next: Book_0012.png" -- what the next scan will be called. Lists the
// folder, so it runs when the base or width changes and after each scan,
// not on every repaint.
void Shell::paint_next_name() {  // helper: the quick settings' next file name
    const auto& p = m_book.plan();
    m_named_base = p.base;
    m_named_digits = p.digits;
    const std::string ext = escl::extension_for(scan_settings().format);
    if (p.base.empty()) {
        m_next_name.set_text("Blank: named by date and time (scan-20261125-160312." + ext + ")");
        return;
    }
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(out_dir(), ec)) names.push_back(e.path().filename().string());
    m_next_name.set_text("Next: " + model::output::numbered_name(p.base, p.digits,
                                                                model::output::next_number(p.base, names), ext));
}

void Shell::relabel_undo() {  // helper: the menu's Undo / Redo rows + greying
    auto& j = m_book.journal();
    if (m_undo_menu) {
        m_undo_menu->remove_all();
        m_undo_menu->append(j.can_undo() ? "Undo " + j.undo_label() : std::string("Undo"), "win.undo");
        m_undo_menu->append(j.can_redo() ? "Redo " + j.redo_label() : std::string("Redo"), "win.redo");
    }
    if (auto a = std::dynamic_pointer_cast<Gio::SimpleAction>(lookup_action("undo"))) a->set_enabled(j.can_undo());
    if (auto a = std::dynamic_pointer_cast<Gio::SimpleAction>(lookup_action("redo"))) a->set_enabled(j.can_redo());
}

bool Shell::focus_is_text() const {  // helper: Ctrl+Z belongs to a text box
    auto* w = const_cast<Shell*>(this)->get_focus();
    if (!w) return false;
    GtkWidget* g = w->gobj();
    return GTK_IS_EDITABLE(g) || GTK_IS_TEXT_VIEW(g);
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
                             ", " + (m_book.is_whole_glass() ? std::string("the whole glass")
                                                             : escl::inches_label(s.region.w, s.region.h)));
    auto range = [](const escl::Range& r) {
        return r.present ? std::to_string(r.min) + " .. " + std::to_string(r.max) + " (normal " +
                               std::to_string(r.normal) + ")"
                         : std::string("not offered");
    };
    card("Corrections", "brightness " + range(m_dev.caps.brightness) + "\ncontrast " +
                            range(m_dev.caps.contrast) + "\nsharpen " + range(m_dev.caps.sharpen));
    if (m_found.size() > 1) {
        std::string others;
        for (std::size_t i = 1; i < m_found.size(); ++i)
            others += (others.empty() ? "" : "\n") + m_found[i].name + "  (" + m_found[i].address + ")";
        card("Also found", others);
    }
    card("Saved", "Capabilities kept at " + cache_dir() + "/ScannerCapabilities.xml");
}

}  // namespace scanit
