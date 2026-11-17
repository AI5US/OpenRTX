/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Port of the radio header FEC of the MMDVM modem firmware (DStarTX.cpp,
 * DStarRX.cpp), adapted to the OpenRTX D-STAR bit order: bit i of a buffer
 * is bit (i % 8) of byte i / 8.
 *
 * Three bit sequences are involved:
 *
 * - header bits h[t], t = 0..327, bit (t % 8) of header byte t / 8, followed
 *   by two zero tail bits h[328], h[329];
 * - coded bits c[k], k = 0..659: c[2t] = G1 output and c[2t + 1] = G0 output
 *   for input bit h[t]. MMDVM keeps them MSB first in its "intermediate"
 *   buffer; here they are kept LSB first, only the index k matters;
 * - air bits a[i], i = 0..659, in transmission order: a[i] = c[P(i)] XOR
 *   s[i], with P the interleaver permutation and s the scrambler sequence.
 *
 * P(i) is MMDVM INTERLEAVE_TABLE_RX[2i] * 8 + INTERLEAVE_TABLE_RX[2i + 1]:
 * the coded bits are written row by row into 24 columns (27 full rows plus a
 * last row of 12 bits) and read column by column, so the first 12 columns
 * hold 28 bits and the other 12 hold 27 bits. interleave() computes it
 * instead of storing the 1320 byte table. MMDVM INTERLEAVE_TABLE_TX is the
 * same permutation, inverted, with air bits shifted by 4 because MMDVM
 * places the last 4 bits of the frame sync in the first transmitted byte.
 *
 * s[i] is bit (i % 8) of SCRAMBLE_TABLE[i / 8], i.e. MMDVM
 * SCRAMBLE_TABLE_RX (MMDVM SCRAMBLE_TABLE_TX is the same sequence shifted by
 * 4 bits, for the same reason). It is the x^7 + x^4 + 1 sequence also used
 * for the slow data.
 */

#include <cstring>
#include "protocols/DSTAR/Crc.hpp"
#include "protocols/DSTAR/HeaderCoder.hpp"

using namespace DStar;

namespace
{

/// Header bits fed to the convolutional encoder, tail bits included
constexpr size_t CODED_STEPS = HEADER_SIZE * 8 + 2;

/// Bytes needed to store one bit per encoder step
constexpr size_t STEP_BYTES = (CODED_STEPS + 7) / 8;

/// Initial path metric of the states other than the starting one
constexpr uint16_t UNREACHED_METRIC = 1000;

/// Scrambler sequence, first 660 bits of MMDVM SCRAMBLE_TABLE_RX
const uint8_t SCRAMBLE_TABLE[HEADER_FEC_SIZE] = {
    0x70, 0x4F, 0x93, 0x40, 0x64, 0x74, 0x6D, 0x30, 0x2B, 0xE7, 0x2D, 0x54,
    0x5F, 0x8A, 0x1D, 0x7F, 0xB8, 0xA7, 0x49, 0x20, 0x32, 0xBA, 0x36, 0x98,
    0x95, 0xF3, 0x16, 0xAA, 0x2F, 0xC5, 0x8E, 0x3F, 0xDC, 0xD3, 0x24, 0x10,
    0x19, 0x5D, 0x1B, 0xCC, 0xCA, 0x79, 0x0B, 0xD5, 0x97, 0x62, 0xC7, 0x1F,
    0xEE, 0x69, 0x12, 0x88, 0x8C, 0xAE, 0x0D, 0x66, 0xE5, 0xBC, 0x85, 0xEA,
    0x4B, 0xB1, 0xE3, 0x0F, 0xF7, 0x34, 0x09, 0x44, 0x46, 0xD7, 0x06, 0xB3,
    0x72, 0xDE, 0x42, 0xF5, 0xA5, 0xD8, 0xF1, 0x87, 0x7B, 0x9A, 0x04
};

inline bool getBit(const uint8_t *buf, size_t i)
{
    return ((buf[i / 8] >> (i % 8)) & 0x01) != 0;
}

inline void setBit(uint8_t *buf, size_t i)
{
    buf[i / 8] |= static_cast<uint8_t>(1 << (i % 8));
}

/**
 * Interleaver permutation.
 *
 * @param i: air bit index, 0 to HEADER_FEC_BITS - 1.
 * @return index of the coded bit sent as air bit i.
 */
inline size_t interleave(size_t i)
{
    size_t column;
    size_t row;

    if (i < 12 * 28) {
        column = i / 28;
        row = i % 28;
    } else {
        column = 12 + (i - 12 * 28) / 27;
        row = (i - 12 * 28) % 27;
    }

    return column + 24 * row;
}

} // namespace

void DStar::encodeHeader(const uint8_t header[HEADER_SIZE],
                         uint8_t out[HEADER_FEC_SIZE])
{
    uint8_t coded[HEADER_FEC_SIZE];
    memset(coded, 0x00, sizeof(coded));

    // Convolutional encoder, d1 is the previous input bit, d2 the one before
    uint8_t d1 = 0;
    uint8_t d2 = 0;

    for (size_t t = 0; t < CODED_STEPS; t++) {
        uint8_t d = 0;
        if (t < HEADER_SIZE * 8)
            d = getBit(header, t) ? 1 : 0;

        uint8_t g1 = d ^ d1 ^ d2;
        uint8_t g0 = d ^ d2;

        if (g1 != 0)
            setBit(coded, 2 * t);
        if (g0 != 0)
            setBit(coded, 2 * t + 1);

        d2 = d1;
        d1 = d;
    }

    // Interleave and scramble
    memset(out, 0x00, HEADER_FEC_SIZE);

    for (size_t i = 0; i < HEADER_FEC_BITS; i++) {
        bool bit = getBit(coded, interleave(i));
        if (bit != getBit(SCRAMBLE_TABLE, i))
            setBit(out, i);
    }
}

bool DStar::decodeHeader(const uint8_t in[HEADER_FEC_SIZE],
                         uint8_t header[HEADER_SIZE])
{
    // Descramble and deinterleave
    uint8_t coded[HEADER_FEC_SIZE];
    memset(coded, 0x00, sizeof(coded));

    for (size_t i = 0; i < HEADER_FEC_BITS; i++) {
        if (getBit(in, i) != getBit(SCRAMBLE_TABLE, i))
            setBit(coded, interleave(i));
    }

    /*
     * Hard decision Viterbi decoder. State s = d1 + 2 * d2, where d1 is the
     * last input bit and d2 the one before. State s is reached, with input
     * bit d = s & 1, from the states p0 = s >> 1 (d2 = 0) and p1 = p0 + 2
     * (d2 = 1). survivors[s] bit t is set when the best path to state s at
     * step t comes from p1. The encoder starts in state 0. On equal metrics
     * p0 wins, as in MMDVM CDStarRX::acs().
     */
    uint8_t survivors[4][STEP_BYTES];
    memset(survivors, 0x00, sizeof(survivors));

    uint16_t metric[4] = { 0, UNREACHED_METRIC, UNREACHED_METRIC,
                           UNREACHED_METRIC };

    for (size_t t = 0; t < CODED_STEPS; t++) {
        uint8_t r1 = getBit(coded, 2 * t) ? 1 : 0;
        uint8_t r0 = getBit(coded, 2 * t + 1) ? 1 : 0;
        uint16_t newMetric[4];

        for (uint8_t s = 0; s < 4; s++) {
            uint8_t d = s & 1;
            uint8_t d1 = s >> 1;
            uint8_t p0 = d1;
            uint8_t p1 = d1 | 2;

            // Expected outputs: G1 = d ^ d1 ^ d2, G0 = d ^ d2
            uint16_t m0 = metric[p0] + ((r1 ^ d ^ d1) & 1)
                        + ((r0 ^ d) & 1);
            uint16_t m1 = metric[p1] + ((r1 ^ d ^ d1 ^ 1) & 1)
                        + ((r0 ^ d ^ 1) & 1);

            if (m0 < m1) {
                newMetric[s] = m0;
            } else {
                newMetric[s] = m1;
                setBit(survivors[s], t);
            }
        }

        for (uint8_t s = 0; s < 4; s++)
            metric[s] = newMetric[s];
    }

    // Trace back from state 0, reached thanks to the zero tail bits
    memset(header, 0x00, HEADER_SIZE);

    uint8_t state = 0;
    for (size_t t = CODED_STEPS; t-- > 0;) {
        if ((t < HEADER_SIZE * 8) && ((state & 1) != 0))
            setBit(header, t);

        uint8_t prev = state >> 1;
        if (getBit(survivors[state], t))
            prev |= 2;

        state = prev;
    }

    uint16_t crc = crcCcitt(header, HEADER_SIZE - 2);

    return (header[HEADER_SIZE - 2] == (crc & 0xFF))
           && (header[HEADER_SIZE - 1] == (crc >> 8));
}
