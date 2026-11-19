# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

# mbelib-neo, IMBE/AMBE vocoder library. Provides the software AMBE 3600x2400
# (D-STAR) and AMBE+2 3600x2450 (DMR) encoders and decoders. The TETRA
# decoder is not built.
project('mbelib-neo', 'c', version : '2.6.11')

pffft_dep = subproject('pffft').get_variable('pffft_dep')

# Generates include/mbelib-neo/version.h
subdir('include/mbelib-neo')

mbelib_src = ['src/ambe/ambe3600x2400.c',
              'src/ambe/ambe3600x2400_enc.c',
              'src/ambe/dstar_frame.c',
              'src/ambe/ambe3600x2450.c',
              'src/ambe/ambe3600x2450_enc.c',
              'src/ambe/ambe_common.c',
              'src/ambe/ambe_encoder.c',
              'src/ecc/ecc_soft.c',
              'src/ecc/ecc_soft_search.c',
              'src/ecc/ecc.c',
              'src/ecc/ecc_const.c',
              'src/imbe/imbe7100x4400.c',
              'src/imbe/imbe7200x4400.c',
              'src/imbe/imbe7200x4400_enc.c',
              'src/core/mbelib.c',
              'src/core/mbe_cpu.c',
              'src/core/mbe_voiced.c',
              'src/core/mbe_pcm_convert.c',
              'src/core/mbe_adaptive.c',
              'src/core/mbe_encoder.c',
              'src/core/mbe_speech_analysis.c',
              'src/core/mbe_subframe.c',
              'src/core/mbe_tone_detect.c',
              'src/core/mbe_tone_fit.c',
              'src/core/mbe_unvoiced_fft.c']

mbelib_args = ['-DMBE_STATIC=1', '-DPFFFT_STATIC_DEFINE', '-w']

# The vocoder is used from a single thread: on embedded targets, where
# thread-local storage may not be available, use plain static variables.
if meson.is_cross_build()
  mbelib_args += ['-include', meson.current_source_dir() / 'mbe_single_thread.h']
  # Single precision maths: Cortex-M FPUs have no double precision
  mbelib_args += ['-DMBE_FLOAT_MATH', '-fsingle-precision-constant']
endif

mbelib_inc = include_directories('include')

mbelib_lib = static_library('mbe-neo',
                            mbelib_src,
                            include_directories : [mbelib_inc,
                                                   include_directories('.', 'src/internal')],
                            c_args              : mbelib_args,
                            dependencies        : pffft_dep,
                            install             : false)

mbelib_dep = declare_dependency(include_directories : mbelib_inc,
                                compile_args        : ['-DMBE_STATIC=1'],
                                link_with           : mbelib_lib,
                                dependencies        : pffft_dep)
