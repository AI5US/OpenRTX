/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2016 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_CONVOLUTION_H
#define YSF_CONVOLUTION_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace YSF
{

/**
 * Rate 1/2, constraint length 5 convolutional code of the FICH and the DCH,
 * with a hard decision Viterbi decoder (MMDVMHost YSFConvolution.cpp).
 */
class Convolution
{
public:
    /// Most coded bit pairs decoded at once: a full rate DCH
    static constexpr size_t MAX_STEPS = 180;

    /**
     * Constructor.
     */
    Convolution();

    /**
     * Destructor.
     */
    ~Convolution();

    /**
     * Start decoding a new sequence.
     */
    void start();

    /**
     * Feed the decoder a pair of coded bits. At most MAX_STEPS pairs per
     * sequence.
     *
     * @param s0: first coded bit.
     * @param s1: second coded bit.
     */
    void decode(uint8_t s0, uint8_t s1);

    /**
     * Trace back the decoded bits once all the pairs have been fed.
     *
     * @param out: output, first bit in the most significant position.
     * @param numBits: number of bits to output.
     */
    void chainback(uint8_t *out, size_t numBits);

    /**
     * Encode a sequence of bits.
     *
     * @param in: input bits, first bit in the most significant position.
     * @param out: output, two coded bits per input bit.
     * @param numBits: number of input bits.
     */
    void encode(const uint8_t *in, uint8_t *out, size_t numBits) const;

private:
    static constexpr size_t NUM_STATES = 16;
    static constexpr size_t NUM_STATES_D2 = NUM_STATES / 2;

    std::array<uint16_t, NUM_STATES> metrics1; ///< Path metrics
    std::array<uint16_t, NUM_STATES> metrics2; ///< Path metrics
    uint16_t *oldMetrics;                      ///< Metrics of the previous step
    uint16_t *newMetrics;                      ///< Metrics of the current step
    std::array<uint16_t, MAX_STEPS> decisions; ///< Survivor bits per step
    size_t steps;                              ///< Pairs decoded so far
};

} // namespace YSF

#endif // YSF_CONVOLUTION_H
