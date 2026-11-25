#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/filelauncher.h>
#include <giomm/file.h>

#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. What a user action actually does. Each is
// small; the worker and the painters live in helpers.

namespace scanit {

void Shell::on_nav(const std::string& page) {  // handler: sidebar row -> stack child + tint
    std::string tint;
    for (const auto& e : model::look::scanit_nav())
        if (!e.section && e.id == page) tint = e.tint;
    if (!m_tint.empty()) remove_css_class("tint-" + m_tint);
    m_tint = tint;
    if (!m_tint.empty()) add_css_class("tint-" + m_tint);
    m_stack.set_visible_child(page);
}

void Shell::on_scan() {  // handler: the orb / Ctrl+Enter / Scan next
    if (m_worker.joinable() || !m_dev.ready) return;   // finding, or nothing to talk to
    if (m_scan.state() == model::look::State::Results) m_scan.reset();
    start_scan();
}

void Shell::on_scan_stop() {  // handler: Stop
    if (m_scan.state() != model::look::State::Running) return;
    m_progress.cancel = true;          // the worker DELETEs the job and comes back
    m_run_caption.set_text("Stopping…");
    if (auto lg = log::get(log::Area::Scan)) lg->info("stop requested");
}

void Shell::on_scan_again() {  // handler: results -> start
    if (m_scan.reset()) paint_scan();
}

void Shell::on_open_folder() {  // handler: the scans folder in Files
    std::error_code ec;
    std::filesystem::create_directories(out_dir(), ec);
    auto launcher = Gtk::FileLauncher::create(Gio::File::create_for_path(out_dir()));
    launcher->launch(*this, [launcher](const Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            launcher->launch_finish(r);
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Shell)) lg->warn("open folder: {}", e.what());
        }
    });
}

void Shell::on_find() {  // handler: look for the scanner
    start_find({});
}

void Shell::on_use_address() {  // handler: the address field -> that scanner
    const std::string a = m_addr.get_text();
    if (a.empty()) return;
    start_find(a);
}

// The worker came back. Join it, take what it left under the mutex, repaint.
void Shell::on_worker_done() {  // handler: the Dispatcher (UI thread)
    join_worker();
    m_tick.disconnect();
    std::lock_guard lk(m_mu);
    const Job job = m_job;
    m_job = Job::None;

    if (job == Job::Find) {
        m_finding = false;
        m_found = m_job_found;
        m_find_note = m_job_error;
        if (m_job_dev.ready) m_dev = m_job_dev;
        paint_scanner();
        paint_scan();
        return;
    }
    if (job == Job::Scan) {
        if (!m_job_path.empty()) {
            m_scan.finish();
            m_last_path = m_job_path;
            m_scan_err.set_text("");
            m_res_big.set_text("Saved");
            m_res_caption.set_text(m_job_path + "  ·  " + model::output::human_bytes(m_job_bytes));
            m_preview.set_filename(m_job_path);
        } else {
            m_scan.stop();
            m_scan_err.set_text(m_job_error);
        }
        paint_scan();
    }
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    registry::dump();
}

void Shell::on_about() {  // handler: the About window
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

void Shell::on_shortcuts() {  // handler: the keyboard reference
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

}  // namespace scanit
