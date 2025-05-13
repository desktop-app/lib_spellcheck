# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(lib_spellcheck STATIC)
add_library(desktop-app::lib_spellcheck ALIAS lib_spellcheck)
init_target(lib_spellcheck)

get_filename_component(src_loc . REALPATH)

set(use_enchant 0)
if (LINUX AND DESKTOP_APP_USE_ENCHANT)
    set(use_enchant 1)
endif()

set(system_spellchecker 0)
if ((APPLE OR WIN32 OR use_enchant) AND NOT DESKTOP_APP_USE_HUNSPELL_ONLY)
    set(system_spellchecker 1)
endif()

if (APPLE)
    target_link_frameworks_weak(lib_spellcheck
    PUBLIC
        NaturalLanguage # Since macOS 10.14 only.
    )
endif()

set(use_cld3 0)
if (LINUX OR DESKTOP_APP_USE_CLD3)
    set(use_cld3 1)
endif()

target_precompile_headers(lib_spellcheck PRIVATE ${src_loc}/spellcheck/spellcheck_pch.h)
nice_target_sources(lib_spellcheck ${src_loc}
PRIVATE
    highlighting/highlighting.qrc

    spellcheck/platform/linux/language_linux.cpp
    spellcheck/platform/linux/language_linux.h
    spellcheck/platform/linux/linux_enchant.cpp
    spellcheck/platform/linux/linux_enchant.h
    spellcheck/platform/linux/spellcheck_linux.cpp
    spellcheck/platform/linux/spellcheck_linux.h
    spellcheck/platform/mac/language_mac.h
    spellcheck/platform/mac/language_mac.mm
    spellcheck/platform/mac/spellcheck_mac.h
    spellcheck/platform/mac/spellcheck_mac.mm
    spellcheck/platform/win/language_win.cpp
    spellcheck/platform/win/language_win.h
    spellcheck/platform/win/spellcheck_win.cpp
    spellcheck/platform/win/spellcheck_win.h
    spellcheck/platform/platform_language.h
    spellcheck/platform/platform_spellcheck.h
    spellcheck/third_party/language_cld3.cpp
    spellcheck/third_party/language_cld3.h
    spellcheck/third_party/hunspell_controller.cpp
    spellcheck/third_party/hunspell_controller.h
    spellcheck/third_party/spellcheck_hunspell.cpp
    spellcheck/third_party/spellcheck_hunspell.h
    spellcheck/spellcheck_types.h
    spellcheck/spellcheck_highlight_syntax.cpp
    spellcheck/spellcheck_highlight_syntax.h
    spellcheck/spellcheck_utils.cpp
    spellcheck/spellcheck_utils.h
    spellcheck/spellcheck_value.cpp
    spellcheck/spellcheck_value.h
    spellcheck/spelling_highlighter.cpp
    spellcheck/spelling_highlighter.h
    spellcheck/spelling_highlighter_helper.cpp
    spellcheck/spelling_highlighter_helper.h

    spellcheck/spellcheck_pch.h
)

target_prepare_qrc(lib_spellcheck)

if (system_spellchecker)
    remove_target_sources(lib_spellcheck ${src_loc}
        spellcheck/third_party/spellcheck_hunspell.cpp
        spellcheck/third_party/spellcheck_hunspell.h
    )
else()
    remove_target_sources(lib_spellcheck ${src_loc}
        spellcheck/platform/linux/linux_enchant.cpp
        spellcheck/platform/linux/linux_enchant.h
        spellcheck/platform/linux/spellcheck_linux.cpp
        spellcheck/platform/linux/spellcheck_linux.h
        spellcheck/platform/mac/spellcheck_mac.h
        spellcheck/platform/mac/spellcheck_mac.mm
        spellcheck/platform/win/spellcheck_win.cpp
        spellcheck/platform/win/spellcheck_win.h
    )
endif()

if (use_cld3)
    target_link_libraries(lib_spellcheck PRIVATE desktop-app::external_cld3)
    remove_target_sources(lib_spellcheck ${src_loc}
        spellcheck/platform/linux/language_linux.cpp
        spellcheck/platform/linux/language_linux.h
        spellcheck/platform/mac/language_mac.h
        spellcheck/platform/mac/language_mac.mm
        spellcheck/platform/win/language_win.cpp
        spellcheck/platform/win/language_win.h
    )
else()
    remove_target_sources(lib_spellcheck ${src_loc}
        spellcheck/third_party/language_cld3.cpp
        spellcheck/third_party/language_cld3.h
    )
endif()

# We should support both types of spellchecker for Windows.
if (WIN32 OR NOT system_spellchecker)
    target_link_libraries(lib_spellcheck PRIVATE desktop-app::external_hunspell)
else()
    remove_target_sources(lib_spellcheck ${src_loc}
        spellcheck/third_party/hunspell_controller.cpp
        spellcheck/third_party/hunspell_controller.h
    )
endif()

target_include_directories(lib_spellcheck
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_spellcheck
PRIVATE
    desktop-app::lib_ui
    desktop-app::lib_base
    desktop-app::lib_rpl
    desktop-app::lib_crl
    desktop-app::lib_prisma
    desktop-app::external_qt
    desktop-app::external_ranges
    desktop-app::external_gsl
    desktop-app::external_xxhash
)

if (LINUX AND use_enchant)
    find_package(PkgConfig REQUIRED)

    pkg_search_module(DESKTOP_APP_ENCHANT REQUIRED enchant-2 enchant)
    target_include_directories(lib_spellcheck SYSTEM PRIVATE ${DESKTOP_APP_ENCHANT_INCLUDE_DIRS})
endif()
