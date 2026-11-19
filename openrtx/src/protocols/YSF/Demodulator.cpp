/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2020 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/YSF/Demodulator.hpp"

using namespace YSF;

/*
 * Ported from MMDVM YSFRX.cpp and IO.cpp, in the form of the DMR receiver
 * (protocols/DMR/Demodulator.cpp): a positive sample is a positive symbol,
 * the history is one frame long so that, once locked, the next sync is
 * looked for within one sample of the last one, and the symbols are read
 * between the samples at the correlation peak of the last sync.
 */

/// Allowed sign errors in the sync symbols, and bit errors in the sync bits
static constexpr uint8_t MAX_SYNC_SIGN_ERRS = 2;
static constexpr uint8_t MAX_SYNC_BIT_ERRS = 3;

/// +1/+3 decision threshold, as a fraction of the +3 level: Q15(0.595)
static constexpr int32_t THRESHOLD_FACTOR = 19505;

/// Sync symbols (MMDVM YSFDefines.h YSF_SYNC_SYMBOLS_VALUES)
static constexpr int8_t SYNC_VALUES[SYNC_SYMBOLS] = {
    -3, +3, +3, +1, +3, -3, +1, +3, -3, +1,
    -1, +3, +3, -1, +3, -3, +3, +1, -3, +3,
};

/// Receive filter, as in the DMR receiver: root raised cosine, roll-off
/// 0.2, 8 symbols at 5 samples per symbol, Q15 with unity DC gain
static const int32_t RX_FILTER[41] = {
    181,  47,   -153,  -330,  -383,  -250,  50,   411,  665,  655,  308,
    -305, -968, -1373, -1222, -348,  1204,  3158, 5074, 6471, 6984, 6471,
    5074, 3158, 1204,  -348,  -1222, -1373, -968, -305, 308,  655,  665,
    411,  50,   -250,  -383,  -330,  -153,  47,   181,
};

/// DC estimator, MMDVM IO.cpp DC_FILTER: [b, a] = butter(1, 0.001)
static constexpr float DC_B0 = 0.0015683342f;
static constexpr float DC_A1 = 0.9968633316f;

static uint8_t countBits(uint64_t value)
{
    uint8_t count = 0;

    while (value != 0) {
        value &= value - 1;
        count++;
    }

    return count;
}

constexpr uint32_t Receiver::SAMPLE_RATE;
constexpr size_t Receiver::SAMPLES_PER_SYMBOL;
constexpr size_t Receiver::MAX_LOST_FRAMES;
constexpr size_t Receiver::EVENT_QUEUE_SIZE;
constexpr size_t Receiver::FRAME_SAMPLES;
constexpr size_t Receiver::FILTER_TAPS;

Receiver::Receiver() : inverted(false)
{
    reset();
}

Receiver::~Receiver()
{
}

void Receiver::reset()
{
    unlock();

    bitPtr = 0;
    dataPtr = 0;
    syncPtr = 0;
    endPtr = 0;
    timing = 0;
    averagePtr = 0;
    dcLevel = 0.0f;
    dcPrevIn = 0.0f;
    filterPtr = 0;
    eventHead = 0;
    eventCount = 0;

    memset(centre, 0x00, sizeof(centre));
    memset(threshold, 0x00, sizeof(threshold));
    memset(filterBuffer, 0x00, sizeof(filterBuffer));
    memset(bitBuffer, 0x00, sizeof(bitBuffer));
    memset(buffer, 0x00, sizeof(buffer));
}

void Receiver::unlock()
{
    locked = false;
    haveSync = false;
    haveEnd = false;
    lostCount = 0;
    maxCorr = 0;
}

void Receiver::invertPhase(bool invert)
{
    inverted = invert;
}

void Receiver::process(const int16_t *samples, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        int16_t sample = filter(samples[i]);

        buffer[dataPtr] = sample;

        bitBuffer[bitPtr] <<= 1;
        if (sample > 0)
            bitBuffer[bitPtr] |= 0x01;

        if (locked == false) {
            correlateSync(true);
        } else {
            // Next sync expected one frame after the last one
            size_t min = (syncPtr + FRAME_SAMPLES - 1) % FRAME_SAMPLES;
            size_t max = (syncPtr + 1) % FRAME_SAMPLES;
            bool inWindow;

            if (min < max)
                inWindow = (dataPtr >= min) && (dataPtr <= max);
            else
                inWindow = (dataPtr >= min) || (dataPtr <= max);

            if (inWindow)
                correlateSync(false);
        }

        if (haveEnd && (dataPtr == endPtr))
            endOfFrame();

        dataPtr++;
        if (dataPtr >= FRAME_SAMPLES)
            dataPtr = 0;

        bitPtr++;
        if (bitPtr >= SAMPLES_PER_SYMBOL)
            bitPtr = 0;
    }
}

bool Receiver::popEvent(Event &ev)
{
    if (eventCount == 0)
        return false;

    ev = events[eventHead];
    eventHead = (eventHead + 1) % EVENT_QUEUE_SIZE;
    eventCount--;

    return true;
}

bool Receiver::hasEvents() const
{
    return eventCount > 0;
}

int16_t Receiver::filter(int16_t raw)
{
    float in = static_cast<float>(raw);

    if (inverted)
        in = -in;

    // DC removal
    dcLevel = (DC_B0 * (in + dcPrevIn)) + (DC_A1 * dcLevel);
    dcPrevIn = in;

    float noDc = in - dcLevel;
    if (noDc > 32767.0f)
        noDc = 32767.0f;
    if (noDc < -32768.0f)
        noDc = -32768.0f;

    filterBuffer[filterPtr] = static_cast<int16_t>(noDc);

    // Symmetric filter: the order of the history is irrelevant
    int32_t acc = 0;
    size_t pos = filterPtr;
    for (size_t i = 0; i < FILTER_TAPS; i++) {
        acc += RX_FILTER[i] * filterBuffer[pos];
        pos = (pos == 0) ? (FILTER_TAPS - 1) : (pos - 1);
    }

    filterPtr++;
    if (filterPtr >= FILTER_TAPS)
        filterPtr = 0;

    int32_t out = acc / 32768;
    if (out > 32767)
        out = 32767;
    if (out < -32768)
        out = -32768;

    return static_cast<int16_t>(out);
}

int32_t Receiver::correlate(size_t last) const
{
    size_t ptr =
        (last + FRAME_SAMPLES - ((SYNC_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
        % FRAME_SAMPLES;
    int32_t corr = 0;

    for (size_t i = 0; i < SYNC_SYMBOLS; i++) {
        corr += SYNC_VALUES[i] * buffer[ptr];

        ptr += SAMPLES_PER_SYMBOL;
        if (ptr >= FRAME_SAMPLES)
            ptr -= FRAME_SAMPLES;
    }

    return corr;
}

void Receiver::correlateSync(bool first)
{
    uint32_t signs = bitBuffer[bitPtr] & SYNC_SIGNS_MASK;
    if (countBits(signs ^ SYNC_SIGNS) > MAX_SYNC_SIGN_ERRS)
        return;

    int32_t corr = correlate(dataPtr);
    if (corr <= maxCorr)
        return;

    // Decision levels from the extremes of the sync, which holds +3 and -3
    size_t start =
        (dataPtr + FRAME_SAMPLES - ((SYNC_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
        % FRAME_SAMPLES;
    int16_t min = 16000;
    int16_t max = -16000;
    size_t ptr = start;

    for (size_t i = 0; i < SYNC_SYMBOLS; i++) {
        if (buffer[ptr] > max)
            max = buffer[ptr];
        if (buffer[ptr] < min)
            min = buffer[ptr];

        ptr += SAMPLES_PER_SYMBOL;
        if (ptr >= FRAME_SAMPLES)
            ptr -= FRAME_SAMPLES;
    }

    int16_t c = static_cast<int16_t>((max + min) / 2);
    int16_t t = static_cast<int16_t>(((max - c) * THRESHOLD_FACTOR) >> 15);

    // Check the sync bits with these decision levels
    uint8_t bits[SYNC_BYTES];
    samplesToBits(start, SYNC_SYMBOLS, bits, c, t, 0);

    uint64_t sync = 0;
    for (size_t i = 0; i < sizeof(bits); i++)
        sync = (sync << 8) | bits[i];

    if (countBits(sync ^ SYNC) > MAX_SYNC_BIT_ERRS)
        return;

    if (first) {
        for (size_t i = 0; i < 4; i++) {
            centre[i] = c;
            threshold[i] = t;
        }

        averagePtr = 0;
    } else {
        centre[averagePtr] = c;
        threshold[averagePtr] = t;
        averagePtr = (averagePtr + 1) % 4;
    }

    maxCorr = corr;
    haveSync = true;
    syncPtr = dataPtr;

    // One sample after the last frame symbol, for the interpolation
    endPtr =
        (dataPtr + ((FRAME_SYMBOLS - SYNC_SYMBOLS) * SAMPLES_PER_SYMBOL) + 1)
        % FRAME_SAMPLES;
    haveEnd = true;
}

int16_t Receiver::estimateTiming() const
{
    int32_t prev = correlate((syncPtr + FRAME_SAMPLES - 1) % FRAME_SAMPLES);
    int32_t peak = correlate(syncPtr);
    int32_t next = correlate((syncPtr + 1) % FRAME_SAMPLES);

    // Vertex of the parabola through the three points
    int32_t den = prev - (2 * peak) + next;
    if (den >= 0)
        return 0;

    int32_t offset = (128 * (prev - next)) / den;
    if (offset > 128)
        offset = 128;
    if (offset < -128)
        offset = -128;

    return static_cast<int16_t>(offset);
}

void Receiver::samplesToBits(size_t start, size_t count, uint8_t *bits,
                             int16_t centre, int16_t threshold,
                             int16_t timing) const
{
    for (size_t i = 0; i < count; i++) {
        int32_t sample = buffer[start];

        // Quadratic interpolation through the sample and its neighbours
        if (timing != 0) {
            size_t prevPos = (start + FRAME_SAMPLES - 1) % FRAME_SAMPLES;
            int32_t prev = buffer[prevPos];
            int32_t next = buffer[(start + 1) % FRAME_SAMPLES];
            int32_t slope = (next - prev) * timing;
            int32_t curve = (next - (2 * sample) + prev) * timing * timing;

            sample += (slope / 512) + (curve / 131072);
        }

        sample -= centre;
        uint8_t dibit;

        if (sample > threshold)
            dibit = 0x01; // +3
        else if (sample > 0)
            dibit = 0x00; // +1
        else if (sample > -threshold)
            dibit = 0x02; // -1
        else
            dibit = 0x03; // -3

        uint8_t shift = static_cast<uint8_t>(6 - (2 * (i % 4)));
        uint8_t mask = static_cast<uint8_t>(0x03 << shift);
        bits[i / 4] = static_cast<uint8_t>((bits[i / 4] & ~mask)
                                           | (dibit << shift));

        start += SAMPLES_PER_SYMBOL;
        if (start >= FRAME_SAMPLES)
            start -= FRAME_SAMPLES;
    }
}

void Receiver::endOfFrame()
{
    int32_t c = 0;
    int32_t t = 0;
    for (size_t i = 0; i < 4; i++) {
        c += centre[i];
        t += threshold[i];
    }

    c /= 4;
    t /= 4;

    // First symbol of the frame: the first sync symbol
    size_t start = (endPtr + FRAME_SAMPLES - 1
                    - ((FRAME_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
                 % FRAME_SAMPLES;

    // Frames without sync keep the timing of the last sync
    if (haveSync) {
        timing = estimateTiming();
        locked = true;
        lostCount = 0;
    } else {
        if (locked == false) {
            haveEnd = false;
            return;
        }

        lostCount++;
        if (lostCount >= MAX_LOST_FRAMES) {
            Event lost = {};
            lost.type = Event::LOST;
            pushEvent(lost);
            unlock();
            return;
        }
    }

    Event ev;
    ev.type = Event::FRAME;
    ev.sync = haveSync;
    samplesToBits(start, FRAME_SYMBOLS, ev.frame, static_cast<int16_t>(c),
                  static_cast<int16_t>(t), timing);
    pushEvent(ev);

    maxCorr = 0;
    haveSync = false;
}

void Receiver::pushEvent(const Event &ev)
{
    // Queue full: drop the oldest event
    if (eventCount == EVENT_QUEUE_SIZE) {
        eventHead = (eventHead + 1) % EVENT_QUEUE_SIZE;
        eventCount--;
    }

    events[(eventHead + eventCount) % EVENT_QUEUE_SIZE] = ev;
    eventCount++;
}

Demodulator::Demodulator() : basebandId(-1), basebandPath(-1)
{
}

Demodulator::~Demodulator()
{
    terminate();
}

void Demodulator::init()
{
    // Two blocks of baseband samples, 1920 bytes, and the receiver
    basebandBuffer = std::make_unique<int16_t[]>(2 * SAMPLE_BUF_SIZE);
    receiver = std::make_unique<Receiver>();
}

void Demodulator::terminate()
{
    stopBasebandSampling();

    basebandBuffer.reset();
    receiver.reset();
}

void Demodulator::startBasebandSampling()
{
    if ((basebandBuffer == nullptr) || (receiver == nullptr))
        return;

    basebandPath = audioPath_request(SOURCE_RTX, SINK_MCU, PRIO_RX);
    if (basebandPath < 0)
        return;

    basebandId = audioStream_start(basebandPath, basebandBuffer.get(),
                                   2 * SAMPLE_BUF_SIZE, Receiver::SAMPLE_RATE,
                                   STREAM_INPUT | BUF_CIRC_DOUBLE);

    receiver->reset();
}

void Demodulator::stopBasebandSampling()
{
    if (basebandId >= 0)
        audioStream_terminate(basebandId);

    if (basebandPath >= 0)
        audioPath_release(basebandPath);

    // Stream slots are reused once released: forget both ids
    basebandId = -1;
    basebandPath = -1;
}

bool Demodulator::update(bool invertPhase)
{
    if ((receiver == nullptr) || (basebandId < 0))
        return false;

    if (audioPath_getStatus(basebandPath) != PATH_OPEN)
        return false;

    dataBlock_t baseband = inputStream_getData(basebandId);
    if (baseband.data == NULL)
        return false;

    receiver->invertPhase(invertPhase);
    receiver->process(baseband.data, baseband.len);

    return receiver->hasEvents();
}

bool Demodulator::popEvent(Event &ev)
{
    if (receiver == nullptr)
        return false;

    return receiver->popEvent(ev);
}

void Demodulator::unlock()
{
    if (receiver != nullptr)
        receiver->unlock();
}
