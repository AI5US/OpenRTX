/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_BURST_H
#define DMR_BURST_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/DMR/Constants.hpp"

/*
 * Access to the fields of a DMR burst (BURST_BYTES bytes, MMDVM bit order),
 * from MMDVMHost DMRSlotType.cpp and DMREMB.cpp:
 *
 * - voice burst: voice bits 0-107, sync or EMB + embedded signalling
 *   108-155, voice bits 156-263;
 * - data burst: information bits 0-97, slot type 98-107, sync 108-155, slot
 *   type 156-165, information bits 166-263.
 */

namespace DMR
{

/**
 * \brief Read a bit of a byte array, first bit in the most significant bit
 * of the first byte.
 *
 * @param buf: byte array.
 * @param pos: bit position.
 * @return bit value.
 */
static inline bool readBit(const uint8_t *buf, size_t pos)
{
    return ((buf[pos / 8] >> (7 - (pos % 8))) & 0x01) != 0;
}

/**
 * \brief Write a bit of a byte array, first bit in the most significant bit
 * of the first byte.
 *
 * @param buf: byte array.
 * @param pos: bit position.
 * @param value: bit value.
 */
static inline void writeBit(uint8_t *buf, size_t pos, bool value)
{
    uint8_t mask = static_cast<uint8_t>(0x80 >> (pos % 8));

    if (value)
        buf[pos / 8] |= mask;
    else
        buf[pos / 8] &= static_cast<uint8_t>(~mask);
}

/**
 * \brief Write the 48 bit sync field of a burst.
 *
 * @param burst: burst.
 * @param sync: sync pattern, e.g. MS_VOICE_SYNC.
 */
void setSync(uint8_t *burst, uint64_t sync);

/**
 * \brief Read the 48 bit sync field of a burst.
 *
 * @param burst: burst.
 * @return the sync field.
 */
uint64_t getSync(const uint8_t *burst);

/**
 * \brief Write the slot type of a data burst: colour code and data type,
 * Golay (20,8) coded.
 *
 * @param burst: burst.
 * @param colorCode: colour code, 0 to 15.
 * @param dataType: data type, 0 to 15.
 */
void encodeSlotType(uint8_t *burst, uint8_t colorCode, uint8_t dataType);

/**
 * \brief Read the slot type of a data burst.
 *
 * @param burst: burst.
 * @param colorCode: decoded colour code.
 * @param dataType: decoded data type.
 * @return true if the slot type has at most 3 bit errors.
 */
bool decodeSlotType(const uint8_t *burst, uint8_t &colorCode,
                    uint8_t &dataType);

/**
 * \brief Write the EMB of a voice burst (B to F): colour code, privacy
 * indicator and link control start/stop, QR (16,7) coded.
 *
 * @param burst: burst.
 * @param colorCode: colour code, 0 to 15.
 * @param pi: privacy indicator.
 * @param lcss: link control start/stop, see Lcss.
 */
void encodeEmb(uint8_t *burst, uint8_t colorCode, bool pi, uint8_t lcss);

/**
 * \brief Read the EMB of a voice burst.
 *
 * @param burst: burst.
 * @param colorCode: decoded colour code.
 * @param pi: decoded privacy indicator.
 * @param lcss: decoded link control start/stop.
 * @return true if the EMB has at most 2 bit errors.
 */
bool decodeEmb(const uint8_t *burst, uint8_t &colorCode, bool &pi,
               uint8_t &lcss);

/**
 * \brief Write the 32 bits of embedded signalling of a voice burst (B to
 * F), between the two halves of the EMB.
 *
 * @param burst: burst.
 * @param data: 4 bytes.
 */
void setEmbeddedData(uint8_t *burst, const uint8_t *data);

/**
 * \brief Read the 32 bits of embedded signalling of a voice burst.
 *
 * @param burst: burst.
 * @param data: 4 bytes.
 */
void getEmbeddedData(const uint8_t *burst, uint8_t *data);

/**
 * \brief Write the three AMBE+2 frames of a voice burst. The second frame
 * is split around the sync field.
 *
 * @param burst: burst.
 * @param frames: VOICE_FRAMES frames of VOICE_FRAME_BYTES bytes.
 */
void setVoiceFrames(uint8_t *burst,
                    const uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES]);

/**
 * \brief Read the three AMBE+2 frames of a voice burst.
 *
 * @param burst: burst.
 * @param frames: VOICE_FRAMES frames of VOICE_FRAME_BYTES bytes.
 */
void getVoiceFrames(const uint8_t *burst,
                    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES]);

} // namespace DMR

#endif // DMR_BURST_H
