/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_CONSTANTS_H
#define DSTAR_CONSTANTS_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

/**
 * D-STAR digital voice (DV) air interface constants.
 *
 * Bit order convention, used by every D-STAR module: a byte array holds bits
 * in transmission order, the first transmitted bit being the least
 * significant bit of the first byte. This is the order of D-STAR equipment
 * and network protocols (headers, AMBE data, slow data), and of mbelib-neo's
 * DV data packing.
 */
namespace DStar
{

static constexpr uint32_t SYMBOL_RATE = 4800; ///< Bit rate, 1 bit per symbol

static constexpr size_t HEADER_SIZE = 41;      ///< Radio header, CRC included
static constexpr size_t HEADER_FEC_BITS = 660; ///< Radio header after FEC
static constexpr size_t HEADER_FEC_SIZE = 83;  ///< Bytes for HEADER_FEC_BITS

static constexpr size_t VOICE_SIZE = 9;     ///< AMBE data, per 20 ms frame
static constexpr size_t SLOW_DATA_SIZE = 3; ///< Slow data, per 20 ms frame
static constexpr size_t FRAME_SIZE = VOICE_SIZE + SLOW_DATA_SIZE;
static constexpr size_t FRAME_BITS = FRAME_SIZE * 8;

/// Voice frames in a superframe; the first one carries the data sync
static constexpr size_t SUPERFRAME_FRAMES = 21;

/// Bit synchronisation preamble byte: alternating 1, 0, 1, 0, ...
static const uint8_t PREAMBLE_BYTE = 0x55;

/// Frame synchronisation, sent after the preamble and before the header:
/// 1 1 1 0 1 1 0 0 1 0 1 0 0 0 0 (FRAME_SYNC_BITS bits).
static const uint8_t FRAME_SYNC[] = { 0x37, 0x05 };
static constexpr size_t FRAME_SYNC_BITS = 15;

/// Last 24 bits received before the radio header, preamble tail included,
/// oldest bit in the most significant position.
static constexpr uint32_t FRAME_SYNC_PATTERN = 0x557650;

/// Data synchronisation, slow data field of the first frame of each
/// superframe.
static const uint8_t DATA_SYNC[] = { 0x55, 0x2D, 0x16 };

/// DATA_SYNC as 24 received bits, oldest bit in the most significant
/// position.
static constexpr uint32_t DATA_SYNC_PATTERN = 0xAAB468;

/// End of transmission, sent after the last voice frame.
static const uint8_t END_SYNC[] = { 0x55, 0x55, 0x55, 0x55, 0xC8, 0x7A };
static constexpr size_t END_SYNC_BITS = 48;

/// END_SYNC as 48 received bits, oldest bit in the most significant position.
static constexpr uint64_t END_SYNC_PATTERN = 0xAAAAAAAA135EULL;

/// AMBE silence frame
static const uint8_t NULL_AMBE[] = { 0x9E, 0x8D, 0x32, 0x88, 0x26,
                                     0x1A, 0x3F, 0x61, 0xE8 };

/// Slow data scrambler, XORed with each 3-byte slow data field
static const uint8_t SLOW_DATA_SCRAMBLER[] = { 0x70, 0x4F, 0x93 };

/// Callsign field size, space padded
static constexpr size_t CALLSIGN_SIZE = 8;

/// Callsign suffix field size, space padded
static constexpr size_t SUFFIX_SIZE = 4;

/// Slow data text message size, space padded
static constexpr size_t MESSAGE_SIZE = 20;

} // namespace DStar

#endif // DSTAR_CONSTANTS_H
