/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "core/crc.h"
#include "protocols/YSF/Bits.hpp"
#include "protocols/YSF/Constants.hpp"
#include "protocols/YSF/Payload.hpp"

using namespace YSF;

/// Coded bit pairs of a V/D mode 2 DCH and of a full rate DCH
static constexpr size_t VD2_PAIRS = 100;
static constexpr size_t FR_PAIRS = 180;

/// DCH bytes per block: V/D mode 2, full rate
static constexpr size_t VD2_BLOCK_DCH = 5;
static constexpr size_t FR_BLOCK_DCH = 9;

/// Voice channel: bits, repeated bits and first bit in a block
static constexpr size_t VCH_BITS = 104;
static constexpr size_t VCH_BYTES = VCH_BITS / 8;
static constexpr size_t VCH_REPEATED = 27;
static constexpr size_t VCH_OFFSET = VD2_BLOCK_DCH * 8;

/*
 * Interleaves of MMDVMHost YSFPayload.cpp: bit pair positions for the DCHs
 * (INTERLEAVE_TABLE_5_20 and _9_20), bit positions for the VCH
 * (INTERLEAVE_TABLE_26_4).
 */
static size_t interleave520(size_t i)
{
    return ((i / 5) * 2) + ((i % 5) * 40);
}

static size_t interleave920(size_t i)
{
    return ((i / 9) * 2) + ((i % 9) * 40);
}

static size_t interleave264(size_t i)
{
    return ((i % 26) * 4) + (i / 26);
}

/// CRC of a DCH, appended in its last two bytes: CRC-16/CCITT, inverted
static void addCrc(uint8_t *data, size_t len)
{
    uint16_t crc = static_cast<uint16_t>(~crc_ccitt(data, len - 2));

    data[len - 2] = static_cast<uint8_t>(crc >> 8);
    data[len - 1] = static_cast<uint8_t>(crc);
}

static bool checkCrc(const uint8_t *data, size_t len)
{
    uint16_t crc = static_cast<uint16_t>(~crc_ccitt(data, len - 2));

    return (data[len - 2] == static_cast<uint8_t>(crc >> 8))
        && (data[len - 1] == static_cast<uint8_t>(crc));
}

/**
 * Code a DCH: whitening, CRC, convolutional code and interleave, then spread
 * over the blocks.
 *
 * @param dch: information bytes.
 * @param len: number of information bytes.
 * @param pairs: coded bit pairs.
 * @param blockBytes: DCH bytes in each block.
 * @param interleave: interleave function.
 * @param out: first DCH byte of the first block in the frame.
 */
static void writeDch(const uint8_t *dch, size_t len, size_t pairs,
                     size_t blockBytes, size_t (*interleave)(size_t),
                     uint8_t *out)
{
    // Information, CRC and a zero tail byte
    uint8_t data[FR_DCH_BYTES + 3];
    for (size_t i = 0; i < len; i++)
        data[i] = dch[i] ^ WHITENING[i];

    addCrc(data, len + 2);
    data[len + 2] = 0x00;

    uint8_t coded[(FR_PAIRS * 2) / 8];
    Convolution conv;
    conv.encode(data, coded, pairs);

    uint8_t bytes[(FR_PAIRS * 2) / 8];
    for (size_t i = 0; i < pairs; i++) {
        size_t n = interleave(i);

        writeBit(bytes, n, readBit(coded, 2 * i));
        writeBit(bytes, n + 1, readBit(coded, (2 * i) + 1));
    }

    for (size_t b = 0; b < BLOCKS; b++)
        memcpy(out + (b * BLOCK_BYTES), bytes + (b * blockBytes), blockBytes);
}

/**
 * Decode a DCH, see writeDch().
 *
 * @return true if the CRC is valid.
 */
static bool readDch(const uint8_t *in, size_t len, size_t pairs,
                    size_t blockBytes, size_t (*interleave)(size_t),
                    Convolution &conv, uint8_t *dch)
{
    uint8_t bytes[(FR_PAIRS * 2) / 8];
    for (size_t b = 0; b < BLOCKS; b++)
        memcpy(bytes + (b * blockBytes), in + (b * BLOCK_BYTES), blockBytes);

    conv.start();
    for (size_t i = 0; i < pairs; i++) {
        size_t n = interleave(i);
        uint8_t s0 = readBit(bytes, n) ? 1 : 0;
        uint8_t s1 = readBit(bytes, n + 1) ? 1 : 0;

        conv.decode(s0, s1);
    }

    uint8_t data[FR_DCH_BYTES + 3];
    conv.chainback(data, (pairs - 4));

    bool valid = checkCrc(data, len + 2);
    for (size_t i = 0; i < len; i++)
        dch[i] = data[i] ^ WHITENING[i];

    return valid;
}

void YSF::writeVd2Dch(const uint8_t *dch, uint8_t *frame)
{
    writeDch(dch, VD2_DCH_BYTES, VD2_PAIRS, VD2_BLOCK_DCH, interleave520,
             frame + PAYLOAD_OFFSET);
}

bool YSF::readVd2Dch(const uint8_t *frame, Convolution &conv, uint8_t *dch)
{
    return readDch(frame + PAYLOAD_OFFSET, VD2_DCH_BYTES, VD2_PAIRS,
                   VD2_BLOCK_DCH, interleave520, conv, dch);
}

void YSF::writeFrDch(size_t index, const uint8_t *dch, uint8_t *frame)
{
    uint8_t *out = frame + PAYLOAD_OFFSET + (index * FR_BLOCK_DCH);

    writeDch(dch, FR_DCH_BYTES, FR_PAIRS, FR_BLOCK_DCH, interleave920, out);
}

bool YSF::readFrDch(size_t index, const uint8_t *frame, Convolution &conv,
                    uint8_t *dch)
{
    const uint8_t *in = frame + PAYLOAD_OFFSET + (index * FR_BLOCK_DCH);

    return readDch(in, FR_DCH_BYTES, FR_PAIRS, FR_BLOCK_DCH, interleave920,
                   conv, dch);
}

void YSF::writeVch(size_t index, const uint8_t *voice, uint8_t *frame)
{
    uint8_t vch[VCH_BYTES];
    memset(vch, 0x00, sizeof(vch));

    for (size_t i = 0; i < VCH_REPEATED; i++) {
        bool bit = readBit(voice, i);

        writeBit(vch, 3 * i, bit);
        writeBit(vch, (3 * i) + 1, bit);
        writeBit(vch, (3 * i) + 2, bit);
    }

    for (size_t i = VCH_REPEATED; i < VOICE_BITS; i++)
        writeBit(vch, (2 * VCH_REPEATED) + i, readBit(voice, i));

    for (size_t i = 0; i < VCH_BYTES; i++)
        vch[i] ^= WHITENING[i];

    uint8_t *block = frame + PAYLOAD_OFFSET + (index * BLOCK_BYTES);
    for (size_t i = 0; i < VCH_BITS; i++)
        writeBit(block, VCH_OFFSET + interleave264(i), readBit(vch, i));
}

size_t YSF::readVch(size_t index, const uint8_t *frame, uint8_t *voice)
{
    const uint8_t *block = frame + PAYLOAD_OFFSET + (index * BLOCK_BYTES);

    uint8_t vch[VCH_BYTES];
    for (size_t i = 0; i < VCH_BITS; i++)
        writeBit(vch, i, readBit(block, VCH_OFFSET + interleave264(i)));

    for (size_t i = 0; i < VCH_BYTES; i++)
        vch[i] ^= WHITENING[i];

    memset(voice, 0x00, VOICE_FRAME_BYTES);
    size_t errors = 0;

    for (size_t i = 0; i < VCH_REPEATED; i++) {
        uint8_t votes = (readBit(vch, 3 * i) ? 1 : 0)
                      + (readBit(vch, (3 * i) + 1) ? 1 : 0)
                      + (readBit(vch, (3 * i) + 2) ? 1 : 0);

        if ((votes == 1) || (votes == 2))
            errors++;

        writeBit(voice, i, votes >= 2);
    }

    for (size_t i = VCH_REPEATED; i < VOICE_BITS; i++)
        writeBit(voice, i, readBit(vch, (2 * VCH_REPEATED) + i));

    return errors;
}
