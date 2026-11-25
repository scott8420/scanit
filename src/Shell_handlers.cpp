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
    if (m_loop.phase() == model::Loop::Phase::Waiting) {
        m_loop.skip();                 // "Scan next" mid-wait: now, not in n seconds
        m_countdown.disconnect();
    } else if (!m_loop.running() && m_book.plan().continuous) {
        m_loop.start(m_book.plan().pause_s);
        if (auto lg = log::get(log::Area::Scan))
            lg->info("continuous: start, {} s between pages", m_book.plan().pause_s);
    }
    if (m_scan.state() == model::look::State::Results) m_scan.reset();
    start_scan();
}

void Shell::on_preview() {  // handler: Preview -- a quick pass + the suggestion
    if (m_worker.joinable() || !m_dev.ready) return;
    if (m_scan.state() == model::look::State::Results) m_scan.reset();
    start_preview();
}

void Shell::on_use_suggestion() {  // handler: the card's "Use suggestion"
    if (m_sugg && m_book.apply(*m_sugg)) paint_plan();
}

void Shell::on_whole_glass() {  // handler: the crop box back to the whole glass
    if (m_book.whole_glass()) paint_plan();
}

void Shell::on_mode(model::escl::Mode m) {  // handler: a Type toggle
    if (m_book.set_mode(m)) paint_plan();
}

void Shell::on_dpi_selected() {  // handler: the Resolution drop-down
    if (m_painting || !m_dpi) return;
    const auto i = m_dpi->get_selected();
    if (i >= m_dpi_values.size()) return;
    if (m_book.set_dpi(m_dpi_values[i])) paint_plan();
}

void Shell::on_rotate_page(int delta) {  // handler: results page: turn the saved page
    if (m_last_path.empty() || m_scan.state() != model::look::State::Results) return;
    m_book.turn_page(m_last_path, delta);   // the store turns the file (Shell ctor's on_page)
}

void Shell::on_rotate_setting(int turns) {  // handler: quick settings: turn every page
    if (m_book.set_rotate(turns)) paint_plan();
}

void Shell::on_undo() {  // handler: Ctrl+Z / the menu
    const auto r = m_book.journal().undo();
    if (r.done) {
        if (auto lg = log::get(log::Area::Shell)) lg->info("undo: {}", r.label);
        paint_plan();
    }
}

void Shell::on_redo() {  // handler: Ctrl+Shift+Z / the menu
    const auto r = m_book.journal().redo();
    if (r.done) {
        if (auto lg = log::get(log::Area::Shell)) lg->info("redo: {}", r.label);
        paint_plan();
    }
}

// The window's keys, BUBBLE phase: only what nothing focused took. A text box
// keeps its own Ctrl+Z (focus_is_text), even when it has nothing to undo.
bool Shell::on_key(guint key, guint, Gdk::ModifierType mods) {  // handler: window keys
    const bool ctrl  = (mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool shift = (mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    if (key == GDK_KEY_Escape && m_book.journal().in_gesture()) {
        m_book.crop_cancel();
        paint_plan();
        return true;
    }
    if (!ctrl || focus_is_text()) return false;
    if ((key == GDK_KEY_z || key == GDK_KEY_Z) && shift) { activate_action("win.redo"); return true; }
    if (key == GDK_KEY_z || key == GDK_KEY_Z)            { activate_action("win.undo"); return true; }
    if (key == GDK_KEY_y || key == GDK_KEY_Y)            { activate_action("win.redo"); return true; }
    return false;
}

void Shell::on_scan_stop() {  // handler: Stop
    if (m_loop.phase() == model::Loop::Phase::Waiting) {   // between pages: just end the run
        end_loop("Stopped");
        return;
    }
    if (m_scan.state() != model::look::State::Running) return;
    m_loop.stop();                     // mid-page: cancel the page AND the run
    m_progress.cancel = true;          // the worker DELETEs the job and comes back
    m_run_caption.set_text("Stopping…");
    if (auto lg = log::get(log::Area::Scan)) lg->info("stop requested");
}

void Shell::on_scan_again() {  // handler: results -> start
    if (m_loop.running()) end_loop("Stopped");
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
        if (m_job_dev.ready) {
            m_dev = m_job_dev;
            // A (new) scanner: fresh defaults for its glass, the old history
            // and preview forgotten. A load, not a user choice.
            m_book.load(m_dev.caps);
            m_named_base = "\x01";   // the format may differ: repaint the next name
            m_sugg.reset();
            m_preview_pix.reset();
            m_crop.set_preview({});
            m_crop.set_content(std::nullopt);
            m_crop.set_bed(m_dev.caps.platen.max_w, m_dev.caps.platen.max_h);
            m_painting = true;
            m_dpi_values = m_dev.caps.platen.resolutions;
            if (m_dpi_values.empty()) m_dpi_values = {m_book.plan().dpi};
            std::vector<Glib::ustring> rows;
            for (int d : m_dpi_values) rows.push_back(std::to_string(d) + " dpi");
            m_dpi_list->splice(0, m_dpi_list->get_n_items(), rows);
            m_painting = false;
            paint_plan();
        }
        paint_scanner();
        paint_scan();
        return;
    }
    if (job == Job::Preview) {
        m_previewing = false;
        m_scan.stop();   // running -> start: a preview has no results page; the glass is the result
        if (m_job_pix) {
            m_preview_pix = m_job_pix;
            m_crop.set_preview(m_preview_pix);
            m_sugg = m_job_sugg;
            m_crop.set_content(m_sugg->found ? std::optional(m_sugg->content) : std::nullopt);
            m_scan_err.set_text("");
            // The suggestion goes straight in, as ONE step -- Ctrl+Z takes it back.
            m_book.apply(*m_sugg);
            if (auto lg = log::get(log::Area::Scan))
                lg->info("preview: {} -- {} (colour {:.3f}, mid-tones {:.3f})",
                         model::detect::headline(*m_sugg), model::detect::advice(*m_sugg),
                         m_sugg->color_fraction, m_sugg->midtone_fraction);
        } else {
            m_scan_err.set_text(m_job_error);
        }
        m_job_pix.reset();
        paint_plan();
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
            paint_next_name();   // the counter moved on
            m_preview.set_filename(m_job_path);
            // Continuous: the page is on disk -- only NOW does the wait start.
            // Nothing is timed or guessed; the scanner's own "done" (the image
            // fully received, the job closed) is the signal.
            if (m_loop.running()) {
                m_loop.scanned();
                m_res_big.set_text("Saved page " + std::to_string(m_loop.pages()));
                m_countdown.disconnect();
                m_countdown = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &Shell::loop_tick), 1);
                if (auto lg = log::get(log::Area::Scan))
                    lg->info("continuous: page {} saved, next in {} s", m_loop.pages(), m_loop.left());
            }
        } else {
            m_scan.stop();
            m_scan_err.set_text(m_job_error);
            if (m_loop.running()) {
                const int n = m_loop.pages();
                m_loop.failed();
                m_countdown.disconnect();
                m_scan_err.set_text(m_job_error + "\nContinuous scanning stopped after " +
                                    std::to_string(n) + (n == 1 ? " page." : " pages."));
            }
        }
        paint_loop();
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
