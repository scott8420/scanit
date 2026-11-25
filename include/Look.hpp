#pragma once
// Look.hpp -- the switch for the look stone (s002), and its install step.
//
// Opt-in. kLookDefault decides at build time; SCANIT_LOOK=0 / =1 in the
// environment overrides it at run time, so both shells can be seen without a
// rebuild. With the look ON: look.css is loaded, GTK is asked for its dark
// variant (the skin's colours are designed on a dark ground), the window gets
// .look, and the Shell builds the custom sidebar instead of the StackSidebar.
// With it OFF: none of that -- Appearance follows the desktop as before.
//
// An app seeded from Scanit renames the env var with the app (TIDY_LOOK) and
// the resource path with the app id. docs/the-look.md.

namespace scanit::look {

inline constexpr bool kLookDefault = true;

// kLookDefault, unless SCANIT_LOOK says otherwise. Read once, then cached.
bool enabled();

// With the look on: ask GTK for dark and load look.css at APPLICATION
// priority. Call before the first window exists. A no-op with it off.
void install();

}  // namespace scanit::look
