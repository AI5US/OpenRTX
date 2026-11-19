/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_CONSTANTS_H
#define YSF_CONSTANTS_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

/*
 * Yaesu System Fusion (C4FM) constants, from MMDVMHost YSFDefines.h and
 * MMDVM YSFDefines.h.
 */
namespace YSF
{

/// Symbol rate, 4FSK
static constexpr uint32_t SYMBOL_RATE = 4800;

/// Frame: 100 ms, sync, FICH and five blocks of payload
static constexpr size_t FRAME_BYTES = 120;
static constexpr size_t FRAME_SYMBOLS = FRAME_BYTES * 4;

/// Frame synchronisation pattern, 40 bits
static constexpr uint64_t SYNC = 0xD471C9634DULL;
static constexpr size_t SYNC_BYTES = 5;
static constexpr size_t SYNC_SYMBOLS = SYNC_BYTES * 4;

/// Sync symbol signs, one bit per symbol, 1 for a positive symbol
static constexpr uint32_t SYNC_SIGNS = 0x7B5AD;
static constexpr uint32_t SYNC_SIGNS_MASK = 0xFFFFF;

/// Frame information channel, after the sync
static constexpr size_t FICH_BYTES = 25;

/// Payload: five blocks of DCH and VCH, after the FICH
static constexpr size_t PAYLOAD_OFFSET = SYNC_BYTES + FICH_BYTES;
static constexpr size_t BLOCKS = 5;
static constexpr size_t BLOCK_BYTES = 18;

/// Callsign fields, padded with spaces
static constexpr size_t CALLSIGN_LEN = 10;

/// AMBE+2 frames per YSF frame, one per block in V/D mode 2
static constexpr size_t VOICE_FRAMES = BLOCKS;

/// AMBE+2 parameter bits of a voice frame, packed first bit in the most
/// significant position
static constexpr size_t VOICE_BITS = 49;
static constexpr size_t VOICE_FRAME_BYTES = 7;

/// Frame information
enum Fi : uint8_t {
    FI_HEADER = 0,
    FI_COMMUNICATIONS = 1,
    FI_TERMINATOR = 2,
    FI_TEST = 3,
};

/// Data type: V/D mode 1 and 2 carry voice and data, the FR modes one of
/// them at full rate
enum Dt : uint8_t {
    DT_VD_MODE1 = 0,
    DT_DATA_FR_MODE = 1,
    DT_VD_MODE2 = 2,
    DT_VOICE_FR_MODE = 3,
};

/// Message route
enum Mr : uint8_t {
    MR_DIRECT = 0,
    MR_NOT_BUSY = 1,
    MR_BUSY = 2,
};

/// Last frame number of a V/D mode 2 transmission: the DCH cycles through
/// eight frames (destination, source, downlink, uplink, remarks, data)
static constexpr uint8_t VD_MODE2_FT = 7;

/// Callsign information of a transmission from a radio (MMDVM_CM DMR2YSF)
static constexpr uint8_t CS_RADIO = 2;

/// Payload whitening sequence (MMDVMHost YSFPayload.cpp WHITENING_DATA)
static constexpr uint8_t WHITENING[20] = {
    0x93, 0xD7, 0x51, 0x21, 0x9C, 0x2F, 0x6C, 0xD0, 0xEF, 0x0F,
    0xF8, 0x3D, 0xF1, 0x73, 0x20, 0x94, 0xED, 0x1E, 0x7C, 0xD8,
};

} // namespace YSF

#endif // YSF_CONSTANTS_H
