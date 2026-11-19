/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017,2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2016 Colin Durbridge G4EML
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "interfaces/delays.h"
#include "protocols/DMR/Modulator.hpp"

#if defined(PLATFORM_LINUX)
#include <stdio.h>
#endif

using namespace DMR;

/*
 * Root raised cosine filter, roll-off 0.2, spanning 8 symbols at 10 samples
 * per symbol (MMDVM uses the same filter at 5 samples per symbol,
 * rcosdesign(0.2, 8, 5, 'sqrt')). Scaled so that the taps of each output
 * phase sum, on average, to FskShaper::UNIT_LEVEL: a long run of equal
 * symbols s gives an output level of s * UNIT_LEVEL.
 *
 * TAPS[j + 10 k] weights the symbol sent k symbols ago for output sample j
 * of the current symbol period.
 */
static const int16_t TAPS[81] = {
    163,   114,  42,   -45,   -138,  -227,  -298,  -340, -345,  -307,  -225,
    -105,  46,   210,  371,   507,   600,   632,   591,  472,   278,   22,
    -275,  -585, -874, -1104, -1239, -1247, -1103, -792, -314,  321,   1087,
    1946,  2850, 3747, 4579,  5292,  5840,  6184,  6301, 6184,  5840,  5292,
    4579,  3747, 2850, 1946,  1087,  321,   -314,  -792, -1103, -1247, -1239,
    -1104, -874, -585, -275,  22,    278,   472,   591,  632,   600,   507,
    371,   210,  46,   -105,  -225,  -307,  -345,  -340, -298,  -227,  -138,
    -45,   42,   114,  163,
};

/// Symbol value of each dibit: 01 = +3, 00 = +1, 10 = -1, 11 = -3
static const int8_t SYMBOLS[4] = { +1, +3, -1, -3 };

constexpr uint32_t FskShaper::SAMPLE_RATE;
constexpr size_t FskShaper::SAMPLES_PER_SYMBOL;
constexpr size_t FskShaper::DELAY_SYMBOLS;
constexpr int32_t FskShaper::UNIT_LEVEL;
constexpr size_t FskShaper::HISTORY;

FskShaper::FskShaper() : inverted(false)
{
    reset();
}

void FskShaper::reset()
{
    memset(history, 0x00, sizeof(history));
}

size_t FskShaper::process(uint8_t dibit, int16_t *out)
{
    int8_t symbol = SYMBOLS[dibit & 0x03];

    if (inverted)
        symbol = -symbol;

    return shape(symbol, out);
}

size_t FskShaper::flush(int16_t *out)
{
    return shape(0, out);
}

void FskShaper::invertPhase(bool invert)
{
    inverted = invert;
}

size_t FskShaper::shape(int8_t symbol, int16_t *out)
{
    memmove(&history[1], &history[0], HISTORY - 1);
    history[0] = symbol;

    for (size_t j = 0; j < SAMPLES_PER_SYMBOL; j++) {
        int32_t acc = 0;

        for (size_t k = 0; k < HISTORY; k++) {
            size_t tap = j + (k * SAMPLES_PER_SYMBOL);
            if (tap >= 81)
                break;

            acc += history[k] * TAPS[tap];
        }

        // Rare peaks of the filtered signal slightly exceed the 16 bit range
        if (acc > 32767)
            acc = 32767;
        if (acc < -32768)
            acc = -32768;

        out[j] = static_cast<int16_t>(acc);
    }

    return SAMPLES_PER_SYMBOL;
}

Modulator::Modulator()
    : idleBuffer(nullptr)
    , blockCallback(nullptr)
    , blockCtx(nullptr)
    , playTick(0)
    , fill(0)
    , blocksSent(0)
    , outStream(-1)
    , outPath(-1)
    , txRunning(false)
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
    blocksSent = 0;
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

void Modulator::send(const uint8_t *data, size_t numBytes)
{
    for (size_t i = 0; i < numBytes; i++) {
        uint8_t c = data[i];

        for (size_t s = 0; s < 4; s++) {
            pushSymbol((c >> 6) & 0x03);
            c = static_cast<uint8_t>(c << 2);
        }
    }
}

void Modulator::sendFrame(const uint8_t *burst)
{
    send(burst, BURST_BYTES);
    send(IDLE_FILL, IDLE_FILL_BYTES);
}

void Modulator::sendSlot(const uint8_t *burst)
{
    send(burst, BURST_BYTES);
    sendCarrier(FRAME_SYMBOLS - BURST_SYMBOLS);
}

void Modulator::sendCarrier(size_t numSymbols)
{
    for (size_t i = 0; i < numSymbols; i++)
        pushSymbol(-1);
}

void Modulator::sendPreamble(size_t numBytes)
{
    for (size_t i = 0; i < numBytes; i++)
        send(&PREAMBLE_BYTE, 1);
}

void Modulator::stop()
{
    if (txRunning == false)
        return;

    if (idleBuffer != nullptr) {
        // Bring the signal back to the carrier centre frequency
        for (size_t i = 0; i <= FskShaper::DELAY_SYMBOLS * 2; i++)
            pushSymbol(-1);

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

void Modulator::setBlockCallback(BlockCallback callback, void *ctx)
{
    blockCallback = callback;
    blockCtx = ctx;
}

void Modulator::syncBlock()
{
    if ((blockCallback != nullptr) && (blocksSent > 0))
        blockCallback(blockCtx, (blocksSent - 1) * BLOCK_SAMPLES, playTick);

#ifndef PLATFORM_LINUX
    outputStream_sync(outStream, true);
#endif

    playTick = getTick();
    blocksSent++;
}

void Modulator::invertPhase(bool invert)
{
    shaper.invertPhase(invert);
}

void Modulator::pushSymbol(int dibit)
{
    if (idleBuffer == nullptr)
        return;

    int16_t *out = idleBuffer + fill;
    size_t count;

    if (dibit < 0)
        count = shaper.flush(out);
    else
        count = shaper.process(static_cast<uint8_t>(dibit), out);

    compensate(out, count);
    fill += count;

    // BLOCK_SAMPLES is a multiple of SAMPLES_PER_SYMBOL
    if (fill >= BLOCK_SAMPLES)
        sendBlock();
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
    syncBlock();

    int16_t *idle = outputStream_getIdleBuffer(outStream);
    if (idle != nullptr)
        idleBuffer = idle;
}
#else
void Modulator::sendBlock()
{
    FILE *outfile = fopen("/tmp/dmr_output.raw", "ab");

    if (outfile != NULL) {
        fwrite(idleBuffer, sizeof(int16_t), BLOCK_SAMPLES, outfile);
        fclose(outfile);
    }

    fill = 0;
    syncBlock();
}
#endif
