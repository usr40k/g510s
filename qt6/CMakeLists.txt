cmake_minimum_required(VERSION 3.21)
project(g510s-qt LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 DBus)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# g510score - the GTK-free C core (clock, config, keys, list, misc, net,
# presets, threads). Shared verbatim with the GTK3 build in the parent dir.
# Built as a separate C target so the .c files are never compiled as C++.
# ---------------------------------------------------------------------------
add_library(g510score STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-clock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-misc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-presets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-threads.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../g510s-vars.c
)

target_include_directories(g510score PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The C core still relies on -fcommon-style tentative definitions in places.
target_compile_options(g510score PRIVATE -fcommon)
target_link_libraries(g510score PUBLIC Threads::Threads m)

# libg15 / libg15render are plain C libraries and ship no pkg-config files,
# so discover them by path. Hints come from LIBG15_PREFIX/LIBG15RENDER_PREFIX
# (set by the Nix build) or from the system defaults.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_path(LIBG15_INCLUDE_DIR NAMES libg15.h
    HINTS ${LIBG15_PREFIX} ENV LIBG15_PREFIX
    PATH_SUFFIXES include)
find_library(LIBG15_LIBRARY NAMES g15
    HINTS ${LIBG15_PREFIX} ENV LIBG15_PREFIX
    PATH_SUFFIXES lib)

find_path(LIBG15RENDER_INCLUDE_DIR NAMES libg15render.h
    HINTS ${LIBG15RENDER_PREFIX} ENV LIBG15RENDER_PREFIX
    PATH_SUFFIXES include)
find_library(LIBG15RENDER_LIBRARY NAMES g15render
    HINTS ${LIBG15RENDER_PREFIX} ENV LIBG15RENDER_PREFIX
    PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibG15
    REQUIRED_VARS LIBG15_LIBRARY LIBG15_INCLUDE_DIR)
find_package_handle_standard_args(LibG15Render
    REQUIRED_VARS LIBG15RENDER_LIBRARY LIBG15RENDER_INCLUDE_DIR)

target_include_directories(g510score PUBLIC
    ${LIBG15_INCLUDE_DIR}
    ${LIBG15RENDER_INCLUDE_DIR}
)
target_link_libraries(g510score PUBLIC ${LIBG15_LIBRARY} ${LIBG15RENDER_LIBRARY})

# ---------------------------------------------------------------------------
# g510s-qt - the Qt6/QML frontend
# ---------------------------------------------------------------------------
qt_add_executable(g510s-qt
    main.cpp
    backend.cpp
    backend.h
    syntaxhighlighter.cpp
    syntaxhighlighter.h
    dbusadaptor.cpp
    dbusadaptor.h
)

target_include_directories(g510s-qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(g510s-qt PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    g510score
)

qt_add_qml_module(g510s-qt
    URI G510s
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/BankPage.qml
        qml/ColorPicker.qml
        qml/DisplayPage.qml
)

# Where the tray icons live (overridden by the Nix build).
target_compile_definitions(g510s-qt PRIVATE
    G510S_SHARE_DIR="${G510S_SHARE_DIR}"
)

install(TARGETS g510s-qt RUNTIME DESTINATION bin)