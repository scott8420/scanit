cmake_minimum_required(VERSION 3.20)
project(scanit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Warnings on everywhere: s002 shipped a constexpr copy of a std::string that
# GCC 13/14 let through and GCC 16 (Fedora) refused. Built clean with these on
# under GCC 13, GCC 14 and clang 18.
# C++ only: the generated gresource C file trips -Woverlength-strings.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")

# Deps -- Fedora: gtkmm4.0-devel spdlog-devel libsoup3-devel avahi-devel cmake gcc-c++
#         Ubuntu: libgtkmm-4.0-dev libspdlog-dev libsoup-3.0-dev libavahi-client-dev cmake g++
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
pkg_check_modules(SOUP  REQUIRED libsoup-3.0)
pkg_check_modules(AVAHI REQUIRED avahi-client)
find_package(spdlog REQUIRED)

# ── scanit_model -- the GTK-free, network-free model ─────────────────────────
# No gtkmm and no network on this library's link line, BY DESIGN: a stray UI
# include under model/ fails to compile, so the seam is enforced by the build
# graph and the model stays headless-testable. (Cairn: two-layer-seam.md.)
add_library(scanit_model STATIC
    # eSCL as data: capabilities in, scan settings out, job URLs.
    src/model/Escl.cpp
    # Where a scan lands and what it is called.
    src/model/Output.cpp
    # What the next scan sends: quick settings + crop, through the undo door.
    src/model/Plan.cpp
    # Reading the preview: content box, pages, orientation, size, best type.
    src/model/Detect.cpp
    # The look (from Cairn s002): page states, the sidebar as data, tints.
    src/model/Look.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels,
    # ShortcutsDialog renders), checked collision-free by the selftest.
    src/model/Shortcuts.cpp
)
target_include_directories(scanit_model PUBLIC include)

# ── scanit_io -- the network: mDNS discovery (Avahi), eSCL over HTTP (soup) ──
# Still no GTK. Blocking calls, run on the Shell's worker thread.
add_library(scanit_io STATIC
    src/io/Discovery.cpp
    src/io/EsclClient.cpp
    src/Log.cpp
)
target_include_directories(scanit_io PUBLIC include ${SOUP_INCLUDE_DIRS} ${AVAHI_INCLUDE_DIRS})
target_link_libraries(scanit_io PUBLIC scanit_model spdlog::spdlog ${SOUP_LIBRARIES} ${AVAHI_LIBRARIES})
target_link_directories(scanit_io PUBLIC ${SOUP_LIBRARY_DIRS} ${AVAHI_LIBRARY_DIRS})

# ── Headless selftest -- the model, no display, no network ───────────────────
add_executable(scanit_selftest src/model/selftest.cpp)
target_link_libraries(scanit_selftest PRIVATE scanit_model)
# The selftest reads resources/look.css to check every tint is defined.
target_compile_definitions(scanit_selftest PRIVATE SCANIT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ── App resources (gresource) ────────────────────────────────────────────────
# glib-compile-resources embeds resources/scanit.gresource.xml into a C source;
# App registers it and points the icon theme at the bundle. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(SCANIT_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/scanit.gresource.xml")
set(SCANIT_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/scanit-resources.c")
file(GLOB SCANIT_RESOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg")
add_custom_command(
    OUTPUT  "${SCANIT_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${SCANIT_RESOURCES_C}"
            --generate-source
            --c-name scanit_resources
            scanit.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${SCANIT_GRESOURCE_XML}" ${SCANIT_RESOURCE_FILES}
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/look.css"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/io.github.scott8420.Scanit.svg"
    COMMENT "Compiling gresource bundle (scanit-resources.c)"
    VERBATIM)

# ── scanit -- the GTK app ────────────────────────────────────────────────────
add_executable(scanit
    ${SCANIT_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    src/Appearance.cpp
    src/Look.cpp
    src/Shell.cpp
    src/CropView.cpp
    src/Rotate.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
)
target_include_directories(scanit PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(scanit     PRIVATE ${GTKMM_LIBRARIES} scanit_io spdlog::spdlog)
target_link_directories(scanit   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(scanit    PRIVATE ${GTKMM_CFLAGS_OTHER})
