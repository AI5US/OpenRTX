/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017,2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DSTAR/Modulator.hpp"

#if defined(PLATFORM_LINUX)
#include <stdio.h>
#endif

using namespace DStar;

/*
 * GMSK frequency pulse, BT = 0.5 as in the D-STAR specification, at 10
 * samples per symbol: the response of a Gaussian filter to a one symbol long
 * NRZ pulse,
 *
 *   g(t) = (erf((t + T/2) / (sqrt(2) s)) - erf((t - T/2) / (sqrt(2) s))) / 2
 *
 * with s = sqrt(ln 2) / (2 pi BT) T = 0.26501 T. The pulse is truncated to
 * three symbols, |t| < 1.5 T, where it falls to 8e-5. Its overall standard
 * deviation, sqrt(T^2 / 12 + s^2) = 0.392 T, matches the one of MMDVM's
 * modulation filter, gaussfir(0.35, 1, 5) applied to impulses at 24 kHz
 * (s = sqrt(ln 2) / (2 pi 0.35) T = 0.379 T): this 48 kHz filter is the
 * equivalent of MMDVM's one, defined with the BT of the specification.
 *
 * Output sample j (0 to 9) of the current symbol period is taken at
 * t = (j + 0.5) T / 10 from the period start, the pulse of a symbol being
 * centred 1.5 T after the start of its period. TAPS[k][j] is
 * g((j + 0.5) / 10 + k - 1.5): k = 0 weights the current symbol, k = 1 the
 * previous one and k = 2 the one before it. Values are g scaled by 32768,
 * rounded, then the k = 1 value adjusted so that each phase sums to exactly
 * 32768: a long run of equal symbols then yields exactly +/-LEVEL.
 */
static const int32_t TAPS[3][GmskShaper::SAMPLES_PER_SYMBOL] = {
    { 6, 22, 76, 232, 622, 1466, 3057, 5661, 9361, 13931 },
    { 18831, 23385, 27031, 29479, 30680, 30680, 29479, 27031, 23385, 18831 },
    { 13931, 9361, 5661, 3057, 1466, 622, 232, 76, 22, 6 },
};

constexpr uint32_t GmskShaper::SAMPLE_RATE;
constexpr size_t GmskShaper::SAMPLES_PER_SYMBOL;
constexpr size_t GmskShaper::TAIL_SYMBOLS;
constexpr int32_t GmskShaper::LEVEL;

GmskShaper::GmskShaper() : inverted(false)
{
    reset();
}

void GmskShaper::reset()
{
    history[0] = 0;
    history[1] = 0;
    history[2] = 0;
}

size_t GmskShaper::process(bool bit, int16_t *out)
{
    // Bit 1 is sent as the negative level, as done by MMDVM
    // invert to match m17
    int8_t symbol = bit ? 1 : -1;

    if (inverted)
        symbol = -symbol;

    return shape(symbol, out);
}

size_t GmskShaper::flush(int16_t *out)
{
    return shape(0, out);
}

void GmskShaper::invertPhase(bool invert)
{
    inverted = invert;
}

size_t GmskShaper::shape(int8_t symbol, int16_t *out)
{
    history[2] = history[1];
    history[1] = history[0];
    history[0] = symbol;

    for (size_t j = 0; j < SAMPLES_PER_SYMBOL; j++) {
        int32_t acc = 0;

        for (size_t k = 0; k < 3; k++)
            acc += history[k] * TAPS[k][j];

        // |acc| <= 32768, so |LEVEL * acc| < 2^31 and the result <= LEVEL
        out[j] = static_cast<int16_t>((LEVEL * acc) / 32768);
    }

    return SAMPLES_PER_SYMBOL;
}

Modulator::Modulator()
    : idleBuffer(nullptr), fill(0), outStream(-1), outPath(-1), txRunning(false)
{
}

Modulator::~Modulator()
{
    terminate();
}

void Modulator::init()
{
    // Two baseband blocks, for double buffering: 3840 bytes
    basebandBuffer = std::make_unique<int16_t[]>(2 * BLOCK_SAMPLES);
    idleBuffer = basebandBuffer.get();
    fill = 0;
    txRunning = false;
    shaper.reset();

#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    pwmComp.reset();
#endif
}

void Modulator::terminate()
{
    // Terminate an ongoing stream, if present
    if (txRunning) {
#ifndef PLATFORM_LINUX
        audioStream_terminate(outStream);
#endif
        txRunning = false;
    }

    // Always ensure that the outgoing audio path is closed
    if (outPath >= 0) {
        audioPath_release(outPath);
        outPath = -1;
    }

    outStream = -1;
    idleBuffer = nullptr;
    fill = 0;
    basebandBuffer.reset();
}

void Modulator::start()
{
    if (txRunning || (basebandBuffer == nullptr))
        return;

    memset(basebandBuffer.get(), 0x00, 2 * BLOCK_SAMPLES * sizeof(int16_t));
    idleBuffer = basebandBuffer.get();
    fill = 0;
    shaper.reset();

#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    pwmComp.reset();
#endif

#ifndef PLATFORM_LINUX
    outPath = audioPath_request(SOURCE_MCU, SINK_RTX, PRIO_TX);
    if (outPath < 0)
        return;

    outStream = audioStream_start(outPath, basebandBuffer.get(),
                                  2 * BLOCK_SAMPLES, TX_SAMPLE_RATE,
                                  STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (outStream < 0) {
        audioPath_release(outPath);
        outPath = -1;
        return;
    }

    int16_t *idle = outputStream_getIdleBuffer(outStream);
    if (idle != nullptr)
        idleBuffer = idle;
#endif

    txRunning = true;
}

void Modulator::send(const uint8_t *data, size_t numBits)
{
    if (idleBuffer == nullptr)
        return;

    for (size_t i = 0; i < numBits; i++) {
        bool bit = ((data[i / 8] >> (i % 8)) & 0x01) != 0;
        int16_t *out = idleBuffer + fill;
        size_t count = shaper.process(bit, out);

        compensate(out, count);
        fill += count;

        // BLOCK_SAMPLES is a multiple of SAMPLES_PER_SYMBOL
        if (fill >= BLOCK_SAMPLES)
            sendBlock();
    }
}

void Modulator::sendPreamble(size_t numBytes)
{
    for (size_t i = 0; i < numBytes; i++)
        send(&PREAMBLE_BYTE, 8);
}

void Modulator::stop()
{
    if (txRunning == false)
        return;

    // Bring the signal back to the carrier centre frequency
    if (idleBuffer != nullptr) {
        for (size_t i = 0; i < GmskShaper::TAIL_SYMBOLS + 1; i++) {
            int16_t *out = idleBuffer + fill;
            size_t count = shaper.flush(out);

            compensate(out, count);
            fill += count;

            if (fill >= BLOCK_SAMPLES)
                sendBlock();
        }

        // Pad and send the last, partially filled, block
        if (fill > 0) {
            memset(idleBuffer + fill, 0x00,
                   (BLOCK_SAMPLES - fill) * sizeof(int16_t));
            sendBlock();
        }
    }

#ifndef PLATFORM_LINUX
    audioStream_stop(outStream);
#endif

    txRunning = false;
    outStream = -1;
    idleBuffer = basebandBuffer.get();
    fill = 0;
    shaper.reset();

    if (outPath >= 0) {
        audioPath_release(outPath);
        outPath = -1;
    }

#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    pwmComp.reset();
#endif
}

void Modulator::invertPhase(bool invert)
{
    shaper.invertPhase(invert);
}

void Modulator::compensate(int16_t *samples, size_t count)
{
#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    for (size_t i = 0; i < count; i++) {
        float elem = pwmComp(static_cast<float>(samples[i]));
        samples[i] = static_cast<int16_t>(elem);
    }
#else
    (void)samples;
    (void)count;
#endif
}

#ifndef PLATFORM_LINUX
void Modulator::sendBlock()
{
    fill = 0;

    if (txRunning == false)
        return;

    if (audioPath_getStatus(outPath) != PATH_OPEN)
        return;

    // Wait for the stream to release the other block, then swap them
    outputStream_sync(outStream, true);

    int16_t *idle = outputStream_getIdleBuffer(outStream);
    if (idle != nullptr)
        idleBuffer = idle;
}
#else
void Modulator::sendBlock()
{
    FILE *outfile = fopen("/tmp/dstar_output.raw", "ab");

    if (outfile != NULL) {
        fwrite(idleBuffer, sizeof(int16_t), BLOCK_SAMPLES, outfile);
        fclose(outfile);
    }

    fill = 0;
}
#endif
