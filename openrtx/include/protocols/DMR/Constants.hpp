/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_CONSTANTS_H
#define DMR_CONSTANTS_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

/*
 * DMR (ETSI TS 102 361-1) Layer 1 and 2 constants, as used for direct mode
 * (DMO) voice. Values from MMDVM and MMDVMHost (DMRDefines.h).
 *
 * Bit order: a burst is a sequence of 264 bits, bit 0 being the most
 * significant bit of byte 0 (the MMDVM layout). Each pair of bits is a
 * symbol, sent first bit first: 01 = +3, 00 = +1, 10 = -1, 11 = -3.
 */

namespace DMR
{

/// Symbol rate, in symbols per second
static constexpr uint32_t SYMBOL_RATE = 4800;

/// Burst length: 264 bits, 132 symbols, 27.5 ms
static constexpr size_t BURST_BYTES = 33;
static constexpr size_t BURST_BITS = BURST_BYTES * 8;
static constexpr size_t BURST_SYMBOLS = BURST_BITS / 2;

/// A timeslot lasts 30 ms (144 symbols), a TDMA frame of two timeslots 60 ms
static constexpr size_t SLOT_SYMBOLS = 144;
static constexpr size_t FRAME_SYMBOLS = 2 * SLOT_SYMBOLS;

/// Position and length of the sync (or embedded signalling) field
static constexpr size_t SYNC_START_BIT = 108;
static constexpr size_t SYNC_BITS = 48;
static constexpr size_t SYNC_SYMBOLS = SYNC_BITS / 2;

/// AMBE+2 voice frames carried by a voice burst, 72 bits each
static constexpr size_t VOICE_FRAMES = 3;
static constexpr size_t VOICE_FRAME_BYTES = 9;

/// Voice bursts in a superframe (A to F), 360 ms
static constexpr size_t SUPERFRAME_BURSTS = 6;

/// Full link control: 72 bits of data and 24 of checksum
static constexpr size_t LC_BYTES = 9;
static constexpr size_t FULL_LC_BYTES = 12;

/// Mobile station sourced sync patterns, the ones used in direct mode
static constexpr uint64_t MS_VOICE_SYNC = 0x7F7D5DD57DFDULL;
static constexpr uint64_t MS_DATA_SYNC = 0xD5D7F77FD757ULL;
static constexpr uint64_t SYNC_MASK = 0xFFFFFFFFFFFFULL;

/// MS sourced sync patterns as symbol signs, one bit per symbol, oldest in
/// the most significant bit, bit 1 for a positive (+3) symbol. The voice
/// sync is the complement of the data sync.
static constexpr uint32_t MS_DATA_SYNC_SIGNS = 0x76286E;
static constexpr uint32_t MS_VOICE_SYNC_SIGNS = 0x89D791;
static constexpr uint32_t SYNC_SIGNS_MASK = 0xFFFFFF;

/// Base station sourced sync patterns, sent by repeaters, and their symbol
/// signs
static constexpr uint64_t BS_VOICE_SYNC = 0x755FD7DF75F7ULL;
static constexpr uint64_t BS_DATA_SYNC = 0xDFF57D75DF5DULL;
static constexpr uint32_t BS_VOICE_SYNC_SIGNS = 0xBC64B2;
static constexpr uint32_t BS_DATA_SYNC_SIGNS = 0x439B4D;

/// Common announcement channel (CACH), before each burst of a repeater
/// transmission: 24 bits, 12 symbols, 2.5 ms
static constexpr size_t CACH_BITS = 24;
static constexpr size_t CACH_SYMBOLS = CACH_BITS / 2;

/// Control signalling block opcode: BS outbound activation, which wakes up
/// a repeater
static constexpr uint8_t CSBKO_BS_DWN_ACT = 0x38;

/// Checksum mask of a CSBK
static constexpr uint8_t CSBK_CRC_MASK[2] = { 0xA5, 0xA5 };

/// Radio ID addressing all repeaters
static constexpr uint32_t ALL_ID = 0xFFFFFF;

/// Data types, sent in the slot type field of data bursts
enum DataType : uint8_t {
    DT_VOICE_PI_HEADER = 0,
    DT_VOICE_LC_HEADER = 1,
    DT_TERMINATOR_WITH_LC = 2,
    DT_CSBK = 3,
    DT_DATA_HEADER = 6,
    DT_RATE_12_DATA = 7,
    DT_RATE_34_DATA = 8,
    DT_IDLE = 9,
    DT_RATE_1_DATA = 10,
};

/// Full link control opcodes
enum Flco : uint8_t {
    FLCO_GROUP = 0x00,     ///< Group voice channel user
    FLCO_USER_USER = 0x03, ///< Unit to unit voice channel user
};

/// Link control start/stop, in the EMB of voice bursts B to F
enum Lcss : uint8_t {
    LCSS_SINGLE = 0, ///< Single fragment (or no embedded LC)
    LCSS_FIRST = 1,  ///< First fragment of the embedded LC
    LCSS_LAST = 2,   ///< Last fragment
    LCSS_CONT = 3,   ///< Continuation fragment
};

/// Checksum masks of the full link control
static constexpr uint8_t VOICE_LC_HEADER_CRC_MASK[3] = { 0x96, 0x96, 0x96 };
static constexpr uint8_t TERMINATOR_WITH_LC_CRC_MASK[3] = { 0x99, 0x99, 0x99 };

/// Idle symbol pattern, filling the unused timeslot of a direct mode
/// transmission (MMDVM DMRDMOTX.cpp PR_FILL), 156 symbols
static constexpr size_t IDLE_FILL_BYTES = 39;
static constexpr uint8_t IDLE_FILL[IDLE_FILL_BYTES] = {
    0x63, 0xEA, 0x00, 0x76, 0x6C, 0x76, 0xC4, 0x52, 0xC8, 0x78,
    0x09, 0x2D, 0xB8, 0x79, 0x27, 0x57, 0x9B, 0x31, 0xBC, 0x3E,
    0xEA, 0x45, 0xC3, 0x30, 0x49, 0x17, 0x93, 0xAE, 0x8B, 0x6D,
    0xA4, 0xA5, 0xAD, 0xA2, 0xF1, 0x35, 0xB5, 0x3C, 0x1E
};

/// Preamble byte, +3 +3 -3 -3 symbols (MMDVM DMR_SYNC)
static constexpr uint8_t PREAMBLE_BYTE = 0x5F;

/// AMBE+2 silence frame
static constexpr uint8_t SILENCE_FRAME[VOICE_FRAME_BYTES] = {
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B
};

} // namespace DMR

#endif // DMR_CONSTANTS_H
