#include "Shell.hpp"

#include <gtkmm/eventcontrollerkey.h>

// Shell_bindings.cpp -- BINDINGS. The win.* actions. Accelerators come from
// model::shortcut_registry() (App::on_startup), not from here.

namespace scanit {

void Shell::bind_actions() {  // bindings: win.* actions
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("quit",          [this]() { close(); });

    add_action("scan",          sigc::mem_fun(*this, &Shell::on_scan));
    add_action("scan-stop",     sigc::mem_fun(*this, &Shell::on_scan_stop));
    add_action("scan-again",    sigc::mem_fun(*this, &Shell::on_scan_again));
    add_action("open-folder",   sigc::mem_fun(*this, &Shell::on_open_folder));
    add_action("find-scanner",  sigc::mem_fun(*this, &Shell::on_find));
    add_action("use-address",   sigc::mem_fun(*this, &Shell::on_use_address));
    add_action("preview",       sigc::mem_fun(*this, &Shell::on_preview));
    add_action("use-suggestion",sigc::mem_fun(*this, &Shell::on_use_suggestion));
    add_action("whole-glass",   sigc::mem_fun(*this, &Shell::on_whole_glass));
    add_action("rotate-left",   [this] { on_rotate_page(-1); });
    add_action("rotate-right",  [this] { on_rotate_page(+1); });
    add_action("undo",          sigc::mem_fun(*this, &Shell::on_undo));
    add_action("redo",          sigc::mem_fun(*this, &Shell::on_redo));

    // Ctrl+Z is NOT an accelerator (an accel fires before the focused widget
    // and takes undo away from every text box -- jot s046a). The window hears
    // it in the BUBBLE phase: only keys nothing else took.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    keys->signal_key_pressed().connect(sigc::mem_fun(*this, &Shell::on_key), false);
    add_controller(keys);
}

}  // namespace scanit
