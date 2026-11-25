#pragma once
#include "widgets/Widgets.hpp"
#include "CropView.hpp"
#include "model/Detect.hpp"
#include "model/Escl.hpp"
#include "model/Look.hpp"
#include "model/Loop.hpp"
#include "model/Output.hpp"
#include "model/Plan.hpp"
#include "io/Discovery.hpp"
#include "io/EsclClient.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/picture.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/switch.h>
#include <gtkmm/spinbutton.h>
#include <gdkmm/pixbuf.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <glibmm/dispatcher.h>
#include <sigc++/connection.h>

#include <memory>
#include <optional>
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
//   "What settings does a scan send?"   -> scan_settings() -> m_book (model::plan)
//   "What does Preview do?"             -> on_preview -> start_preview (worker)
//                                          -> model::detect -> m_book.apply (one step)
//   "Where do the quick settings go?"   -> the on_* handlers -> m_book's verbs
//                                          (every one an undo step) -> paint_plan
//   "How does continuous scanning loop?" -> on_scan starts m_loop; on_worker_done
//                                          (file saved) -> m_loop.scanned + loop_tick
//                                          each second -> start_scan; Stop ends it
//   "Where is Ctrl+Z?"                  -> on_key (the window, bubble phase)
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
    Gtk::Widget* build_quick_panel();            // category: zone: Scan -- quick settings + preview card + the orb
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
    void on_preview();                           // category: handler: Preview -- a quick pass + the suggestion
    void on_use_suggestion();                    // category: handler: the card's "Use suggestion"
    void on_whole_glass();                       // category: handler: the crop box back to the whole glass
    void on_mode(model::escl::Mode m);           // category: handler: a Type toggle
    void on_dpi_selected();                      // category: handler: the Resolution drop-down
    void on_rotate_page(int delta);              // category: handler: results page: turn the saved page
    void on_rotate_setting(int turns);           // category: handler: quick settings: turn every page
    void on_undo();                              // category: handler: Ctrl+Z / the menu
    void on_redo();                              // category: handler: Ctrl+Shift+Z / the menu
    bool on_key(guint key, guint code, Gdk::ModifierType mods);   // category: handler: window keys, bubble phase
    void on_about();                             // category: handler: the About window
    void on_shortcuts();                         // category: handler: the keyboard reference

    // ── helpers ────────────────────────────────────────────────────────────
    enum class Job { None, Find, Scan, Preview };
    void start_find(const std::string& address); // category: helper: worker: discover (or use address) + caps
    void start_scan();                           // category: helper: worker: the job, the bytes, the file
    void start_preview();                        // category: helper: worker: low-res pass, decode, detect
    void paint_plan();                           // category: helper: quick settings + crop view from m_book
    void paint_next_name();                      // category: helper: "Next: Book_0012.png"
    void relabel_undo();                         // category: helper: the menu's Undo / Redo rows + greying
    bool focus_is_text() const;                  // category: helper: Ctrl+Z belongs to a text box
    bool scan_tick();                            // category: helper: running-state numbers from m_progress
    void scan_tick_body();                       // category: helper: the running state's phase words
    bool loop_tick();                            // category: helper: continuous: one second of the countdown
    void paint_loop();                           // category: helper: continuous: the results page's countdown + Stop
    void end_loop(const std::string& why);       // category: helper: continuous: stop the run, say why
    model::escl::Settings scan_settings() const; // category: helper: the plan's settings (m_book)
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
    widgets::Label   m_res_loop;        // "Next page in 6 s -- turn the page" (continuous)
    widgets::Button  m_loop_stop;       // Stop, on the results page while a run waits
    Gtk::Entry*        m_base_entry = nullptr;   // quick settings: file name base
    Gtk::ToggleButton* m_dig3 = nullptr;         // name_001
    Gtk::ToggleButton* m_dig4 = nullptr;         // name_0001
    widgets::Label     m_next_name;              // "Next: Book_0012.png"
    std::string        m_named_base = "\x01";   // what m_next_name was painted for
    int                m_named_digits = 0;
    Gtk::ToggleButton* m_rot[4] = {};   // quick settings: rotate every page, by quarter turns
    Gtk::Switch*     m_cont  = nullptr; // quick settings: continuous on / off
    Gtk::SpinButton* m_pause = nullptr; // quick settings: seconds between pages
    model::Loop      m_loop;            // continuous scanning: scan, wait, scan... until Stop
    sigc::connection m_countdown;       // the 1 s tick while m_loop waits
    bool             m_previewing = false;   // the running state is a preview, not a scan

    // Scan page: the glass + crop box, and the quick settings beside it.
    CropView               m_crop;
    widgets::Label         m_crop_label;      // "6.00 x 9.00 in at 300 dpi -- 1800 x 2700 px"
    Gtk::ToggleButton*     m_type_color = nullptr;
    Gtk::ToggleButton*     m_type_gray  = nullptr;
    Gtk::ToggleButton*     m_type_bw    = nullptr;
    Gtk::DropDown*         m_dpi = nullptr;
    Glib::RefPtr<Gtk::StringList> m_dpi_list;
    std::vector<int>       m_dpi_values;      // m_dpi's rows
    widgets::Scale         m_bright;
    widgets::Scale         m_contrast;
    widgets::Scale         m_sharpen;
    widgets::Label         m_sugg_head;       // "Book found · 2 pages up · landscape · 12.00 x 9.00 in"
    widgets::Label         m_sugg_text;       // "Suggest gray, 300 dpi, crop to ..."
    widgets::Button        m_use_sugg;
    widgets::Box           m_sugg_card;
    bool                   m_painting = false;   // a repaint is setting widgets: not a user choice

    // The plan the next scan sends, and its undo journal.
    model::plan::Book                        m_book;
    std::optional<model::detect::Suggestion> m_sugg;
    Glib::RefPtr<Gdk::Pixbuf>                m_preview_pix;
    Glib::RefPtr<Gio::Menu>                  m_undo_menu;

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
    Glib::RefPtr<Gdk::Pixbuf>   m_job_pix;     // the decoded preview
    model::detect::Suggestion   m_job_sugg;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace scanit
