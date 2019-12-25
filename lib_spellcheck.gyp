# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

{
  'includes': [
    '../gyp/helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_spellcheck',
    'includes': [
      '../gyp/helpers/common/library.gypi',
      '../gyp/helpers/modules/qt.gypi',
      '../gyp/helpers/modules/pch.gypi',
    ],
    'variables': {
      'src_loc': '.',
      'list_sources_command': 'python gyp/list_sources.py --input gyp/sources.txt --replace src_loc=<(src_loc)',
      'pch_source': '<(src_loc)/spellcheck/spellcheck_pch.cpp',
      'pch_header': '<(src_loc)/spellcheck/spellcheck_pch.h',
      'style_files': [
        '<(submodules_loc)/lib_ui/ui/colors.palette',
      ],
    },
    'defines': [
    ],
    'dependencies': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      '<(submodules_loc)/lib_ui/lib_ui.gyp:lib_ui',
      '<(submodules_loc)/lib_crl/lib_crl.gyp:lib_crl',
    ],
    'export_dependent_settings': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      '<(submodules_loc)/lib_ui/lib_ui.gyp:lib_ui',
      '<(submodules_loc)/lib_crl/lib_crl.gyp:lib_crl',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(submodules_loc)/lib_rpl',
      '<(libs_loc)/range-v3/include',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
        '<(src_loc)',
        '<(SHARED_INTERMEDIATE_DIR)',
        '<(libs_loc)/range-v3/include',
        '<(submodules_loc)/lib_rpl',
      ],
    },
    'sources': [
      'gyp/sources.txt',
      '<!@(<(list_sources_command))',
    ],
    'sources!': [
      '<!@(<(list_sources_command) --exclude_for <(build_os))',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }], [ 'build_linux', {
      'cflags_cc': [
        '<!(pkg-config --silence-errors --cflags enchant-2 || pkg-config --cflags enchant)',
      ],
    }]],
  }],
}