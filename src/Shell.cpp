#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow>
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Rotate.hpp"

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass; every other
// body lives in a category file (see the index in Shell.hpp).

namespace scanit {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_menu_button("shell.menu_button"),
      m_body("shell.body", Gtk::Orientation::HORIZONTAL),
      m_sidebar("shell.sidebar"),
      m_side("shell.side", Gtk::Orientation::VERTICAL),
      m_nav("shell.nav"),
      m_stack("shell.stack"),
      m_scan_states("scan.states"),
      m_scan_sub("scan.sub"),
      m_scan_err("scan.err"),
      m_orb("scan.orb"),
      m_run_spinner("scan.run.spinner"),
      m_run_big("scan.run.big"),
      m_run_caption("scan.run.caption"),
      m_res_big("scan.res.big"),
      m_res_caption("scan.res.caption"),
      m_res_loop("scan.res.loop"),
      m_loop_stop("scan.loop_stop"),
      m_next_name("quick.next_name"),
      m_crop_label("scan.crop.label"),
      m_bright("quick.brightness", Gtk::Orientation::HORIZONTAL),
      m_contrast("quick.contrast", Gtk::Orientation::HORIZONTAL),
      m_sharpen("quick.sharpen", Gtk::Orientation::HORIZONTAL),
      m_sugg_head("preview.headline"),
      m_sugg_text("preview.advice"),
      m_use_sugg("preview.use"),
      m_sugg_card("preview.card", Gtk::Orientation::VERTICAL, 6),
      m_dev_title("scanner.title"),
      m_dev_sub("scanner.sub"),
      m_dev_cards("scanner.cards", Gtk::Orientation::VERTICAL, 10),
      m_addr("scanner.address"),
      m_find_button("scanner.find") {
    set_name("shell.window");
    m_done.connect(sigc::mem_fun(*this, &Shell::on_worker_done));
    m_book.journal().set_on_change([this] { relabel_undo(); });
    // A saved page turned (or turned back by undo): do it to the file, then
    // show it again if it is the page on screen.
    m_book.set_on_page([this](const std::string& path, int turns) {
        std::string err;
        if (!rotate::turn_file(path, turns, &err)) {
            if (auto lg = log::get(log::Area::Scan)) lg->warn("rotate {}: {}", path, err);
            m_scan_err.set_text("Could not rotate: " + err);
            return;
        }
        if (auto lg = log::get(log::Area::Scan)) lg->info("rotated {} to {}", path, model::plan::turns_label(turns));
        if (path == m_last_path) {
            m_preview.set_file({});
            m_preview.set_filename(path);
        }
    });
}

Shell::~Shell() {
    m_tick.disconnect();
    m_countdown.disconnect();
    m_progress.cancel = true;     // a scan in flight stops; discovery ends on its own
    join_worker();
}

// The ordered pass: construct the shell, build the pages, bind actions, then
// go and look for the scanner.
// Text that changes while scanning must never widen the window either: every
// such label wraps anywhere (a path has no spaces), so its minimum width is a
// character, not the whole line.
static void no_grow(Gtk::Label& l) {
    l.set_wrap(true);
    l.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    l.set_width_chars(1);
}

void Shell::build_ui() {
    build_shell();     // zone
    build_pages();     // zone
    for (Gtk::Label* l : {static_cast<Gtk::Label*>(&m_scan_sub), static_cast<Gtk::Label*>(&m_scan_err),
                          static_cast<Gtk::Label*>(&m_run_big), static_cast<Gtk::Label*>(&m_run_caption),
                          static_cast<Gtk::Label*>(&m_res_big), static_cast<Gtk::Label*>(&m_res_caption),
                          static_cast<Gtk::Label*>(&m_res_loop), static_cast<Gtk::Label*>(&m_crop_label),
                          static_cast<Gtk::Label*>(&m_next_name), static_cast<Gtk::Label*>(&m_sugg_head),
                          static_cast<Gtk::Label*>(&m_sugg_text), static_cast<Gtk::Label*>(&m_dev_sub)})
        no_grow(*l);
    bind_actions();    // bindings
    relabel_undo();
    paint_plan();
    paint_scan();
    paint_scanner();
    on_find();         // find the scanner straight away
    if (auto lg = log::get(log::Area::Shell)) lg->info("ready");
}

}  // namespace scanit
