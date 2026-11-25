#include "Shell.hpp"

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
}

}  // namespace scanit
