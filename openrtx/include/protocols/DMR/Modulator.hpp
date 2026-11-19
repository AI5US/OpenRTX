/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017,2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2016 Colin Durbridge G4EML
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_MODULATOR_H
#define DMR_MODULATOR_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "protocols/DMR/Constants.hpp"
#include "protocols/M17/PwmCompensator.hpp"

namespace DMR
{

/**
 * 4FSK baseband pulse shaper for DMR, 48 kHz output sample rate.
 *
 * Each pair of bits is a symbol (01 = +3, 00 = +1, 10 = -1, 11 = -3)
 * filtered by a root raised cosine filter with roll-off 0.2, as in MMDVM's
 * DMR transmitter. The output is the frequency modulating signal: its level
 * for a long run of +3 symbols gives a +1944 Hz deviation, with the same
 * scaling as the M17 modulator. This class has no dependency on the audio
 * subsystem.
 */
class FskShaper
{
public:
    /// Output sample rate, in Hz
    static constexpr uint32_t SAMPLE_RATE = 48000;

    /// Output samples generated for each symbol
    static constexpr size_t SAMPLES_PER_SYMBOL = SAMPLE_RATE / SYMBOL_RATE;

    /// Delay, in symbols, between a symbol input and its pulse centre
    static constexpr size_t DELAY_SYMBOLS = 4;

    /**
     * \brief Output level for a long run of +1 symbols (648 Hz).
     *
     * The M17 modulator gives 7279.04 per symbol unit, a unit being 800 Hz
     * of deviation (see DStar::GmskShaper::LEVEL). A DMR symbol unit is
     * 648 Hz: 7279.04 * 648 / 800 = 5896.
     */
    static constexpr int32_t UNIT_LEVEL = 5896;

    /**
     * Constructor.
     */
    FskShaper();

    /**
     * \brief Clear the filter history, as if a long run of zero (centre
     * frequency) symbols had been sent.
     */
    void reset();

    /**
     * \brief Shape a symbol.
     *
     * @param dibit: symbol bits, 0 to 3, first bit in the most significant
     * position.
     * @param out: buffer receiving SAMPLES_PER_SYMBOL samples.
     * @return number of samples written, SAMPLES_PER_SYMBOL.
     */
    size_t process(uint8_t dibit, int16_t *out);

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
    /// Symbols in the filter history
    static constexpr size_t HISTORY = 9;

    /**
     * Push a symbol in the filter history and generate its samples.
     *
     * @param symbol: symbol value, -3 to +3.
     * @param out: buffer receiving SAMPLES_PER_SYMBOL samples.
     * @return number of samples written.
     */
    size_t shape(int8_t symbol, int16_t *out);

    int8_t history[HISTORY]; ///< Current symbol and the previous ones
    bool inverted;           ///< Output phase inversion
};

/**
 * Modulator device for DMR: 4FSK modulation of a bit stream and output of
 * the baseband signal towards the RTX.
 */
class Modulator
{
public:
    /**
     * Function called for each block of baseband the output stream plays,
     * from the transmitting thread, so that the TDMA timing of the radio,
     * e.g. switching the power amplifier between the bursts, can follow the
     * output samples exactly. It is called while the block plays, once the
     * next one is ready: it can wait for an instant within the block without
     * starving the stream.
     *
     * @param ctx: context given to setBlockCallback().
     * @param sample: index, counted from start(), of the first sample of the
     * block playing.
     * @param tick: system tick when the block started playing.
     */
    using BlockCallback = void (*)(void *ctx, uint32_t sample, long long tick);

    /// Samples of a baseband block: 20 ms, 96 symbols
    static constexpr size_t BLOCK_SAMPLES = 96 * FskShaper::SAMPLES_PER_SYMBOL;

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
     * \brief Modulate and transmit a sequence of bytes, four symbols per
     * byte, first symbol in the two most significant bits. The function
     * blocks when the baseband buffer is full, until the output stream has
     * room for new data.
     *
     * @param data: bytes to be sent.
     * @param numBytes: number of bytes to be sent.
     */
    void send(const uint8_t *data, size_t numBytes);

    /**
     * \brief Transmit a burst followed by the idle pattern of the other
     * timeslot: 60 ms, a direct mode TDMA frame.
     *
     * @param burst: BURST_BYTES bytes.
     */
    void sendFrame(const uint8_t *burst);

    /**
     * \brief Transmit a burst followed by an unmodulated carrier in the
     * other timeslot: 60 ms, the TDMA frame of a radio talking through a
     * repeater, which must leave the other timeslot to other radios.
     *
     * @param burst: BURST_BYTES bytes.
     */
    void sendSlot(const uint8_t *burst);

    /**
     * \brief Transmit an unmodulated carrier, e.g. to place the next burst
     * in time.
     *
     * @param numSymbols: duration, in symbols.
     */
    void sendCarrier(size_t numSymbols);

    /**
     * \brief Transmit the preamble.
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
     * Set the function called each time a block starts playing.
     *
     * @param callback: function to call, nullptr for none.
     * @param ctx: context passed to the function.
     */
    void setBlockCallback(BlockCallback callback, void *ctx);

    /**
     * \brief Invert the baseband signal phase before output.
     *
     * @param invert: if true, the signal phase is inverted.
     */
    void invertPhase(bool invert);

private:
    /**
     * Shape a symbol into the current block, emitting it when full.
     *
     * @param dibit: symbol bits, or -1 for a zero (flush) symbol.
     */
    void pushSymbol(int dibit);

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

    /**
     * Call the block callback for the block playing, the one sent before
     * the block being sent, then wait for the stream to start playing the
     * latter. The stream plays one block of silence first.
     */
    void syncBlock();

    static constexpr uint32_t TX_SAMPLE_RATE = FskShaper::SAMPLE_RATE;

    FskShaper shaper;                          ///< Pulse shaping filter
    std::unique_ptr<int16_t[]> basebandBuffer; ///< Two baseband blocks
    int16_t *idleBuffer; ///< Block being filled, not in use by the stream
    BlockCallback blockCallback; ///< Called when a block starts playing
    void *blockCtx;              ///< Context of blockCallback
    long long playTick;          ///< System tick of the last block start
    size_t fill;                 ///< Samples already written in idleBuffer
    uint32_t blocksSent;         ///< Blocks sent since start()
    streamId outStream;          ///< Baseband output stream ID
    pathId outPath;              ///< Baseband output path ID
    bool txRunning;              ///< Transmission running

#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    PwmCompensator pwmComp;
#endif
};

} // namespace DMR

#endif // DMR_MODULATOR_H
