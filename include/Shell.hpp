#pragma once
#include "widgets/Widgets.hpp"
#include "model/Escl.hpp"
#include "model/Look.hpp"
#include "model/Output.hpp"
#include "io/Discovery.hpp"
#include "io/EsclClient.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/picture.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <glibmm/dispatcher.h>
#include <sigc++/connection.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- Scanit's window. A split class across TUs by CATEGORY (Cairn's
// shape): every method carries a `// category:` tag naming its file.
//
//   Zones    -- construction: window, sidebar, the Scan and Scanner pages,
//               the menu model.                      src/Shell_zones.cpp
//   Bindings -- win.* actions.                       src/Shell_bindings.cpp
//   Handlers -- what a user action does (on_*).      src/Shell_handlers.cpp
//   Helpers  -- painters, the worker, settings.      src/Shell_helpers.cpp
//   Glue     -- ctor + build_ui().                   src/Shell.cpp
//
// Finding things
//   "What does the orb do?"             -> on_scan -> start_scan (worker)
//   "Where does the scanner get found?" -> on_find -> start_find (worker)
//   "What comes back to the UI thread?" -> on_worker_done (the Dispatcher)
//   "What settings does a scan send?"   -> scan_settings()
//   "Where does the file go?"           -> out_dir() + the worker in start_scan
//
// Threads: ONE worker at a time (m_worker), finding or scanning. It writes
// only m_job_* fields under m_mu and the atomics in m_progress; the UI thread
// reads them in on_worker_done / scan_tick. No widget is touched off the UI
// thread.
// ─────────────────────────────────────────────────────────────────────────────
namespace scanit {

class AboutWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window + header + body
    void build_pages();                          // category: zone: both pages, then the sidebar
    void build_nav();                            // category: zone: brand + sections + rows (model::look::scanit_nav)
    void build_scan_page();                      // category: zone: Scan -- start / running / results
    void build_scanner_page();                   // category: zone: Scanner -- what was found, what it can do
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: win.* actions

    // ── handlers ───────────────────────────────────────────────────────────
    void on_nav(const std::string& page);        // category: handler: sidebar row -> stack child + tint
    void on_scan();                              // category: handler: the orb / Ctrl+Enter / "Scan next"
    void on_scan_stop();                         // category: handler: Stop
    void on_scan_again();                        // category: handler: results -> start (no scan)
    void on_open_folder();                       // category: handler: the scans folder in Files
    void on_find();                              // category: handler: look for the scanner (mDNS)
    void on_use_address();                       // category: handler: the address field -> that scanner
    void on_worker_done();                       // category: handler: the Dispatcher (UI thread)
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_about();                             // category: handler: the About window
    void on_shortcuts();                         // category: handler: the keyboard reference

    // ── helpers ────────────────────────────────────────────────────────────
    enum class Job { None, Find, Scan };
    void start_find(const std::string& address); // category: helper: worker: discover (or use address) + caps
    void start_scan();                           // category: helper: worker: the job, the bytes, the file
    bool scan_tick();                            // category: helper: running-state numbers from m_progress
    model::escl::Settings scan_settings() const; // category: helper: s001: 300 dpi gray, full bed
    std::string out_dir() const;                 // category: helper: ~/Pictures/Scanit
    void paint_scan();                           // category: helper: the Scan page from m_scan + the device
    void paint_scanner();                        // category: helper: the Scanner page from the device
    void join_worker();                          // category: helper: join if it ran

    // Titlebar.
    widgets::Button     m_logo_button;
    widgets::MenuButton m_menu_button;

    // Body: the look's sidebar | stack. (With SCANIT_LOOK=0 the StackSidebar.)
    widgets::Box          m_body;
    widgets::StackSidebar m_sidebar;
    widgets::Box          m_side;
    widgets::ListBox      m_nav;
    widgets::Stack        m_stack;
    std::string           m_tint;

    // Scan page.
    model::look::PageStates m_scan;
    widgets::Stack   m_scan_states;
    widgets::Label   m_scan_sub;        // "EPSON ET-8550 · 300 dpi gray · whole bed" / "Looking..."
    widgets::Label   m_scan_err;        // the last failure, in .stop
    widgets::Button  m_orb;
    widgets::Spinner m_run_spinner;
    widgets::Label   m_run_big;         // "Scanning" / "1.4 MB"
    widgets::Label   m_run_caption;
    widgets::Label   m_res_big;
    widgets::Label   m_res_caption;     // the path
    Gtk::Picture     m_preview;
    sigc::connection m_tick;

    // Scanner page.
    widgets::Label   m_dev_title;
    widgets::Label   m_dev_sub;
    widgets::Box     m_dev_cards;       // one card per fact, rebuilt by paint_scanner
    widgets::Entry   m_addr;
    widgets::Button  m_find_button;

    // The device we talk to (UI thread only).
    struct Device {
        bool              ready = false;
        std::string       name;          // from mDNS or the caps
        std::string       base;          // eSCL root URL
        model::escl::Caps caps;
    };
    Device                     m_dev;
    bool                       m_finding = false;
    std::string                m_find_note;   // why there is no device, or ""
    std::vector<io::Found>     m_found;

    // The worker and what it hands back (under m_mu).
    std::thread       m_worker;
    Glib::Dispatcher  m_done;
    Job               m_job = Job::None;
    std::mutex        m_mu;
    io::Progress      m_progress;
    Device            m_job_dev;
    std::vector<io::Found> m_job_found;
    std::string       m_job_error;
    std::string       m_job_path;        // the saved file
    long long         m_job_bytes = 0;
    std::string       m_last_path;       // UI copy of the last saved file

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace scanit
