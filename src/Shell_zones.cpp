#include "Shell.hpp"
#include "Look.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the window,
// the sidebar, the Scan and Scanner pages, the menu model. The look is CSS
// (resources/look.css + Css.hpp); here widgets only get their classes.

namespace scanit {

namespace {
Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Image* hero(const char* icon, int px) {
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name(icon);
    i->set_pixel_size(px);
    return i;
}
}  // namespace

void Shell::build_shell() {  // zone: window + header + body
    set_title("Scanit");
    set_default_size(980, 680);

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    m_logo_button.set_icon_name("scanit-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Scanit");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);
    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);
    set_titlebar(*header);

    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    if (look::enabled()) {
        add_css_class("look");
        m_side.add_css_class("sidebar-pane");
        m_side.set_size_request(220, -1);
        m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
        m_stack.set_transition_duration(180);
        m_body.append(m_side);
    } else {
        m_sidebar.set_stack(m_stack);
        m_sidebar.set_size_request(160, -1);
        m_body.append(m_sidebar);
    }
    m_body.append(m_stack);
    set_child(m_body);
}

void Shell::build_pages() {  // zone: both pages, then the sidebar
    build_scan_page();
    build_scanner_page();
    if (look::enabled()) build_nav();   // last: selecting its first row shows a page
}

// The sidebar from model::look::scanit_nav(). Section headers are rows that
// cannot be selected, activated or focused, so the keyboard skips them.
void Shell::build_nav() {  // zone: brand + sections + rows
    m_side.append(*label("Scanit", "sidebar-brand", 0.0f));
    m_nav.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_nav.set_vexpand(true);
    for (const auto& e : model::look::scanit_nav()) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        if (e.section) {
            row->set_selectable(false);
            row->set_activatable(false);
            row->set_can_focus(false);
            row->add_css_class("section-row");
            row->set_child(*label(e.title, "sidebar-section", 0.0f));
        } else {
            row->set_name("nav." + e.id);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            auto img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name(e.icon);
            img->set_pixel_size(18);
            box->append(*img);
            box->append(*label(e.title, nullptr, 0.0f));
            row->set_child(*box);
        }
        m_nav.append(*row);
    }
    m_nav.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string n = row->get_name();
        if (n.rfind("nav.", 0) == 0) on_nav(n.substr(4));
    });
    m_side.append(m_nav);
    for (int i = 0;; ++i) {
        auto* row = m_nav.get_row_at_index(i);
        if (!row) break;
        if (row->get_selectable()) { m_nav.select_row(*row); break; }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan -- three states in one stack (model::look::PageStates):
//   start   : the hero, what will happen (device, dpi, mode, area), the orb.
//   running : the spinner, what the scanner is doing, Stop.
//   results : saved -- the path, the page itself, Open folder / Scan next.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_scan_page() {  // zone: Scan
    using model::look::State;
    using model::look::name;

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(24);
    start->append(*hero("scanit-hero-scan", 128));
    start->append(*label("Scan", "hero-title"));
    m_scan_sub.add_css_class("hero-sub");
    m_scan_sub.set_wrap(true);
    m_scan_sub.set_justify(Gtk::Justification::CENTER);
    start->append(m_scan_sub);
    m_orb.set_label("Scan");
    m_orb.add_css_class("orb");
    m_orb.set_halign(Gtk::Align::CENTER);
    m_orb.set_margin_top(18);
    m_orb.set_action_name("win.scan");
    m_orb.set_tooltip_text("Scan the page on the glass (Ctrl+Enter)");
    start->append(m_orb);
    m_scan_err.add_css_class("stop");
    m_scan_err.set_wrap(true);
    m_scan_err.set_justify(Gtk::Justification::CENTER);
    m_scan_err.set_max_width_chars(60);
    m_scan_err.set_selectable(true);
    start->append(m_scan_err);
    m_scan_states.add(*start, name(State::Start));

    auto run = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    run->set_valign(Gtk::Align::CENTER);
    run->set_halign(Gtk::Align::CENTER);
    m_run_spinner.add_css_class("big");
    m_run_spinner.set_halign(Gtk::Align::CENTER);
    run->append(m_run_spinner);
    m_run_big.add_css_class("hero-big");
    run->append(m_run_big);
    m_run_caption.add_css_class("hero-caption");
    run->append(m_run_caption);
    auto stop = Gtk::make_managed<Gtk::Button>("Stop");
    stop->set_name("scan.stop");
    stop->add_css_class("ghost");
    stop->set_halign(Gtk::Align::CENTER);
    stop->set_action_name("win.scan-stop");
    run->append(*stop);
    m_scan_states.add(*run, name(State::Running));

    auto res = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 18);
    head->set_margin(24);
    head->set_margin_bottom(12);
    head->append(*hero("scanit-hero-done", 72));
    auto words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    words->set_valign(Gtk::Align::CENTER);
    m_res_big.add_css_class("hero-big");
    m_res_big.set_xalign(0.0f);
    m_res_caption.add_css_class("hero-caption");
    m_res_caption.set_xalign(0.0f);
    m_res_caption.set_selectable(true);
    m_res_caption.set_wrap(true);
    words->append(m_res_big);
    words->append(m_res_caption);
    head->append(*words);
    res->append(*head);

    m_preview.set_name("scan.preview");
    m_preview.add_css_class("preview");
    m_preview.set_content_fit(Gtk::ContentFit::CONTAIN);
    m_preview.set_can_shrink(true);
    m_preview.set_vexpand(true);
    m_preview.set_hexpand(true);
    m_preview.set_margin_start(24);
    m_preview.set_margin_end(24);
    m_preview.set_margin_bottom(16);
    res->append(m_preview);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    auto open = Gtk::make_managed<Gtk::Button>("Open folder");
    open->set_name("scan.open_folder");
    open->add_css_class("ghost");
    open->set_action_name("win.open-folder");
    bar->append(*open);
    auto back = Gtk::make_managed<Gtk::Button>("Done");
    back->set_name("scan.done");
    back->add_css_class("ghost");
    back->set_action_name("win.scan-again");
    bar->append(*back);
    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    bar->append(*spacer);
    auto next = Gtk::make_managed<Gtk::Button>("Scan next");
    next->set_name("scan.next");
    next->add_css_class("act");
    next->set_action_name("win.scan");
    next->set_tooltip_text("Turn the page, then scan again (Ctrl+Enter)");
    bar->append(*next);
    res->append(*bar);
    m_scan_states.add(*res, name(State::Results));

    m_scan_states.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_scan_states.set_transition_duration(160);
    m_stack.add(m_scan_states, "scan", "Scan");
}

// ─────────────────────────────────────────────────────────────────────────────
// Scanner -- the device: what mDNS found, what the scanner says it can do,
// and an address field for when mDNS finds nothing.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_scanner_page() {  // zone: Scanner
    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 18);
    head->set_margin(24);
    head->append(*hero("scanit-hero-scanner", 72));
    auto words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    words->set_valign(Gtk::Align::CENTER);
    m_dev_title.add_css_class("hero-title");
    m_dev_title.set_xalign(0.0f);
    m_dev_sub.add_css_class("hero-caption");
    m_dev_sub.set_xalign(0.0f);
    m_dev_sub.set_wrap(true);
    m_dev_sub.set_selectable(true);
    words->append(m_dev_title);
    words->append(m_dev_sub);
    head->append(*words);
    page->append(*head);

    m_dev_cards.set_margin_start(24);
    m_dev_cards.set_margin_end(24);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_child(m_dev_cards);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    page->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_addr.set_placeholder_text("Address, e.g. 192.168.1.40");
    m_addr.set_hexpand(true);
    m_addr.set_tooltip_text("If the scanner isn't found on its own, type its IP address");
    m_addr.signal_activate().connect([this] { on_use_address(); });
    bar->append(m_addr);
    auto use = Gtk::make_managed<Gtk::Button>("Use");
    use->set_name("scanner.use");
    use->add_css_class("ghost");
    use->set_action_name("win.use-address");
    bar->append(*use);
    m_find_button.set_label("Find again");
    m_find_button.add_css_class("act");
    m_find_button.set_action_name("win.find-scanner");
    bar->append(m_find_button);
    page->append(*bar);

    m_stack.add(*page, "scanner", "Scanner");
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto scan = Gio::Menu::create();
    scan->append("Open scans folder", "win.open-folder");
    scan->append("Find the scanner again", "win.find-scanner");
    menu->append_section(scan);

    auto diag = Gio::Menu::create();
    diag->append("Dump registry", "win.dump-registry");
    menu->append_section(diag);

    auto app = Gio::Menu::create();
    app->append("Keyboard shortcuts", "win.shortcuts");
    app->append("About Scanit", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace scanit
