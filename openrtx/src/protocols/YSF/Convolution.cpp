/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2016 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/YSF/Bits.hpp"
#include "protocols/YSF/Convolution.hpp"

using namespace YSF;

/// Coded bits of each branch: generators 1 + D^3 + D^4 and 1 + D + D^2 + D^4
static constexpr uint8_t BRANCH_TABLE1[] = { 0, 0, 0, 0, 1, 1, 1, 1 };
static constexpr uint8_t BRANCH_TABLE2[] = { 0, 1, 1, 0, 0, 1, 1, 0 };

/// Metric of a branch with both coded bits wrong
static constexpr uint16_t M = 2;

constexpr size_t Convolution::MAX_STEPS;

Convolution::Convolution()
{
    start();
}

Convolution::~Convolution()
{
}

void Convolution::start()
{
    metrics1.fill(0);
    metrics2.fill(0);
    oldMetrics = metrics1.data();
    newMetrics = metrics2.data();
    steps = 0;
}

void Convolution::decode(uint8_t s0, uint8_t s1)
{
    uint16_t decision = 0;

    for (size_t i = 0; i < NUM_STATES_D2; i++) {
        size_t j = i * 2;
        uint16_t metric = (BRANCH_TABLE1[i] ^ s0) + (BRANCH_TABLE2[i] ^ s1);

        uint16_t m0 = oldMetrics[i] + metric;
        uint16_t m1 = oldMetrics[i + NUM_STATES_D2] + (M - metric);
        bool decision0 = (m0 >= m1);
        newMetrics[j] = decision0 ? m1 : m0;

        m0 = oldMetrics[i] + (M - metric);
        m1 = oldMetrics[i + NUM_STATES_D2] + metric;
        bool decision1 = (m0 >= m1);
        newMetrics[j + 1] = decision1 ? m1 : m0;

        decision |= static_cast<uint16_t>((decision1 ? 1u : 0u) << (j + 1));
        decision |= static_cast<uint16_t>((decision0 ? 1u : 0u) << j);
    }

    decisions[steps] = decision;
    steps++;

    uint16_t *tmp = oldMetrics;
    oldMetrics = newMetrics;
    newMetrics = tmp;
}

void Convolution::chainback(uint8_t *out, size_t numBits)
{
    uint32_t state = 0;
    size_t step = steps;

    while (numBits-- > 0) {
        step--;

        uint32_t i = state >> 4;
        uint32_t bit = (decisions[step] >> i) & 0x01;
        state = (bit << 7) | (state >> 1);

        writeBit(out, numBits, bit != 0);
    }
}

void Convolution::encode(const uint8_t *in, uint8_t *out, size_t numBits) const
{
    uint8_t d1 = 0;
    uint8_t d2 = 0;
    uint8_t d3 = 0;
    uint8_t d4 = 0;

    for (size_t i = 0; i < numBits; i++) {
        uint8_t d = readBit(in, i) ? 1 : 0;
        uint8_t g1 = (d + d3 + d4) & 0x01;
        uint8_t g2 = (d + d1 + d2 + d4) & 0x01;

        d4 = d3;
        d3 = d2;
        d2 = d1;
        d1 = d;

        writeBit(out, 2 * i, g1 != 0);
        writeBit(out, (2 * i) + 1, g2 != 0);
    }
}
