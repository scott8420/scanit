#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow>
#include "ShortcutsDialog.hpp"
#include "Log.hpp"

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
      m_dev_title("scanner.title"),
      m_dev_sub("scanner.sub"),
      m_dev_cards("scanner.cards", Gtk::Orientation::VERTICAL, 10),
      m_addr("scanner.address"),
      m_find_button("scanner.find") {
    set_name("shell.window");
    m_done.connect(sigc::mem_fun(*this, &Shell::on_worker_done));
}

Shell::~Shell() {
    m_tick.disconnect();
    m_progress.cancel = true;     // a scan in flight stops; discovery ends on its own
    join_worker();
}

// The ordered pass: construct the shell, build the pages, bind actions, then
// go and look for the scanner.
void Shell::build_ui() {
    build_shell();     // zone
    build_pages();     // zone
    bind_actions();    // bindings
    paint_scan();
    paint_scanner();
    on_find();         // find the scanner straight away
    if (auto lg = log::get(log::Area::Shell)) lg->info("ready");
}

}  // namespace scanit
