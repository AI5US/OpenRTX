/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2017 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DSTAR/Demodulator.hpp"
#include "protocols/DSTAR/HeaderCoder.hpp"

using namespace DStar;

/*
 * Ported from MMDVM DStarRX.cpp and IO.cpp.
 *
 * Bit order: as in MMDVM, each sampling phase has a shift register where
 * each new bit enters in the least significant position, a negative sample
 * being a bit 1. The last 24 (or 48) bits of a register are thus the last
 * received bits with the oldest one in the most significant position, the
 * layout of FRAME_SYNC_PATTERN, DATA_SYNC_PATTERN and END_SYNC_PATTERN.
 * Header and frame bits are instead stored in transmission order, first bit
 * in the least significant bit of the first byte (MMDVM samplesToBits() with
 * WRITE_BIT2), the D-STAR byte order.
 */

/// Allowed bit errors in the frame sync
static constexpr uint8_t FRAME_SYNC_ERRS = 2;

/// Allowed bit errors in the data sync, when receiving voice frames. A data
/// sync starting a late entry must be error free.
static constexpr uint8_t DATA_SYNC_ERRS = 2;

/// Allowed bit errors in the end sync
static constexpr uint8_t END_SYNC_ERRS = 1;

static constexpr uint32_t SYNC_MASK = 0x00FFFFFF;
static constexpr uint64_t END_SYNC_MASK = 0x0000FFFFFFFFFFFFULL;

/*
 * Receive filter, MMDVM IO.cpp GAUSSIAN_0_5_FILTER: gaussfir(0.5, 4, 5) in
 * MATLAB, Q15, unity DC gain (the taps sum to 32768).
 */
static const int32_t RX_FILTER[] = { 8,    104,  760, 3158, 7421, 9866,
                                     7421, 3158, 760, 104,  8 };

/*
 * DC estimator, MMDVM IO.cpp DC_FILTER: [b, a] = butter(1, 0.001) in MATLAB,
 * a first order low pass whose output is subtracted from the input.
 * b0 = b1 = 3367972 / 2^31, -a1 = 2140747704 / 2^31.
 */
static constexpr float DC_B0 = 0.0015683342f;
static constexpr float DC_A1 = 0.9968633316f;

/**
 * Count the bits set in a value.
 *
 * @param value: input value.
 * @return number of bits set.
 */
static uint8_t countBits(uint64_t value)
{
    uint8_t count = 0;

    while (value != 0) {
        value &= value - 1;
        count++;
    }

    return count;
}

/**
 * Set or clear a bit of a byte array, first bit in the least significant
 * bit of the first byte.
 *
 * @param buf: byte array.
 * @param pos: bit position.
 * @param value: bit value.
 */
static inline void writeBit(uint8_t *buf, size_t pos, bool value)
{
    uint8_t mask = static_cast<uint8_t>(1 << (pos % 8));

    if (value)
        buf[pos / 8] |= mask;
    else
        buf[pos / 8] &= static_cast<uint8_t>(~mask);
}

constexpr uint32_t Receiver::SAMPLE_RATE;
constexpr size_t Receiver::SAMPLES_PER_SYMBOL;
constexpr size_t Receiver::MAX_FRAMES;
constexpr size_t Receiver::EVENT_QUEUE_SIZE;
constexpr size_t Receiver::FRAME_SAMPLES;
constexpr size_t Receiver::SYNC_BITS;
constexpr size_t Receiver::FILTER_TAPS;

Receiver::Receiver() : inverted(false)
{
    reset();
}

void Receiver::reset()
{
    state = State::NONE;
    locked = false;
    bitPtr = 0;
    countdown = 0;
    dataPtr = 0;
    headerPtr = 0;
    startPtr = 0;
    syncPtr = 0;
    minSyncPtr = 0;
    maxSyncPtr = 0;
    frameCount = 0;
    maxFrameCorr = 0;
    maxDataCorr = 0;
    dcLevel = 0.0f;
    dcPrevIn = 0.0f;
    filterPtr = 0;
    eventHead = 0;
    eventCount = 0;

    memset(filterBuffer, 0x00, sizeof(filterBuffer));
    memset(bitBuffer, 0x00, sizeof(bitBuffer));
    memset(dataBuffer, 0x00, sizeof(dataBuffer));
    memset(headerBits, 0x00, sizeof(headerBits));
}

void Receiver::invertPhase(bool invert)
{
    inverted = invert;
}

void Receiver::process(const int16_t *samples, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        int16_t sample = filter(samples[i]);

        bitBuffer[bitPtr] <<= 1;
        if (sample < 0)
            bitBuffer[bitPtr] |= 0x01;

        dataBuffer[dataPtr] = sample;

        switch (state) {
            case State::HEADER:
                processHeader(sample);
                break;

            case State::DATA:
                processData();
                break;

            default:
                processNone(sample);
                break;
        }

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
    // This is from MMDVM which uses inverted phaes on comparision to M17
    // this is to match the decoding of M17
    float in = -static_cast<float>(raw);

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

    // Unity gain filter: |acc / 32768| <= 32768
    int32_t out = acc / 32768;
    if (out > 32767)
        out = 32767;

    return static_cast<int16_t>(out);
}

void Receiver::processNone(int16_t sample)
{
    // Fuzzy matching of the frame sync
    if (correlateFrameSync()) {
        countdown = 5;
        storeHeaderSample(sample);
        state = State::HEADER;
        return;
    }

    // Exact matching of the data sync, late entry
    if (correlateDataSync()) {
        state = State::DATA;
        locked = true;
    }
}

void Receiver::processHeader(int16_t sample)
{
    // Look for a better aligned frame sync on the following samples
    if (countdown > 0) {
        correlateFrameSync();
        countdown--;
    }

    storeHeaderSample(sample);

    // Sample of the last header bit, stored at dataPtr
    if (headerPtr == (HEADER_FEC_BITS * SAMPLES_PER_SYMBOL) + 1) {
        uint8_t header[HEADER_SIZE];

        if (decodeHeader(headerBits, header) == false) {
            // Wrong CRC, back to looking for syncs
            toNone();
            return;
        }

        Event ev = {};
        ev.type = Event::HEADER;
        ev.headerValid = true;
        memcpy(ev.header, header, HEADER_SIZE);
        pushEvent(ev);
        locked = true;

        // The first voice frame follows: its first bit is one symbol after
        // the last header bit, its last bit (the end of its data sync) one
        // frame after it, which is again at the current dataPtr.
        frameCount = 0;
        syncPtr = dataPtr;
        startPtr = (dataPtr + SAMPLES_PER_SYMBOL) % FRAME_SAMPLES;
        maxSyncPtr = (dataPtr + 1) % FRAME_SAMPLES;
        minSyncPtr = (dataPtr + FRAME_SAMPLES - 1) % FRAME_SAMPLES;
    }

    // Switch to voice frames well before the first data sync window opens
    if (headerPtr
        == (HEADER_FEC_BITS * SAMPLES_PER_SYMBOL) + (2 * SAMPLES_PER_SYMBOL)
               + 1) {
        maxDataCorr = 0;
        state = State::DATA;
    }
}

void Receiver::processData()
{
    // Fuzzy matching of the end sync
    uint64_t endBits = bitBuffer[bitPtr] & END_SYNC_MASK;
    if (countBits(endBits ^ END_SYNC_PATTERN) <= END_SYNC_ERRS) {
        Event ev = {};
        ev.type = Event::END;
        pushEvent(ev);
        toNone();
        return;
    }

    // Fuzzy matching of the data sync, around its expected position
    bool inWindow;
    if (minSyncPtr < maxSyncPtr)
        inWindow = (dataPtr >= minSyncPtr) && (dataPtr <= maxSyncPtr);
    else
        inWindow = (dataPtr >= minSyncPtr) || (dataPtr <= maxSyncPtr);

    if (inWindow)
        correlateDataSync();

    // No data sync for too long
    if (frameCount >= MAX_FRAMES) {
        Event ev = {};
        ev.type = Event::LOST;
        pushEvent(ev);
        toNone();
        return;
    }

    // A frame is complete one sample after its last bit
    if (dataPtr == maxSyncPtr) {
        Event ev = {};
        ev.type = Event::FRAME;
        ev.frameIndex = static_cast<int8_t>(frameCount % SUPERFRAME_FRAMES);

        size_t ptr = startPtr;
        for (size_t i = 0; i < FRAME_BITS; i++) {
            writeBit(ev.frame, i, dataBuffer[ptr] < 0);

            ptr += SAMPLES_PER_SYMBOL;
            if (ptr >= FRAME_SAMPLES)
                ptr -= FRAME_SAMPLES;
        }

        // Data sync just found: put it back free of errors
        if (frameCount == 0)
            memcpy(ev.frame + VOICE_SIZE, DATA_SYNC, SLOW_DATA_SIZE);

        pushEvent(ev);

        frameCount++;
        maxFrameCorr = 0;
        maxDataCorr = 0;
    }
}

void Receiver::storeHeaderSample(int16_t sample)
{
    // Header bit i is sampled (i + 1) symbols after the last frame sync bit
    if ((headerPtr >= SAMPLES_PER_SYMBOL)
        && ((headerPtr % SAMPLES_PER_SYMBOL) == 0)) {
        size_t bit = (headerPtr / SAMPLES_PER_SYMBOL) - 1;

        if (bit < HEADER_FEC_BITS)
            writeBit(headerBits, bit, sample < 0);
    }

    headerPtr++;
}

bool Receiver::correlateFrameSync()
{
    uint32_t bits = static_cast<uint32_t>(bitBuffer[bitPtr]) & SYNC_MASK;
    if (countBits(bits ^ FRAME_SYNC_PATTERN) > FRAME_SYNC_ERRS)
        return false;

    int32_t corr = correlate(FRAME_SYNC_PATTERN);
    if (corr <= maxFrameCorr)
        return false;

    maxFrameCorr = corr;
    headerPtr = 0;

    return true;
}

bool Receiver::correlateDataSync()
{
    uint8_t maxErrs = (state == State::DATA) ? DATA_SYNC_ERRS : 0;

    uint32_t bits = static_cast<uint32_t>(bitBuffer[bitPtr]) & SYNC_MASK;
    if (countBits(bits ^ DATA_SYNC_PATTERN) > maxErrs)
        return false;

    int32_t corr = correlate(DATA_SYNC_PATTERN);
    if (corr <= maxDataCorr)
        return false;

    maxDataCorr = corr;
    frameCount = 0;

    // The data sync ends the frame: the next one starts one symbol later,
    // this one is complete on the next sample.
    syncPtr = dataPtr;
    startPtr = (dataPtr + SAMPLES_PER_SYMBOL) % FRAME_SAMPLES;
    maxSyncPtr = (dataPtr + 1) % FRAME_SAMPLES;
    minSyncPtr = (dataPtr + FRAME_SAMPLES - 1) % FRAME_SAMPLES;

    return true;
}

int32_t Receiver::correlate(uint32_t pattern) const
{
    // Sample of the oldest pattern bit, SYNC_BITS - 1 symbols ago
    size_t ptr =
        (dataPtr + FRAME_SAMPLES - ((SYNC_BITS - 1) * SAMPLES_PER_SYMBOL))
        % FRAME_SAMPLES;
    int32_t corr = 0;

    for (size_t i = 0; i < SYNC_BITS; i++) {
        int32_t val = dataBuffer[ptr];

        // A bit 1 is a negative sample
        if (((pattern >> (SYNC_BITS - 1 - i)) & 0x01) != 0)
            corr -= val;
        else
            corr += val;

        ptr += SAMPLES_PER_SYMBOL;
        if (ptr >= FRAME_SAMPLES)
            ptr -= FRAME_SAMPLES;
    }

    return corr;
}

void Receiver::toNone()
{
    state = State::NONE;
    locked = false;
    maxFrameCorr = 0;
    maxDataCorr = 0;
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

bool Demodulator::isLocked() const
{
    if (receiver == nullptr)
        return false;

    return receiver->isLocked();
}
