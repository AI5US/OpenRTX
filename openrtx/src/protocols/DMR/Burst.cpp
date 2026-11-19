/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Fec.hpp"

using namespace DMR;

/// Slot type halves: bits 98-107 and 156-165
static constexpr size_t SLOT_TYPE_START1 = 98;
static constexpr size_t SLOT_TYPE_START2 = 156;

/// EMB halves: bits 108-115 and 148-155, embedded signalling in between
static constexpr size_t EMB_START1 = 108;
static constexpr size_t EMB_START2 = 148;
static constexpr size_t EMBEDDED_START = 116;

/**
 * Burst bit carrying bit i of the 216 voice bits.
 */
static inline size_t voiceBit(size_t i)
{
    return (i < 108) ? i : (i + SYNC_BITS);
}

void DMR::setSync(uint8_t *burst, uint64_t sync)
{
    for (size_t i = 0; i < SYNC_BITS; i++) {
        bool bit = ((sync >> (SYNC_BITS - 1 - i)) & 0x01) != 0;
        writeBit(burst, SYNC_START_BIT + i, bit);
    }
}

uint64_t DMR::getSync(const uint8_t *burst)
{
    uint64_t sync = 0;

    for (size_t i = 0; i < SYNC_BITS; i++) {
        sync <<= 1;
        if (readBit(burst, SYNC_START_BIT + i))
            sync |= 0x01;
    }

    return sync;
}

void DMR::encodeSlotType(uint8_t *burst, uint8_t colorCode, uint8_t dataType)
{
    uint8_t data = static_cast<uint8_t>(((colorCode & 0x0F) << 4)
                                        | (dataType & 0x0F));
    uint32_t code = golay2087Encode(data);

    for (size_t i = 0; i < 20; i++) {
        bool bit = ((code >> (19 - i)) & 0x01) != 0;
        size_t pos = (i < 10) ? (SLOT_TYPE_START1 + i) :
                                (SLOT_TYPE_START2 + i - 10);
        writeBit(burst, pos, bit);
    }
}

bool DMR::decodeSlotType(const uint8_t *burst, uint8_t &colorCode,
                         uint8_t &dataType)
{
    uint32_t code = 0;

    for (size_t i = 0; i < 20; i++) {
        size_t pos = (i < 10) ? (SLOT_TYPE_START1 + i) :
                                (SLOT_TYPE_START2 + i - 10);
        code <<= 1;
        if (readBit(burst, pos))
            code |= 0x01;
    }

    uint8_t errors;
    uint8_t data = golay2087Decode(code, &errors);

    colorCode = (data >> 4) & 0x0F;
    dataType = data & 0x0F;

    return errors <= 3;
}

void DMR::encodeEmb(uint8_t *burst, uint8_t colorCode, bool pi, uint8_t lcss)
{
    uint8_t data = static_cast<uint8_t>(((colorCode & 0x0F) << 3)
                                        | (pi ? 0x04 : 0x00) | (lcss & 0x03));
    uint16_t code = qr1676Encode(data);

    for (size_t i = 0; i < 16; i++) {
        bool bit = ((code >> (15 - i)) & 0x01) != 0;
        size_t pos = (i < 8) ? (EMB_START1 + i) : (EMB_START2 + i - 8);
        writeBit(burst, pos, bit);
    }
}

bool DMR::decodeEmb(const uint8_t *burst, uint8_t &colorCode, bool &pi,
                    uint8_t &lcss)
{
    uint16_t code = 0;

    for (size_t i = 0; i < 16; i++) {
        size_t pos = (i < 8) ? (EMB_START1 + i) : (EMB_START2 + i - 8);
        code = static_cast<uint16_t>(code << 1);
        if (readBit(burst, pos))
            code |= 0x01;
    }

    uint8_t errors;
    uint8_t data = qr1676Decode(code, &errors);

    colorCode = (data >> 3) & 0x0F;
    pi = (data & 0x04) != 0;
    lcss = data & 0x03;

    return errors <= 2;
}

void DMR::setEmbeddedData(uint8_t *burst, const uint8_t *data)
{
    for (size_t i = 0; i < 32; i++)
        writeBit(burst, EMBEDDED_START + i, readBit(data, i));
}

void DMR::getEmbeddedData(const uint8_t *burst, uint8_t *data)
{
    for (size_t i = 0; i < 32; i++)
        writeBit(data, i, readBit(burst, EMBEDDED_START + i));
}

void DMR::setVoiceFrames(uint8_t *burst,
                         const uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES])
{
    for (size_t f = 0; f < VOICE_FRAMES; f++) {
        for (size_t i = 0; i < (VOICE_FRAME_BYTES * 8); i++) {
            size_t pos = voiceBit((f * VOICE_FRAME_BYTES * 8) + i);
            writeBit(burst, pos, readBit(frames[f], i));
        }
    }
}

void DMR::getVoiceFrames(const uint8_t *burst,
                         uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES])
{
    for (size_t f = 0; f < VOICE_FRAMES; f++) {
        for (size_t i = 0; i < (VOICE_FRAME_BYTES * 8); i++) {
            size_t pos = voiceBit((f * VOICE_FRAME_BYTES * 8) + i);
            writeBit(frames[f], i, readBit(burst, pos));
        }
    }
}
