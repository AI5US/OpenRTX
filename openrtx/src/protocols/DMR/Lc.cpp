/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Fec.hpp"
#include "protocols/DMR/Lc.hpp"

using namespace DMR;

constexpr size_t EmbeddedLcEncoder::FRAGMENTS;

void Lc::toBytes(uint8_t *bytes) const
{
    bytes[0] = flco & 0x3F;
    bytes[1] = fid;
    bytes[2] = options;
    bytes[3] = static_cast<uint8_t>(dst >> 16);
    bytes[4] = static_cast<uint8_t>(dst >> 8);
    bytes[5] = static_cast<uint8_t>(dst);
    bytes[6] = static_cast<uint8_t>(src >> 16);
    bytes[7] = static_cast<uint8_t>(src >> 8);
    bytes[8] = static_cast<uint8_t>(src);
}

void Lc::fromBytes(const uint8_t *bytes)
{
    flco = bytes[0] & 0x3F;
    fid = bytes[1];
    options = bytes[2];
    dst = (static_cast<uint32_t>(bytes[3]) << 16)
        | (static_cast<uint32_t>(bytes[4]) << 8) | bytes[5];
    src = (static_cast<uint32_t>(bytes[6]) << 16)
        | (static_cast<uint32_t>(bytes[7]) << 8) | bytes[8];
}

/**
 * Checksum mask of the full link control for a data type.
 *
 * @return the mask, nullptr for data types not carrying a full LC.
 */
static const uint8_t *fullLcMask(uint8_t dataType)
{
    switch (dataType) {
        case DT_VOICE_LC_HEADER:
            return VOICE_LC_HEADER_CRC_MASK;

        case DT_TERMINATOR_WITH_LC:
            return TERMINATOR_WITH_LC_CRC_MASK;

        default:
            return nullptr;
    }
}

void DMR::encodeFullLc(const Lc &lc, uint8_t dataType, uint8_t *burst)
{
    const uint8_t *mask = fullLcMask(dataType);
    if (mask == nullptr)
        return;

    uint8_t data[FULL_LC_BYTES];
    lc.toBytes(data);
    rs129Encode(data, &data[LC_BYTES]);

    for (size_t i = 0; i < 3; i++)
        data[LC_BYTES + i] ^= mask[i];

    bptc19696Encode(data, burst);
}

bool DMR::decodeFullLc(const uint8_t *burst, uint8_t dataType, Lc &lc)
{
    const uint8_t *mask = fullLcMask(dataType);
    if (mask == nullptr)
        return false;

    uint8_t data[FULL_LC_BYTES];
    bptc19696Decode(burst, data);

    for (size_t i = 0; i < 3; i++)
        data[LC_BYTES + i] ^= mask[i];

    if (rs129Check(data) == false)
        return false;

    lc.fromBytes(data);
    return true;
}

/*
 * Embedded link control coding (MMDVMHost DMREmbeddedData.cpp): the 72 LC
 * bits and their 5 bit checksum fill a matrix of 8 rows of 16 bits. Rows 0
 * to 6 hold 11 bits (10 for rows 2 to 6, plus a checksum bit) and their
 * Hamming (16,11,4) parity, row 7 the parity of each column. The matrix is
 * sent column by column.
 */

/// Matrix positions of the 72 LC bits
static inline size_t lcBitPos(size_t i)
{
    if (i < 22)
        return ((i / 11) * 16) + (i % 11);

    size_t row = ((i - 22) / 10) + 2;
    return (row * 16) + ((i - 22) % 10);
}

/// Matrix position of the checksum bit of weight 2^(4 - i), i = 0 to 4
static inline size_t crcBitPos(size_t i)
{
    return 42 + (i * 16);
}

/// Transmission order: matrix bit of coded bit a
static inline size_t matrixPos(size_t a)
{
    // Column by column: b = (a * 16) mod 127, bit 127 staying in place
    return (a == 127) ? 127 : ((a * 16) % 127);
}

EmbeddedLcEncoder::EmbeddedLcEncoder()
{
    memset(raw, 0x00, sizeof(raw));
}

void EmbeddedLcEncoder::setLc(const Lc &lc)
{
    uint8_t bytes[LC_BYTES];
    lc.toBytes(bytes);

    uint8_t m[128];
    memset(m, 0x00, sizeof(m));

    for (size_t i = 0; i < (LC_BYTES * 8); i++)
        m[lcBitPos(i)] = readBit(bytes, i) ? 1 : 0;

    uint8_t crc = crc5(bytes);
    for (size_t i = 0; i < 5; i++)
        m[crcBitPos(i)] = (crc >> (4 - i)) & 0x01;

    for (size_t r = 0; r < 7; r++)
        hamming16114Encode(&m[r * 16]);

    for (size_t c = 0; c < 16; c++) {
        uint8_t parity = 0;
        for (size_t r = 0; r < 7; r++)
            parity ^= m[(r * 16) + c];

        m[112 + c] = parity;
    }

    for (size_t a = 0; a < 128; a++)
        writeBit(raw, a, m[matrixPos(a)] != 0);
}

uint8_t EmbeddedLcEncoder::getFragment(size_t n, uint8_t *data) const
{
    memcpy(data, &raw[(n % FRAGMENTS) * 4], 4);

    switch (n % FRAGMENTS) {
        case 0:
            return LCSS_FIRST;

        case FRAGMENTS - 1:
            return LCSS_LAST;

        default:
            return LCSS_CONT;
    }
}

EmbeddedLcDecoder::EmbeddedLcDecoder()
{
    reset();
}

void EmbeddedLcDecoder::reset()
{
    memset(raw, 0x00, sizeof(raw));
    count = 0;
}

bool EmbeddedLcDecoder::push(uint8_t lcss, const uint8_t *data, Lc &lc)
{
    // Fragments must come in order: first, two continuations, last
    if (lcss == LCSS_FIRST) {
        count = 0;
    } else if ((lcss == LCSS_CONT) && ((count == 1) || (count == 2))) {
        // Expected continuation
    } else if ((lcss == LCSS_LAST) && (count == 3)) {
        // Expected last fragment
    } else {
        count = 0;
        return false;
    }

    memcpy(&raw[count * 4], data, 4);
    count++;

    if (count < EmbeddedLcEncoder::FRAGMENTS)
        return false;

    count = 0;

    uint8_t m[128];
    for (size_t a = 0; a < 128; a++)
        m[matrixPos(a)] = readBit(raw, a) ? 1 : 0;

    for (size_t r = 0; r < 7; r++) {
        if (hamming16114Decode(&m[r * 16]) == false)
            return false;
    }

    for (size_t c = 0; c < 16; c++) {
        uint8_t parity = 0;
        for (size_t r = 0; r < 8; r++)
            parity ^= m[(r * 16) + c];

        if (parity != 0)
            return false;
    }

    uint8_t bytes[LC_BYTES];
    memset(bytes, 0x00, sizeof(bytes));
    for (size_t i = 0; i < (LC_BYTES * 8); i++)
        writeBit(bytes, i, m[lcBitPos(i)] != 0);

    uint8_t crc = 0;
    for (size_t i = 0; i < 5; i++)
        crc = static_cast<uint8_t>((crc << 1) | m[crcBitPos(i)]);

    if (crc != crc5(bytes))
        return false;

    // Only voice calls are of interest
    uint8_t flco = bytes[0] & 0x3F;
    if ((flco != FLCO_GROUP) && (flco != FLCO_USER_USER))
        return false;

    lc.fromBytes(bytes);
    return true;
}
