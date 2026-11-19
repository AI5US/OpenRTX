/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_PAYLOAD_H
#define YSF_PAYLOAD_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/YSF/Convolution.hpp"

/*
 * Payload of a frame: five 18 byte blocks after the FICH (MMDVMHost
 * YSFPayload.cpp). In V/D mode 2 each block holds 5 bytes of the data
 * channel (DCH) and 13 of a voice channel (VCH); header and terminator frames
 * carry two full rate DCHs instead, in the first and last 9 bytes of each
 * block.
 */
namespace YSF
{

/// Bytes of a V/D mode 2 DCH
static constexpr size_t VD2_DCH_BYTES = 10;

/// Bytes of a full rate DCH: callsign data CSD1 or CSD2
static constexpr size_t FR_DCH_BYTES = 20;

/**
 * Write the V/D mode 2 DCH of a frame.
 *
 * @param dch: VD2_DCH_BYTES bytes.
 * @param frame: FRAME_BYTES bytes.
 */
void writeVd2Dch(const uint8_t *dch, uint8_t *frame);

/**
 * Read the V/D mode 2 DCH of a frame.
 *
 * @param frame: FRAME_BYTES bytes.
 * @param conv: decoder to use.
 * @param dch: output, VD2_DCH_BYTES bytes, written in any case.
 * @return true if the CRC is valid.
 */
bool readVd2Dch(const uint8_t *frame, Convolution &conv, uint8_t *dch);

/**
 * Write one of the two full rate DCHs of a header or terminator frame.
 *
 * @param index: 0 for CSD1 (destination, source), 1 for CSD2 (downlink,
 * uplink).
 * @param dch: FR_DCH_BYTES bytes.
 * @param frame: FRAME_BYTES bytes.
 */
void writeFrDch(size_t index, const uint8_t *dch, uint8_t *frame);

/**
 * Read one of the two full rate DCHs of a header or terminator frame.
 *
 * @param index: 0 for CSD1, 1 for CSD2.
 * @param frame: FRAME_BYTES bytes.
 * @param conv: decoder to use.
 * @param dch: output, FR_DCH_BYTES bytes, written in any case.
 * @return true if the CRC is valid.
 */
bool readFrDch(size_t index, const uint8_t *frame, Convolution &conv,
               uint8_t *dch);

/**
 * Write a V/D mode 2 voice channel: the first 27 bits of the AMBE+2 frame
 * three times, the other 22 once, whitened and interleaved.
 *
 * @param index: block, 0 to BLOCKS - 1.
 * @param voice: VOICE_FRAME_BYTES bytes, the VOICE_BITS parameter bits.
 * @param frame: FRAME_BYTES bytes.
 */
void writeVch(size_t index, const uint8_t *voice, uint8_t *frame);

/**
 * Read a V/D mode 2 voice channel, correcting the repeated bits by majority.
 *
 * @param index: block, 0 to BLOCKS - 1.
 * @param frame: FRAME_BYTES bytes.
 * @param voice: output, VOICE_FRAME_BYTES bytes.
 * @return number of repeated bits found with an error.
 */
size_t readVch(size_t index, const uint8_t *frame, uint8_t *voice);

} // namespace YSF

#endif // YSF_PAYLOAD_H
