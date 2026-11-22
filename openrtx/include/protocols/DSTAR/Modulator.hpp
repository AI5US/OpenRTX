/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017,2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_MODULATOR_H
#define DSTAR_MODULATOR_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "protocols/DSTAR/Constants.hpp"
#include "protocols/M17/PwmCompensator.hpp"

namespace DStar
{

/**
 * GMSK baseband pulse shaper for D-STAR, 48 kHz output sample rate.
 *
 * Each bit is turned into an NRZ symbol (bit 1 = negative level, bit 0 =
 * positive level, as MMDVM does) filtered by a Gaussian filter with BT = 0.5.
 * The output is the frequency modulating signal: its level for a long run of
 * equal bits gives a +/-1.2 kHz deviation, with the same scaling as the M17
 * modulator. This class has no dependency on the audio subsystem.
 */
class GmskShaper
{
public:
    /// Output sample rate, in Hz
    static constexpr uint32_t SAMPLE_RATE = 48000;

    /// Output samples generated for each bit
    static constexpr size_t SAMPLES_PER_SYMBOL = SAMPLE_RATE / SYMBOL_RATE;

    /// Delay, in symbols, between a bit input and its pulse centre (1.5)
    static constexpr size_t TAIL_SYMBOLS = 2;

    /**
     * \brief Output level for a long run of equal bits (+/-1.2 kHz).
     *
     * The M17 modulator feeds symbols of value +/-1, +/-3 multiplied by
     * 23000 to the 81 taps, 10 samples per symbol, root raised cosine
     * filter rrc_taps_48k. The sum of these taps is 3.164801, so the DC gain
     * from a symbol to the output samples is 23000 * 3.164801 / 10 = 7279.04
     * per symbol unit, a +/-3 symbol run giving +/-2.4 kHz. D-STAR deviation
     * is +/-1.2 kHz, that is +/-1.5 symbol units: 1.5 * 7279.04 = 10918.6.
     */
    static constexpr int32_t LEVEL = 10919;

    /**
     * Constructor.
     */
    GmskShaper();

    /**
     * \brief Clear the filter history, as if a long run of zero (centre
     * frequency) symbols had been sent.
     */
    void reset();

    /**
     * \brief Shape a single bit.
     *
     * @param bit: bit value.
     * @param out: buffer receiving SAMPLES_PER_SYMBOL samples.
     * @return number of samples written, SAMPLES_PER_SYMBOL.
     */
    size_t process(bool bit, int16_t *out);

    /**
     * \brief Shape a zero-valued symbol (carrier centre frequency), used to
     * flush the filter tail at the end of a transmission.
     *
     * @param out: buffer receiving SAMPLES_PER_SYMBOL samples.
     * @return number of samples written, SAMPLES_PER_SYMBOL.
     */
    size_t flush(int16_t *out);

    /**
     * \brief Invert the phase of the output signal.
     *
     * @param invert: if true, the output signal phase is inverted.
     */
    void invertPhase(bool invert);

private:
    /**
     * Push a symbol in the filter history and generate its samples.
     *
     * @param symbol: symbol value, -1, 0 or +1.
     * @param out: buffer receiving SAMPLES_PER_SYMBOL samples.
     * @return number of samples written.
     */
    size_t shape(int8_t symbol, int16_t *out);

    int8_t history[3]; ///< Current symbol and the two previous ones
    bool inverted;     ///< Output phase inversion
};

/**
 * Modulator device for the D-STAR protocol: GMSK modulation of a bit stream
 * and output of the baseband signal towards the RTX.
 */
class Modulator
{
public:
    /**
     * Constructor.
     */
    Modulator();

    /**
     * Destructor.
     */
    ~Modulator();

    /**
     * \brief Allocate the baseband buffer and initialise the modulator.
     */
    void init();

    /**
     * \brief Forcefully shut down the modulator and deallocate its buffer.
     */
    void terminate();

    /**
     * \brief Open the baseband output path and start the output stream.
     */
    void start();

    /**
     * \brief Modulate and transmit a sequence of bits. The function blocks
     * when the baseband buffer is full, until the output stream has room for
     * new data.
     *
     * @param data: bits to be sent, first bit in the least significant bit
     * of data[0].
     * @param numBits: number of bits to be sent.
     */
    void send(const uint8_t *data, size_t numBits);

    /**
     * \brief Transmit the bit synchronisation preamble.
     *
     * @param numBytes: number of PREAMBLE_BYTE to be sent.
     */
    void sendPreamble(size_t numBytes);

    /**
     * \brief Flush the filter tail and the pending samples, then stop the
     * transmission.
     */
    void stop();

    /**
     * \brief Invert the baseband signal phase before output.
     *
     * @param invert: if true, the signal phase is inverted.
     */
    void invertPhase(bool invert);

private:
    /**
     * Apply the output compensation, if any, to freshly shaped samples.
     *
     * @param samples: samples to be processed in place.
     * @param count: number of samples.
     */
    void compensate(int16_t *samples, size_t count);

    /**
     * Emit the current baseband block towards the output stage and get a
     * new one. Platform dependent, blocking.
     */
    void sendBlock();

    static constexpr uint32_t TX_SAMPLE_RATE = GmskShaper::SAMPLE_RATE;

    /// Samples of a baseband block: 20 ms, one voice frame
    static constexpr size_t BLOCK_SAMPLES = FRAME_BITS
                                          * GmskShaper::SAMPLES_PER_SYMBOL;

    GmskShaper shaper;                         ///< Pulse shaping filter
    std::unique_ptr<int16_t[]> basebandBuffer; ///< Two baseband blocks
    int16_t *idleBuffer; ///< Block being filled, not in use by the stream
    size_t fill;         ///< Samples already written in idleBuffer
    streamId outStream;  ///< Baseband output stream ID
    pathId outPath;      ///< Baseband output path ID
    bool txRunning;      ///< Transmission running

#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    PwmCompensator pwmComp;
#endif
};

} // namespace DStar

#endif // DSTAR_MODULATOR_H
