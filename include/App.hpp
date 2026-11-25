#pragma once
#include <gtkmm/application.h>
#include <memory>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate (or on the first action that needs it). (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace scanit {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
    void on_startup() override;   // accelerators, driven from model::shortcut_registry()

private:
    Shell& ensure_shell();        // one window, ever: built on first need, then reused

    Shell* m_shell = nullptr;   // owned by the application via add_window
};

}  // namespace scanit
