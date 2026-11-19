/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2012 Ian Wraith
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Fec.hpp"

using namespace DMR;

/*
 * Parity bits of the Golay (20,8,7) code word of each 8 bit value, from
 * MMDVMHost ENCODING_TABLE_2087 (rearranged: the code word is the data
 * followed by these 12 bits).
 */
static const uint16_t GOLAY2087_PARITY[256] = {
    0x000, 0x8EB, 0x93E, 0x1D5, 0xA97, 0x27C, 0x3A9, 0xB42, 0xDC6, 0x52D, 0x4F8,
    0xC13, 0x751, 0xFBA, 0xE6F, 0x684, 0x367, 0xB8C, 0xA59, 0x2B2, 0x9F0, 0x11B,
    0x0CE, 0x825, 0xEA1, 0x64A, 0x79F, 0xF74, 0x436, 0xCDD, 0xD08, 0x5E3, 0x6CD,
    0xE26, 0xFF3, 0x718, 0xC5A, 0x4B1, 0x564, 0xD8F, 0xB0B, 0x3E0, 0x235, 0xADE,
    0x19C, 0x977, 0x8A2, 0x049, 0x5AA, 0xD41, 0xC94, 0x47F, 0xF3D, 0x7D6, 0x603,
    0xEE8, 0x86C, 0x087, 0x152, 0x9B9, 0x2FB, 0xA10, 0xBC5, 0x32E, 0xD99, 0x572,
    0x4A7, 0xC4C, 0x70E, 0xFE5, 0xE30, 0x6DB, 0x05F, 0x8B4, 0x961, 0x18A, 0xAC8,
    0x223, 0x3F6, 0xB1D, 0xEFE, 0x615, 0x7C0, 0xF2B, 0x469, 0xC82, 0xD57, 0x5BC,
    0x338, 0xBD3, 0xA06, 0x2ED, 0x9AF, 0x144, 0x091, 0x87A, 0xB54, 0x3BF, 0x26A,
    0xA81, 0x1C3, 0x928, 0x8FD, 0x016, 0x692, 0xE79, 0xFAC, 0x747, 0xC05, 0x4EE,
    0x53B, 0xDD0, 0x833, 0x0D8, 0x10D, 0x9E6, 0x2A4, 0xA4F, 0xB9A, 0x371, 0x5F5,
    0xD1E, 0xCCB, 0x420, 0xF62, 0x789, 0x65C, 0xEB7, 0x3DA, 0xB31, 0xAE4, 0x20F,
    0x94D, 0x1A6, 0x073, 0x898, 0xE1C, 0x6F7, 0x722, 0xFC9, 0x48B, 0xC60, 0xDB5,
    0x55E, 0x0BD, 0x856, 0x983, 0x168, 0xA2A, 0x2C1, 0x314, 0xBFF, 0xD7B, 0x590,
    0x445, 0xCAE, 0x7EC, 0xF07, 0xED2, 0x639, 0x517, 0xDFC, 0xC29, 0x4C2, 0xF80,
    0x76B, 0x6BE, 0xE55, 0x8D1, 0x03A, 0x1EF, 0x904, 0x246, 0xAAD, 0xB78, 0x393,
    0x670, 0xE9B, 0xF4E, 0x7A5, 0xCE7, 0x40C, 0x5D9, 0xD32, 0xBB6, 0x35D, 0x288,
    0xA63, 0x121, 0x9CA, 0x81F, 0x0F4, 0xE43, 0x6A8, 0x77D, 0xF96, 0x4D4, 0xC3F,
    0xDEA, 0x501, 0x385, 0xB6E, 0xABB, 0x250, 0x912, 0x1F9, 0x02C, 0x8C7, 0xD24,
    0x5CF, 0x41A, 0xCF1, 0x7B3, 0xF58, 0xE8D, 0x666, 0x0E2, 0x809, 0x9DC, 0x137,
    0xA75, 0x29E, 0x34B, 0xBA0, 0x88E, 0x065, 0x1B0, 0x95B, 0x219, 0xAF2, 0xB27,
    0x3CC, 0x548, 0xDA3, 0xC76, 0x49D, 0xFDF, 0x734, 0x6E1, 0xE0A, 0xBE9, 0x302,
    0x2D7, 0xA3C, 0x17E, 0x995, 0x840, 0x0AB, 0x62F, 0xEC4, 0xF11, 0x7FA, 0xCB8,
    0x453, 0x586, 0xD6D,
};

/*
 * Parity bits of the quadratic residue (16,7,6) code word of each 7 bit
 * value, from MMDVMHost ENCODING_TABLE_1676 (the code word is the data
 * followed by these 9 bits).
 */
static const uint16_t QR1676_PARITY[128] = {
    0x000, 0x073, 0x0E5, 0x096, 0x1C9, 0x1BA, 0x12C, 0x15F, 0x1E2, 0x191, 0x107,
    0x174, 0x02B, 0x058, 0x0CE, 0x0BD, 0x1B7, 0x1C4, 0x152, 0x121, 0x07E, 0x00D,
    0x09B, 0x0E8, 0x055, 0x026, 0x0B0, 0x0C3, 0x19C, 0x1EF, 0x179, 0x10A, 0x11E,
    0x16D, 0x1FB, 0x188, 0x0D7, 0x0A4, 0x032, 0x041, 0x0FC, 0x08F, 0x019, 0x06A,
    0x135, 0x146, 0x1D0, 0x1A3, 0x0A9, 0x0DA, 0x04C, 0x03F, 0x160, 0x113, 0x185,
    0x1F6, 0x14B, 0x138, 0x1AE, 0x1DD, 0x082, 0x0F1, 0x067, 0x014, 0x04F, 0x03C,
    0x0AA, 0x0D9, 0x186, 0x1F5, 0x163, 0x110, 0x1AD, 0x1DE, 0x148, 0x13B, 0x064,
    0x017, 0x081, 0x0F2, 0x1F8, 0x18B, 0x11D, 0x16E, 0x031, 0x042, 0x0D4, 0x0A7,
    0x01A, 0x069, 0x0FF, 0x08C, 0x1D3, 0x1A0, 0x136, 0x145, 0x151, 0x122, 0x1B4,
    0x1C7, 0x098, 0x0EB, 0x07D, 0x00E, 0x0B3, 0x0C0, 0x056, 0x025, 0x17A, 0x109,
    0x19F, 0x1EC, 0x0E6, 0x095, 0x003, 0x070, 0x12F, 0x15C, 0x1CA, 0x1B9, 0x104,
    0x177, 0x1E1, 0x192, 0x0CD, 0x0BE, 0x028, 0x05B,
};

/// Reed-Solomon (12,9) generator polynomial, MMDVMHost RS129.cpp
static const uint8_t RS129_POLY[3] = { 64, 56, 14 };

static inline uint8_t countBits(uint32_t value)
{
    uint8_t count = 0;

    while (value != 0) {
        value &= value - 1;
        count++;
    }

    return count;
}

static inline uint32_t golayCodeWord(uint8_t data)
{
    return (static_cast<uint32_t>(data) << 12) | GOLAY2087_PARITY[data];
}

uint32_t DMR::golay2087Encode(uint8_t data)
{
    return golayCodeWord(data);
}

uint8_t DMR::golay2087Decode(uint32_t code, uint8_t *errors)
{
    uint8_t best = 0;
    uint8_t bestDist = 0xFF;

    code &= 0xFFFFF;

    for (uint32_t d = 0; d < 256; d++) {
        uint8_t dist = countBits(code ^ golayCodeWord(d));

        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<uint8_t>(d);
        }
    }

    if (errors != nullptr)
        *errors = bestDist;

    return best;
}

static inline uint16_t qrCodeWord(uint8_t data)
{
    return static_cast<uint16_t>((data << 9) | QR1676_PARITY[data]);
}

uint16_t DMR::qr1676Encode(uint8_t data)
{
    return qrCodeWord(data & 0x7F);
}

uint8_t DMR::qr1676Decode(uint16_t code, uint8_t *errors)
{
    uint8_t best = 0;
    uint8_t bestDist = 0xFF;

    for (uint8_t d = 0; d < 128; d++) {
        uint8_t dist = countBits(code ^ qrCodeWord(d));

        if (dist < bestDist) {
            bestDist = dist;
            best = d;
        }
    }

    if (errors != nullptr)
        *errors = bestDist;

    return best;
}

/**
 * Multiplication in GF(2^8) with the field polynomial x^8 + x^4 + x^3 + x^2
 * + 1 (0x11D), the field of MMDVMHost's RS129 tables.
 */
static uint8_t gfMult(uint8_t a, uint8_t b)
{
    uint8_t res = 0;

    while (b != 0) {
        if ((b & 0x01) != 0)
            res ^= a;

        bool carry = (a & 0x80) != 0;
        a = static_cast<uint8_t>(a << 1);
        if (carry)
            a ^= 0x1D;

        b >>= 1;
    }

    return res;
}

void DMR::rs129Encode(const uint8_t *msg, uint8_t *parity)
{
    uint8_t reg[3] = { 0, 0, 0 };

    for (size_t i = 0; i < 9; i++) {
        uint8_t dbyte = msg[i] ^ reg[2];

        reg[2] = reg[1] ^ gfMult(RS129_POLY[2], dbyte);
        reg[1] = reg[0] ^ gfMult(RS129_POLY[1], dbyte);
        reg[0] = gfMult(RS129_POLY[0], dbyte);
    }

    parity[0] = reg[2];
    parity[1] = reg[1];
    parity[2] = reg[0];
}

bool DMR::rs129Check(const uint8_t *data)
{
    uint8_t parity[3];
    rs129Encode(data, parity);

    return (data[9] == parity[0]) && (data[10] == parity[1])
        && (data[11] == parity[2]);
}

uint8_t DMR::crc5(const uint8_t *lc)
{
    uint16_t total = 0;

    for (size_t i = 0; i < LC_BYTES; i++)
        total += lc[i];

    return static_cast<uint8_t>(total % 31);
}

/*
 * Hamming codes of the BPTC (196,96) and of the embedded link control,
 * MMDVMHost Hamming.cpp. Bits are one per byte.
 */

static void hamming15113Encode(uint8_t *d)
{
    d[11] = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    d[12] = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    d[13] = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    d[14] = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
}

/**
 * Check and correct a row of the BPTC, Hamming (15,11,3) code.
 *
 * @return true if an error has been corrected.
 */
static bool hamming15113Decode(uint8_t *d)
{
    uint8_t c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    uint8_t c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    uint8_t c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];

    uint8_t n = 0;
    n |= (c0 != d[11]) ? 0x01 : 0x00;
    n |= (c1 != d[12]) ? 0x02 : 0x00;
    n |= (c2 != d[13]) ? 0x04 : 0x00;
    n |= (c3 != d[14]) ? 0x08 : 0x00;

    // Bit in error for each syndrome
    static const int8_t POS[16] = { -1, 11, 12, 8, 13, 5, 9, 3,
                                    14, 0,  6,  1, 10, 7, 4, 2 };

    if (n == 0)
        return false;

    d[POS[n]] ^= 0x01;
    return true;
}

/**
 * Write the parity bits of a column of the BPTC, Hamming (13,9,3) code. The
 * column bits are 15 bits apart in the matrix.
 *
 * @param m: BPTC matrix.
 * @param col: position of the first column bit.
 */
static void hamming1393Encode(uint8_t *m, size_t col)
{
    uint8_t d[9];
    for (size_t i = 0; i < 9; i++)
        d[i] = m[col + (i * 15)];

    m[col + (9 * 15)] = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6];
    m[col + (10 * 15)] = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7];
    m[col + (11 * 15)] = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    m[col + (12 * 15)] = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8];
}

/**
 * Check and correct a column of the BPTC, Hamming (13,9,3) code.
 *
 * @param m: BPTC matrix.
 * @param col: position of the first column bit.
 * @return true if an error has been corrected.
 */
static bool hamming1393Decode(uint8_t *m, size_t col)
{
    uint8_t d[13];
    for (size_t i = 0; i < 13; i++)
        d[i] = m[col + (i * 15)];

    uint8_t c0 = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6];
    uint8_t c1 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7];
    uint8_t c2 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c3 = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8];

    uint8_t n = 0;
    n |= (c0 != d[9]) ? 0x01 : 0x00;
    n |= (c1 != d[10]) ? 0x02 : 0x00;
    n |= (c2 != d[11]) ? 0x04 : 0x00;
    n |= (c3 != d[12]) ? 0x08 : 0x00;

    // Bit in error for each syndrome, -1 for the uncorrectable ones
    static const int8_t POS[16] = { -1, 9,  10, 6,  11, 3, 7, 1,
                                    12, -1, 4,  -1, 8,  5, 2, 0 };

    if (POS[n] < 0)
        return false;

    m[col + (POS[n] * 15)] ^= 0x01;
    return true;
}

void DMR::hamming16114Encode(uint8_t *d)
{
    d[11] = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    d[12] = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    d[13] = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    d[14] = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
    d[15] = d[0] ^ d[2] ^ d[5] ^ d[6] ^ d[8] ^ d[9] ^ d[10];
}

bool DMR::hamming16114Decode(uint8_t *d)
{
    uint8_t c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    uint8_t c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    uint8_t c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
    uint8_t c4 = d[0] ^ d[2] ^ d[5] ^ d[6] ^ d[8] ^ d[9] ^ d[10];

    uint8_t n = 0;
    n |= (c0 != d[11]) ? 0x01 : 0x00;
    n |= (c1 != d[12]) ? 0x02 : 0x00;
    n |= (c2 != d[13]) ? 0x04 : 0x00;
    n |= (c3 != d[14]) ? 0x08 : 0x00;
    n |= (c4 != d[15]) ? 0x10 : 0x00;

    if (n == 0)
        return true;

    // Syndrome of a single error on each bit, data then parity bits
    static const uint8_t SYNDROMES[16] = { 0x19, 0x0B, 0x1F, 0x07, 0x0E, 0x15,
                                           0x1A, 0x0D, 0x13, 0x16, 0x1C, 0x01,
                                           0x02, 0x04, 0x08, 0x10 };

    for (size_t i = 0; i < 16; i++) {
        if (SYNDROMES[i] == n) {
            d[i] ^= 0x01;
            return true;
        }
    }

    return false;
}

/*
 * BPTC (196,96), MMDVMHost BPTC19696.cpp. The 196 coded bits are a reserved
 * bit followed by a matrix of 13 rows of 15 bits: rows 0 to 8 hold 11 data
 * bits and 4 row parity bits, rows 9 to 12 the column parity. The 96 data
 * bits fill the data positions from bit 4 (bits 1 to 3 are reserved). On
 * air, coded bit a is sent as interleaved bit (a * 181) % 196.
 */

/**
 * Burst bit carrying interleaved bit i: bits 0 to 97, then bits 166 to 263,
 * around the slot type and sync fields.
 */
static inline size_t bptcBurstBit(size_t i)
{
    return (i < 98) ? i : (i + 68);
}

/**
 * Position, among the coded bits, of data bit i (0 to 95).
 */
static inline size_t bptcDataPos(size_t i)
{
    if (i < 8)
        return 4 + i;

    size_t row = ((i - 8) / 11) + 1;
    return (row * 15) + 1 + ((i - 8) % 11);
}

void DMR::bptc19696Encode(const uint8_t *data, uint8_t *burst)
{
    uint8_t m[196];
    memset(m, 0x00, sizeof(m));

    for (size_t i = 0; i < 96; i++)
        m[bptcDataPos(i)] = (data[i / 8] >> (7 - (i % 8))) & 0x01;

    for (size_t r = 0; r < 9; r++)
        hamming15113Encode(&m[(r * 15) + 1]);

    for (size_t c = 0; c < 15; c++)
        hamming1393Encode(m, c + 1);

    for (size_t a = 0; a < 196; a++) {
        size_t pos = bptcBurstBit((a * 181) % 196);
        uint8_t mask = static_cast<uint8_t>(0x80 >> (pos % 8));

        if (m[a] != 0)
            burst[pos / 8] |= mask;
        else
            burst[pos / 8] &= static_cast<uint8_t>(~mask);
    }
}

bool DMR::bptc19696Decode(const uint8_t *burst, uint8_t *data)
{
    uint8_t m[196];

    for (size_t a = 0; a < 196; a++) {
        size_t pos = bptcBurstBit((a * 181) % 196);
        m[a] = (burst[pos / 8] >> (7 - (pos % 8))) & 0x01;
    }

    // Alternate column and row corrections while they find errors
    bool fixing = true;
    for (size_t pass = 0; fixing && (pass < 5); pass++) {
        fixing = false;

        for (size_t c = 0; c < 15; c++) {
            if (hamming1393Decode(m, c + 1))
                fixing = true;
        }

        for (size_t r = 0; r < 9; r++) {
            if (hamming15113Decode(&m[(r * 15) + 1]))
                fixing = true;
        }
    }

    memset(data, 0x00, 12);
    for (size_t i = 0; i < 96; i++) {
        if (m[bptcDataPos(i)] != 0)
            data[i / 8] |= static_cast<uint8_t>(0x80 >> (i % 8));
    }

    return fixing == false;
}
