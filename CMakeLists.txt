add_library(lib_spellcheck OBJECT)
init_target(lib_spellcheck)

get_filename_component(src_loc . REALPATH)

set(lib_spellcheck_sources
PRIVATE
    spellcheck/platform/platform_spellcheck.h
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
)
nice_target_sources(lib_spellcheck ${src_loc} "${lib_spellcheck_sources}")

target_precompile_headers(lib_spellcheck PRIVATE ${src_loc}/spellcheck/spellcheck_pch.h)

target_include_directories(lib_spellcheck
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_spellcheck
PUBLIC
    lib_ui
)
