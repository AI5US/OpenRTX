# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

# PFFFT, FFT library used by mbelib-neo. Only the single precision scalar
# implementation is built, as done by mbelib-neo's own build.
project('pffft', 'c')

pffft_args = ['-DPFFFT_STATIC_DEFINE', '-DPFFFT_SIMD_DISABLE=1', '-w']

pffft_lib = static_library('pffft',
                           ['src/pffft.c', 'src/pffft_common.c'],
                           include_directories : include_directories('include/pffft', 'src'),
                           c_args              : pffft_args,
                           install             : false)

pffft_dep = declare_dependency(include_directories : include_directories('include/pffft'),
                               compile_args        : ['-DPFFFT_STATIC_DEFINE'],
                               link_with           : pffft_lib)
