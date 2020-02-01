# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(lib_spellcheck OBJECT)
add_library(desktop-app::lib_spellcheck ALIAS lib_spellcheck)
init_target(lib_spellcheck)

get_filename_component(src_loc . REALPATH)

target_precompile_headers(lib_spellcheck PRIVATE ${src_loc}/spellcheck/spellcheck_pch.h)
nice_target_sources(lib_spellcheck ${src_loc}
PRIVATE
    spellcheck/platform/platform_spellcheck.h
    spellcheck/platform/linux/linux_enchant.cpp
    spellcheck/platform/linux/linux_enchant.h
    spellcheck/platform/linux/spellcheck_linux.cpp
    spellcheck/platform/linux/spellcheck_linux.h
    spellcheck/platform/mac/spellcheck_mac.h
    spellcheck/platform/mac/spellcheck_mac.mm
    spellcheck/platform/win/spellcheck_win.cpp
    spellcheck/platform/win/spellcheck_win.h
    spellcheck/spellcheck_utils.cpp
    spellcheck/spellcheck_utils.h
    spellcheck/spellcheck_types.h
    spellcheck/spelling_highlighter.cpp
    spellcheck/spelling_highlighter.h
    spellcheck/spellcheck_hunspell.cpp
    spellcheck/spellcheck_hunspell.h
    spellcheck/spellcheck_value.cpp
    spellcheck/spellcheck_value.h
)

target_include_directories(lib_spellcheck
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_spellcheck
PUBLIC
    desktop-app::lib_ui
    desktop-app::external_hunspell
)

if (LINUX)
    find_package(PkgConfig REQUIRED)

    pkg_search_module(ENCHANT REQUIRED enchant-2 enchant)
    target_include_directories(lib_spellcheck PRIVATE ${ENCHANT_INCLUDE_DIRS})
endif()
