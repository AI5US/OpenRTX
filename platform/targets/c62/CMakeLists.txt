# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

#
# CMake configuration file for the Retevis C62 OpenRTX target
#
cmake_minimum_required(VERSION 3.20.0)

zephyr_compile_definitions(PSRAM_CHIP_SIZE=4) # 4 MiB PSRAM

target_sources(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/mcu/CSK6011B/drivers/delays.c

    ${OPENRTX_ROOT}/platform/drivers/GPIO/gpio_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/GPIO/gpio_bk4819.c
    ${OPENRTX_ROOT}/platform/drivers/SPI/spi_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/SPI/spi_zephyr_bus.c
    ${OPENRTX_ROOT}/platform/drivers/I2C/i2c_bitbang.c

    ${OPENRTX_ROOT}/platform/drivers/baseband/BK4819.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/BK1080.c

    ${OPENRTX_ROOT}/platform/drivers/NVM/nvmem_C62.c
    ${OPENRTX_ROOT}/platform/targets/c62/calib_C62.c
    ${OPENRTX_ROOT}/platform/drivers/stubs/cps_io_stub.c

    ${OPENRTX_ROOT}/platform/drivers/display/display_c62.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/radio_C62.cpp
    ${OPENRTX_ROOT}/platform/drivers/keyboard/keyboard_c62.c
    ${OPENRTX_ROOT}/platform/drivers/audio/audio_c62.c
    ${OPENRTX_ROOT}/platform/drivers/audio/csk6_codec.c

    ${OPENRTX_ROOT}/platform/targets/c62/platform.c
    ${OPENRTX_ROOT}/platform/targets/c62/hwconfig.c
    ${OPENRTX_ROOT}/platform/targets/c62/dsp.c
    ${OPENRTX_ROOT}/platform/targets/c62/lsf.c
)

add_subdirectory(services)
zephyr_library_link_libraries(lsf_client)
zephyr_linker_sources(DATA_SECTIONS lsf_service.ld)

target_include_directories(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/targets/c62
)

target_compile_definitions(app PRIVATE PLATFORM_C62)

# C62_DSTAR: D-STAR support, with the software AMBE vocoder of mbelib-neo,
#   which west fetches to modules/lib/mbelib-neo (and PFFFT, its FFT, to
#   modules/lib/pffft).
option(C62_DSTAR "D-STAR support" OFF)
# C62_DMR: DMR direct mode voice, with the software AMBE+2 vocoder of
#   mbelib-neo (fetched as for C62_DSTAR).
option(C62_DMR "DMR support" OFF)

# mbelib-neo, the vocoder library shared by D-STAR and DMR
if(C62_DSTAR OR C62_DMR)
  set(C62_MBELIB ${WEST_TOPDIR}/modules/lib/mbelib-neo)
  set(C62_PFFFT ${WEST_TOPDIR}/modules/lib/pffft)
  if(NOT EXISTS ${C62_MBELIB}/src/ambe/ambe3600x2400_enc.c
     OR NOT EXISTS ${C62_PFFFT}/src/pffft.c)
    message(FATAL_ERROR "mbelib-neo or PFFFT not found in ${WEST_TOPDIR}/"
                        "modules/lib: run 'west update --group-filter +csk6', "
                        "or build with -DC62_DSTAR=OFF -DC62_DMR=OFF")
  endif()

  # mbelib-neo's own build downloads PFFFT, build the sources here instead.
  # Same source list as subprojects/packagefiles/mbelib-neo/meson.build: the
  # AMBE and IMBE vocoders, not the TETRA decoder.
  # version.h, without touching this project's PROJECT_VERSION
  file(READ ${C62_MBELIB}/include/mbelib-neo/version.h.in c62_mbelib_version)
  string(REPLACE "@PROJECT_VERSION@" "2.6.11" c62_mbelib_version
         "${c62_mbelib_version}")
  file(CONFIGURE
       OUTPUT ${CMAKE_BINARY_DIR}/mbelib-neo/include/mbelib-neo/version.h
       CONTENT "${c62_mbelib_version}" @ONLY)
  add_library(mbe_neo STATIC
    ${C62_MBELIB}/src/ambe/ambe3600x2400.c
    ${C62_MBELIB}/src/ambe/ambe3600x2400_enc.c
    ${C62_MBELIB}/src/ambe/dstar_frame.c
    ${C62_MBELIB}/src/ambe/ambe3600x2450.c
    ${C62_MBELIB}/src/ambe/ambe3600x2450_enc.c
    ${C62_MBELIB}/src/ambe/ambe_common.c
    ${C62_MBELIB}/src/ambe/ambe_encoder.c
    ${C62_MBELIB}/src/ecc/ecc_soft.c
    ${C62_MBELIB}/src/ecc/ecc_soft_search.c
    ${C62_MBELIB}/src/ecc/ecc.c
    ${C62_MBELIB}/src/ecc/ecc_const.c
    ${C62_MBELIB}/src/imbe/imbe7100x4400.c
    ${C62_MBELIB}/src/imbe/imbe7200x4400.c
    ${C62_MBELIB}/src/imbe/imbe7200x4400_enc.c
    ${C62_MBELIB}/src/core/mbelib.c
    ${C62_MBELIB}/src/core/mbe_cpu.c
    ${C62_MBELIB}/src/core/mbe_voiced.c
    ${C62_MBELIB}/src/core/mbe_pcm_convert.c
    ${C62_MBELIB}/src/core/mbe_adaptive.c
    ${C62_MBELIB}/src/core/mbe_encoder.c
    ${C62_MBELIB}/src/core/mbe_speech_analysis.c
    ${C62_MBELIB}/src/core/mbe_subframe.c
    ${C62_MBELIB}/src/core/mbe_tone_detect.c
    ${C62_MBELIB}/src/core/mbe_tone_fit.c
    ${C62_MBELIB}/src/core/mbe_unvoiced_fft.c
    ${C62_PFFFT}/src/pffft.c
    ${C62_PFFFT}/src/pffft_common.c)
  # Build it with the Zephyr compiler flags (CPU, FPU, ABI)
  target_link_libraries(mbe_neo PRIVATE zephyr_interface)
  target_include_directories(mbe_neo
    PUBLIC
      ${C62_MBELIB}/include
      ${CMAKE_BINARY_DIR}/mbelib-neo/include
    PRIVATE
      ${C62_MBELIB}
      ${C62_MBELIB}/src/internal
      ${C62_PFFFT}/include/pffft
      ${C62_PFFFT}/src)
  # Zephyr builds C as C99: _DEFAULT_SOURCE makes newlib's math.h define
  # M_PI and M_SQRT2, used by mbelib-neo
  target_compile_definitions(mbe_neo
    PUBLIC MBE_STATIC=1
    PRIVATE PFFFT_STATIC_DEFINE PFFFT_SIMD_DISABLE=1 _DEFAULT_SOURCE)
  # Single thread use, no thread-local storage. Allocations in PSRAM, the
  # encoders do not fit in the SRAM malloc arena (see mbe_psram_heap.h).
  target_compile_options(mbe_neo PRIVATE -w
    "SHELL:-include ${OPENRTX_ROOT}/subprojects/packagefiles/mbelib-neo/mbe_single_thread.h"
    "SHELL:-include ${OPENRTX_ROOT}/platform/targets/c62/mbe_psram_heap.h")

  # mbelib-neo in single precision, the FPU has no double precision (see
  # mbe_float_math.h). Not PFFFT, which computes in single precision.
  get_target_property(c62_mbe_sources mbe_neo SOURCES)
  list(FILTER c62_mbe_sources INCLUDE REGEX "/mbelib-neo/")
  set_source_files_properties(${c62_mbe_sources} PROPERTIES
    COMPILE_DEFINITIONS MBE_FLOAT_MATH
    COMPILE_OPTIONS -fsingle-precision-constant)

  target_link_libraries(app PRIVATE mbe_neo)
  target_sources(app PRIVATE ${OPENRTX_ROOT}/platform/targets/c62/mbe_heap.c)
endif()

if(C62_DSTAR)
  target_sources(app PRIVATE
    ${OPENRTX_ROOT}/openrtx/src/core/dstar_vocoder.c
    ${OPENRTX_ROOT}/openrtx/src/protocols/DSTAR/Header.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DSTAR/HeaderCoder.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DSTAR/SlowData.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DSTAR/Modulator.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DSTAR/Demodulator.cpp
    ${OPENRTX_ROOT}/openrtx/src/rtx/OpMode_DSTAR.cpp)
  target_compile_definitions(app PRIVATE CONFIG_DSTAR)
endif()

if(C62_DMR)
  target_sources(app PRIVATE
    ${OPENRTX_ROOT}/openrtx/src/core/dmr_vocoder.c
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Fec.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Burst.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Lc.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Voice.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Cach.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Csbk.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Modulator.cpp
    ${OPENRTX_ROOT}/openrtx/src/protocols/DMR/Demodulator.cpp
    ${OPENRTX_ROOT}/openrtx/src/rtx/OpMode_DMR.cpp)
  target_compile_definitions(app PRIVATE CONFIG_DMR)
endif()

message(STATUS "C62: D-STAR ${C62_DSTAR}, DMR ${C62_DMR}")

#
# DSP firmware, flashed separately at 0x100000 (dsp_firmware partition). The
# firmware does not boot without it, and it must match the audio code, so the
# build pins one image by hash: by default it is downloaded from OpenRTX's
# lsf-zephyr-sdk, or C62_DSP_FIRMWARE (CMake or environment variable) gives a
# local copy for offline builds. Either way it ends up next to zephyr.bin.
#
set(C62_DSP_FIRMWARE_URL
    "https://raw.githubusercontent.com/OpenRTX/lsf-zephyr-sdk/2ac605527dcde3e90c814bb4175546cc7fbd36da/resources/dsp_firmware.bin")
set(C62_DSP_FIRMWARE_SHA256
    "9579b617f048c646465118f6d0b9e1b765b4d7d811ff9c8649c5754fe45a8903")
set(C62_DSP_FIRMWARE_OUT ${CMAKE_BINARY_DIR}/zephyr/dsp_firmware.bin)

if(NOT DEFINED C62_DSP_FIRMWARE AND DEFINED ENV{C62_DSP_FIRMWARE})
    set(C62_DSP_FIRMWARE $ENV{C62_DSP_FIRMWARE})
endif()

# A relative path is from the OpenRTX root. Both branches check the hash
# themselves: with EXPECTED_HASH, a failed file(DOWNLOAD) stops with CMake's
# own error before the hint below.
if(C62_DSP_FIRMWARE)
    get_filename_component(c62_dsp_local "${C62_DSP_FIRMWARE}" ABSOLUTE
                           BASE_DIR "${OPENRTX_ROOT}")
    if(EXISTS "${c62_dsp_local}")
        file(SHA256 "${c62_dsp_local}" c62_dsp_local_sha256)
    endif()
    if(NOT "${c62_dsp_local_sha256}" STREQUAL "${C62_DSP_FIRMWARE_SHA256}")
        message(FATAL_ERROR "C62_DSP_FIRMWARE ${c62_dsp_local} is missing or "
                            "is not the pinned DSP firmware (sha256 "
                            "${C62_DSP_FIRMWARE_SHA256}).")
    endif()
    configure_file("${c62_dsp_local}" "${C62_DSP_FIRMWARE_OUT}" COPYONLY)
else()
    # Skipped when the file is already there with the expected hash
    if(EXISTS "${C62_DSP_FIRMWARE_OUT}")
        file(SHA256 "${C62_DSP_FIRMWARE_OUT}" c62_dsp_sha256)
    endif()
    if(NOT "${c62_dsp_sha256}" STREQUAL "${C62_DSP_FIRMWARE_SHA256}")
        file(DOWNLOAD "${C62_DSP_FIRMWARE_URL}" "${C62_DSP_FIRMWARE_OUT}"
             TLS_VERIFY ON
             STATUS c62_dsp_status)
        list(GET c62_dsp_status 1 c62_dsp_message)
        if(EXISTS "${C62_DSP_FIRMWARE_OUT}")
            file(SHA256 "${C62_DSP_FIRMWARE_OUT}" c62_dsp_sha256)
        endif()
        if(NOT "${c62_dsp_sha256}" STREQUAL "${C62_DSP_FIRMWARE_SHA256}")
            file(REMOVE "${C62_DSP_FIRMWARE_OUT}")
            message(FATAL_ERROR "Could not get the C62 DSP firmware from "
                                "${C62_DSP_FIRMWARE_URL} (${c62_dsp_message}). "
                                "Set C62_DSP_FIRMWARE to a local copy with "
                                "sha256 ${C62_DSP_FIRMWARE_SHA256}.")
        endif()
    endif()
endif()

message(STATUS "C62 DSP firmware: ${C62_DSP_FIRMWARE_OUT}, flash at 0x100000")
