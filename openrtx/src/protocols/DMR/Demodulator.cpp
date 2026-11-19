/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DMR/Demodulator.hpp"

using namespace DMR;

/*
 * Ported from MMDVM DMRDMORX.cpp and IO.cpp, with the opposite sign
 * convention: here a positive sample is a positive symbol (+3), as on air
 * and in the M17 demodulator.
 *
 * The sample history is one TDMA frame (60 ms) long, so a burst found at a
 * given position comes back at the same position in the following frames:
 * once locked, the receiver looks for the next sync within one sample of
 * the last one and emits a burst each time the history pointer reaches the
 * end of the burst.
 *
 * At 5 samples per symbol the sampling instants can be up to a tenth of a
 * symbol away from the symbol centres, which with a roll-off of 0.2 costs
 * about 1% of bit errors on a clean signal. Unlike MMDVM, the symbols are
 * therefore read between the samples, at the correlation peak of the last
 * sync, estimated by a parabola through the correlation at the samples
 * around it.
 */

/// Allowed sign errors in the sync symbols, and bit errors in the sync bits
static constexpr uint8_t MAX_SYNC_SIGN_ERRS = 2;
static constexpr uint8_t MAX_SYNC_BIT_ERRS = 3;

/// +1/+3 decision threshold, as a fraction of the +3 level: Q15(0.595)
static constexpr int32_t THRESHOLD_FACTOR = 19505;

/// Samples from the last sync symbol to the last burst symbol (54 symbols)
static constexpr size_t SYNC_TO_END_SYMBOLS = BURST_SYMBOLS - SYNC_SYMBOLS
                                            - (SYNC_START_BIT / 2);

/*
 * Receive filter: root raised cosine, roll-off 0.2, spanning 8 symbols at 5
 * samples per symbol (MMDVM IO.cpp RRC_0_2_FILTER), Q15 with unity DC gain
 * (the taps sum to 32768).
 */
static const int32_t RX_FILTER[41] = {
    181,  47,   -153,  -330,  -383,  -250,  50,   411,  665,  655,  308,
    -305, -968, -1373, -1222, -348,  1204,  3158, 5074, 6471, 6984, 6471,
    5074, 3158, 1204,  -348,  -1222, -1373, -968, -305, 308,  655,  665,
    411,  50,   -250,  -383,  -330,  -153,  47,   181,
};

/*
 * DC estimator, MMDVM IO.cpp DC_FILTER: [b, a] = butter(1, 0.001) in MATLAB,
 * a first order low pass whose output is subtracted from the input.
 */
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

void Receiver::reset()
{
    unlock();

    bitPtr = 0;
    dataPtr = 0;
    syncPtr = 0;
    endPtr = 0;
    syncPattern = MS_DATA_SYNC_SIGNS;
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
    haveEnd = false;
    control = Control::NONE;
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
            endOfBurst();

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

bool Receiver::isLocked() const
{
    return locked;
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

void Receiver::correlateSync(bool first)
{
    uint32_t signs = bitBuffer[bitPtr] & SYNC_SIGNS_MASK;
    uint8_t errs = countBits(signs ^ MS_DATA_SYNC_SIGNS);

    // The voice sync is the complement of the data sync
    bool data = (errs <= MAX_SYNC_SIGN_ERRS);
    bool voice = (errs >= (SYNC_SYMBOLS - MAX_SYNC_SIGN_ERRS));

    if ((data == false) && (voice == false))
        return;

    uint32_t pattern = data ? MS_DATA_SYNC_SIGNS : MS_VOICE_SYNC_SIGNS;

    // Sample of the first sync symbol
    size_t start =
        (dataPtr + FRAME_SAMPLES - ((SYNC_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
        % FRAME_SAMPLES;

    int32_t corr = 0;
    int16_t min = 16000;
    int16_t max = -16000;
    size_t ptr = start;

    for (size_t i = 0; i < SYNC_SYMBOLS; i++) {
        int32_t val = buffer[ptr];

        if (val > max)
            max = static_cast<int16_t>(val);
        if (val < min)
            min = static_cast<int16_t>(val);

        // Sync symbols are all +3 or -3
        if (((pattern >> (SYNC_SYMBOLS - 1 - i)) & 0x01) != 0)
            corr += 3 * val;
        else
            corr -= 3 * val;

        ptr += SAMPLES_PER_SYMBOL;
        if (ptr >= FRAME_SAMPLES)
            ptr -= FRAME_SAMPLES;
    }

    if (corr <= maxCorr)
        return;

    int16_t c = static_cast<int16_t>((max + min) / 2);
    int16_t t = static_cast<int16_t>(((max - c) * THRESHOLD_FACTOR) >> 15);

    // Check the sync bits with these decision levels
    uint8_t bits[SYNC_BITS / 8];
    samplesToBits(start, SYNC_SYMBOLS, bits, 0, c, t, 0);

    uint64_t sync = 0;
    for (size_t i = 0; i < sizeof(bits); i++)
        sync = (sync << 8) | bits[i];

    uint64_t expected = data ? MS_DATA_SYNC : MS_VOICE_SYNC;
    if (countBits(sync ^ expected) > MAX_SYNC_BIT_ERRS)
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
    control = data ? Control::DATA : Control::VOICE;
    syncPattern = pattern;
    syncPtr = dataPtr;

    // One sample after the last burst symbol, for the interpolation
    endPtr = (dataPtr + (SYNC_TO_END_SYMBOLS * SAMPLES_PER_SYMBOL) + 1)
           % FRAME_SAMPLES;
    haveEnd = true;
}

int32_t Receiver::correlate(size_t last, uint32_t pattern) const
{
    size_t ptr =
        (last + FRAME_SAMPLES - ((SYNC_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
        % FRAME_SAMPLES;
    int32_t corr = 0;

    for (size_t i = 0; i < SYNC_SYMBOLS; i++) {
        if (((pattern >> (SYNC_SYMBOLS - 1 - i)) & 0x01) != 0)
            corr += buffer[ptr];
        else
            corr -= buffer[ptr];

        ptr += SAMPLES_PER_SYMBOL;
        if (ptr >= FRAME_SAMPLES)
            ptr -= FRAME_SAMPLES;
    }

    return corr;
}

int16_t Receiver::estimateTiming() const
{
    int32_t prev = correlate((syncPtr + FRAME_SAMPLES - 1) % FRAME_SAMPLES,
                             syncPattern);
    int32_t peak = correlate(syncPtr, syncPattern);
    int32_t next = correlate((syncPtr + 1) % FRAME_SAMPLES, syncPattern);

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

void Receiver::samplesToBits(size_t start, size_t count, uint8_t *buffer,
                             size_t offset, int16_t centre, int16_t threshold,
                             int16_t timing) const
{
    for (size_t i = 0; i < count; i++) {
        int32_t sample = this->buffer[start];

        // Quadratic interpolation through the sample and its neighbours
        if (timing != 0) {
            size_t prevPos = (start + FRAME_SAMPLES - 1) % FRAME_SAMPLES;
            int32_t prev = this->buffer[prevPos];
            int32_t next = this->buffer[(start + 1) % FRAME_SAMPLES];
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

        for (size_t b = 0; b < 2; b++) {
            size_t pos = offset + (2 * i) + b;
            uint8_t mask = static_cast<uint8_t>(0x80 >> (pos % 8));
            bool bit = ((dibit >> (1 - b)) & 0x01) != 0;

            if (bit)
                buffer[pos / 8] |= mask;
            else
                buffer[pos / 8] &= static_cast<uint8_t>(~mask);
        }

        start += SAMPLES_PER_SYMBOL;
        if (start >= FRAME_SAMPLES)
            start -= FRAME_SAMPLES;
    }
}

void Receiver::endOfBurst()
{
    int32_t c = 0;
    int32_t t = 0;
    for (size_t i = 0; i < 4; i++) {
        c += centre[i];
        t += threshold[i];
    }

    c /= 4;
    t /= 4;

    // First symbol of the burst
    size_t start = (endPtr + FRAME_SAMPLES - 1
                    - ((BURST_SYMBOLS - 1) * SAMPLES_PER_SYMBOL))
                 % FRAME_SAMPLES;

    // Bursts without sync keep the timing of the last sync
    if (control != Control::NONE)
        timing = estimateTiming();

    Event ev;
    ev.type = Event::BURST;

    switch (control) {
        case Control::DATA:
        case Control::VOICE:
            ev.sync = (control == Control::DATA) ? SyncType::DATA :
                                                   SyncType::VOICE;
            samplesToBits(start, BURST_SYMBOLS, ev.burst, 0,
                          static_cast<int16_t>(c), static_cast<int16_t>(t),
                          timing);
            pushEvent(ev);
            locked = true;
            lostCount = 0;
            break;

        default:
            if (locked == false) {
                haveEnd = false;
                break;
            }

            lostCount++;
            if (lostCount >= MAX_LOST_FRAMES) {
                Event lost = {};
                lost.type = Event::LOST;
                pushEvent(lost);
                unlock();
                return;
            }

            ev.sync = SyncType::NONE;
            samplesToBits(start, BURST_SYMBOLS, ev.burst, 0,
                          static_cast<int16_t>(c), static_cast<int16_t>(t),
                          timing);
            pushEvent(ev);
            break;
    }

    // Next frame
    maxCorr = 0;
    control = Control::NONE;
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
    // Ensure proper termination of baseband sampling
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

    // Audio path closed, nothing to do
    if (audioPath_getStatus(basebandPath) != PATH_OPEN)
        return false;

    // Read samples from the ADC
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
