#include "Look.hpp"
#include "Log.hpp"

#include <gdkmm/display.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/settings.h>
#include <gtkmm/stylecontext.h>

#include <cstdlib>
#include <string>

// Look.cpp -- reading the switch, loading the skin. The skin itself is
// resources/look.css (compiled into the gresource).

namespace scanit::look {

bool enabled() {
    static const bool on = [] {
        if (const char* v = std::getenv("SCANIT_LOOK")) {
            const std::string s(v);
            if (s == "0" || s == "off" || s == "no")  return false;
            if (s == "1" || s == "on"  || s == "yes") return true;
        }
        return kLookDefault;
    }();
    return on;
}

void install() {
    if (!enabled()) return;
    if (auto settings = Gtk::Settings::get_default())
        settings->property_gtk_application_prefer_dark_theme() = true;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->signal_parsing_error().connect(
        [](const Glib::RefPtr<const Gtk::CssSection>& sec, const Glib::Error& e) {
            if (auto lg = log::get(log::Area::App))
                lg->warn("look.css:{}: {}", sec ? sec->get_start_location().get_lines() + 1 : 0,
                         e.what());
        });
    css->load_from_resource("/io/github/scott8420/Scanit/look.css");
    Gtk::StyleContext::add_provider_for_display(display, css,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (auto lg = log::get(log::Area::App)) lg->info("the look: on");
}

}  // namespace scanit::look
