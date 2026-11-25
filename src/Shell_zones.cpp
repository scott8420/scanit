#include "Shell.hpp"
#include "Look.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <cmath>

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
    set_default_size(1060, 780);

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

    // start: the glass (preview + crop box) | the quick settings and the orb.
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 18);
    start->set_margin(20);
    auto glass = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    glass->set_hexpand(true);
    glass->append(m_crop);
    auto crop_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    m_crop_label.add_css_class("card-sub");
    m_crop_label.set_xalign(0.0f);
    m_crop_label.set_hexpand(true);
    m_crop_label.set_wrap(true);
    crop_row->append(m_crop_label);
    auto whole = Gtk::make_managed<Gtk::Button>("Whole glass");
    whole->set_name("scan.whole_glass");
    whole->add_css_class("ghost");
    whole->add_css_class("small");
    whole->set_action_name("win.whole-glass");
    whole->set_tooltip_text("Crop box back to the whole glass");
    crop_row->append(*whole);
    glass->append(*crop_row);
    start->append(*glass);
    start->append(*build_quick_panel());

    m_crop.signal_crop_begin.connect([this] { m_book.crop_begin(); });
    m_crop.signal_crop_move.connect([this](const model::escl::Region& r) {
        m_book.crop_move(r);
        paint_plan();
    });
    m_crop.signal_crop_end.connect([this] {
        m_book.crop_commit();
        paint_plan();
    });
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
    m_res_loop.add_css_class("card-big");
    m_res_loop.set_xalign(0.0f);
    m_res_loop.set_visible(false);
    words->append(m_res_loop);
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
    // The page must never size the window. A Picture's natural size is the
    // image's (2550 x 3508 at 300 dpi, wider again once rotated), and a
    // compositor may grow the window toward it (Scott, s002: "the window kept
    // scaling larger" in continuous + rotate). A ScrolledWindow that never
    // scrolls and never propagates natural size asks for nothing; the Picture
    // just fills what it is given.
    auto frame = Gtk::make_managed<Gtk::ScrolledWindow>();
    frame->set_name("scan.preview_frame");
    frame->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::NEVER);
    frame->set_propagate_natural_width(false);
    frame->set_propagate_natural_height(false);
    frame->set_hexpand(true);
    frame->set_vexpand(true);
    frame->set_child(m_preview);
    res->append(*frame);

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
    auto rot_left = Gtk::make_managed<Gtk::Button>();
    rot_left->set_name("scan.rotate_left");
    rot_left->set_icon_name("object-rotate-left-symbolic");
    rot_left->add_css_class("ghost");
    rot_left->set_action_name("win.rotate-left");
    rot_left->set_tooltip_text("Turn this page 90 degrees left (Ctrl+[)");
    bar->append(*rot_left);
    auto rot_right = Gtk::make_managed<Gtk::Button>();
    rot_right->set_name("scan.rotate_right");
    rot_right->set_icon_name("object-rotate-right-symbolic");
    rot_right->add_css_class("ghost");
    rot_right->set_action_name("win.rotate-right");
    rot_right->set_tooltip_text("Turn this page 90 degrees right (Ctrl+])");
    bar->append(*rot_right);
    m_loop_stop.set_label("Stop");
    m_loop_stop.add_css_class("ghost");
    m_loop_stop.set_action_name("win.scan-stop");
    m_loop_stop.set_tooltip_text("End continuous scanning (Ctrl+.)");
    m_loop_stop.set_visible(false);
    bar->append(m_loop_stop);
    auto next = Gtk::make_managed<Gtk::Button>("Scan next");
    next->set_name("scan.next");
    next->add_css_class("act");
    next->set_action_name("win.scan");
    next->set_tooltip_text("Turn the page, then scan again (Ctrl+Enter); "
                           "in continuous mode, skip the rest of the wait");
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

// The quick settings: Type, Resolution, Corrections; then Preview and what it
// found; then what the scan will send, and the orb. Every control is a verb on
// m_book (one undo step each; a slider's moves fold into one).
Gtk::Widget* Shell::build_quick_panel() {  // zone: Scan -- quick settings
    auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    col->set_size_request(320, -1);
    col->set_hexpand(false);

    auto heading = [&](const char* t) {
        auto l = label(t, "card-title", 0.0f);
        l->set_margin_top(4);
        col->append(*l);
    };

    heading("Type");
    auto types = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    types->set_homogeneous(true);
    auto toggle = [&](const char* text, const char* name, model::escl::Mode m) {
        auto b = Gtk::make_managed<Gtk::ToggleButton>(text);
        b->set_name(name);
        b->add_css_class("ghost");
        b->add_css_class("mode");
        b->signal_toggled().connect([this, b, m] {
            if (!m_painting && b->get_active()) on_mode(m);
        });
        types->append(*b);
        return b;
    };
    m_type_color = toggle("Colour", "quick.type.color", model::escl::Mode::Color);
    m_type_gray  = toggle("Gray", "quick.type.gray", model::escl::Mode::Gray);
    m_type_bw    = toggle("B & W", "quick.type.bw", model::escl::Mode::BlackWhite);
    m_type_gray->set_group(*m_type_color);
    m_type_bw->set_group(*m_type_color);
    col->append(*types);

    heading("Resolution");
    m_dpi_list = Gtk::StringList::create({"300 dpi"});
    m_dpi = Gtk::make_managed<Gtk::DropDown>(m_dpi_list);
    m_dpi->set_name("quick.dpi");
    m_dpi->property_selected().signal_changed().connect([this] { on_dpi_selected(); });
    col->append(*m_dpi);

    heading("Corrections");
    auto slider = [&](widgets::Scale& sc, const char* text, auto verb) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto l = label(text, "card-sub", 0.0f);
        l->set_size_request(80, -1);
        row->append(*l);
        sc.set_range(-50, 50);
        sc.set_increments(5, 10);
        sc.set_round_digits(0);
        sc.set_draw_value(true);
        sc.set_digits(0);
        sc.set_value_pos(Gtk::PositionType::RIGHT);
        sc.set_hexpand(true);
        sc.signal_value_changed().connect([this, &sc, verb] {
            if (m_painting) return;
            (m_book.*verb)(static_cast<int>(std::lround(sc.get_value())));
            paint_plan();
        });
        row->append(sc);
        col->append(*row);
    };
    slider(m_bright, "Brightness", &model::plan::Book::set_brightness);
    slider(m_contrast, "Contrast", &model::plan::Book::set_contrast);
    slider(m_sharpen, "Sharpen", &model::plan::Book::set_sharpen);

    // File name: a base and a counter -- Book_0001.png, Book_0002.png ...
    heading("File name");
    auto name_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    m_base_entry = Gtk::make_managed<Gtk::Entry>();
    m_base_entry->set_name("quick.base");
    m_base_entry->set_placeholder_text("Base, e.g. War and Peace");
    m_base_entry->set_hexpand(true);
    m_base_entry->set_tooltip_text("Pages save as <base>_0001, <base>_0002 ... and carry on from the "
                                   "highest number already in the folder. Blank: date and time.");
    m_base_entry->signal_changed().connect([this] {
        if (m_painting) return;
        if (m_book.set_base(m_base_entry->get_text())) paint_plan();
    });
    name_row->append(*m_base_entry);
    auto digit = [&](const char* text, const char* name, int d) {
        auto b = Gtk::make_managed<Gtk::ToggleButton>(text);
        b->set_name(name);
        b->add_css_class("ghost");
        b->add_css_class("mode");
        b->set_tooltip_text(d == 3 ? "Number pages 001, 002 ..." : "Number pages 0001, 0002 ...");
        b->signal_toggled().connect([this, b, d] {
            if (!m_painting && b->get_active() && m_book.set_digits(d)) paint_plan();
        });
        name_row->append(*b);
        return b;
    };
    m_dig3 = digit("001", "quick.digits3", 3);
    m_dig4 = digit("0001", "quick.digits4", 4);
    m_dig4->set_group(*m_dig3);
    col->append(*name_row);
    m_next_name.add_css_class("card-sub");
    m_next_name.set_xalign(0.0f);
    m_next_name.set_wrap(true);
    m_next_name.set_selectable(true);
    col->append(m_next_name);

    // Rotate: every page, as it arrives (eSCL cannot; Scanit turns the image
    // before writing it).
    heading("Rotate each page");
    auto rots = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    rots->set_homogeneous(true);
    const struct { const char* text; const char* name; int turns; const char* tip; } kRot[] = {
        {"None",  "quick.rotate.none",  0, "Keep pages as the scanner sends them"},
        {"Left",  "quick.rotate.left",  3, "Turn every page 90 degrees left"},
        {"Right", "quick.rotate.right", 1, "Turn every page 90 degrees right"},
        {"180",   "quick.rotate.180",   2, "Turn every page upside down"},
    };
    for (const auto& r : kRot) {
        auto b = Gtk::make_managed<Gtk::ToggleButton>(r.text);
        b->set_name(r.name);
        b->add_css_class("ghost");
        b->add_css_class("mode");
        b->set_tooltip_text(r.tip);
        const int t = r.turns;
        b->signal_toggled().connect([this, b, t] {
            if (!m_painting && b->get_active()) on_rotate_setting(t);
        });
        if (m_rot[0]) b->set_group(*m_rot[0]);
        m_rot[t] = b;
        rots->append(*b);
    }
    col->append(*rots);

    // Continuous: scan, wait, scan again until Stop -- a book without touching
    // anything. The wait starts only when the page has fully arrived.
    heading("Continuous");
    auto cont = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_cont = Gtk::make_managed<Gtk::Switch>();
    m_cont->set_name("quick.continuous");
    m_cont->set_valign(Gtk::Align::CENTER);
    m_cont->set_tooltip_text("Scan, wait, scan again -- until you press Stop");
    m_cont->property_active().signal_changed().connect([this] {
        if (m_painting) return;
        if (m_book.set_continuous(m_cont->get_active())) paint_plan();
    });
    cont->append(*m_cont);
    auto wait = label("then wait", "card-sub", 0.0f);
    wait->set_margin_start(6);
    cont->append(*wait);
    m_pause = Gtk::make_managed<Gtk::SpinButton>();
    m_pause->set_name("quick.pause");
    m_pause->set_range(model::plan::kMinPause, model::plan::kMaxPause);
    m_pause->set_increments(1, 5);
    m_pause->set_digits(0);
    m_pause->set_numeric(true);
    m_pause->set_tooltip_text("Seconds to turn the page, counted from when the last page has arrived");
    m_pause->signal_value_changed().connect([this] {
        if (m_painting) return;
        if (m_book.set_pause(m_pause->get_value_as_int())) paint_plan();
    });
    cont->append(*m_pause);
    cont->append(*label("s between pages", "card-sub", 0.0f));
    col->append(*cont);

    // Preview: a fast pass of the whole glass, read for what is on it.
    heading("Preview");
    auto prev = Gtk::make_managed<Gtk::Button>("Preview the glass");
    prev->set_name("quick.preview");
    prev->add_css_class("ghost");
    prev->set_action_name("win.preview");
    prev->set_tooltip_text("A quick low-resolution pass: finds what is on the glass and suggests "
                           "the crop, type and resolution (Ctrl+P)");
    col->append(*prev);
    m_sugg_card.add_css_class("card");
    m_sugg_head.add_css_class("card-title");
    m_sugg_head.set_xalign(0.0f);
    m_sugg_head.set_wrap(true);
    m_sugg_text.add_css_class("card-sub");
    m_sugg_text.set_xalign(0.0f);
    m_sugg_text.set_wrap(true);
    m_sugg_text.set_selectable(true);
    m_use_sugg.set_label("Use suggestion");
    m_use_sugg.add_css_class("ghost");
    m_use_sugg.add_css_class("small");
    m_use_sugg.set_halign(Gtk::Align::START);
    m_use_sugg.set_action_name("win.use-suggestion");
    m_sugg_card.append(m_sugg_head);
    m_sugg_card.append(m_sugg_text);
    m_sugg_card.append(m_use_sugg);
    m_sugg_card.set_visible(false);
    col->append(m_sugg_card);

    // Below the scroll, always in view: what the scan will send, and the orb.
    auto outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    outer->set_size_request(320, -1);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    scroll->set_child(*col);
    outer->append(*scroll);
    col = outer;

    m_scan_sub.add_css_class("hero-caption");
    m_scan_sub.set_wrap(true);
    m_scan_sub.set_xalign(0.0f);
    col->append(m_scan_sub);
    m_orb.set_label("Scan");
    m_orb.add_css_class("orb");
    m_orb.set_halign(Gtk::Align::CENTER);
    m_orb.set_margin_top(8);
    m_orb.set_action_name("win.scan");
    m_orb.set_tooltip_text("Scan what is inside the crop box (Ctrl+Enter)");
    col->append(m_orb);
    m_scan_err.add_css_class("stop");
    m_scan_err.set_wrap(true);
    m_scan_err.set_max_width_chars(40);
    m_scan_err.set_selectable(true);
    col->append(m_scan_err);
    return col;
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    // Undo / Redo name the step ("Undo Resolution 600 dpi"); relabel_undo()
    // refills this section whenever the journal changes.
    m_undo_menu = Gio::Menu::create();
    menu->append_section(m_undo_menu);

    auto scan = Gio::Menu::create();
    scan->append("Preview the glass", "win.preview");
    scan->append("Crop to the whole glass", "win.whole-glass");
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
