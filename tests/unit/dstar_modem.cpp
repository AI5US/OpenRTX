/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocols/DSTAR/Constants.hpp"
#include "protocols/DSTAR/Crc.hpp"
#include "protocols/DSTAR/Demodulator.hpp"
#include "protocols/DSTAR/HeaderCoder.hpp"
#include "protocols/DSTAR/Modulator.hpp"

using namespace DStar;

static constexpr size_t PREAMBLE_BYTES = 20;
static constexpr size_t NUM_FRAMES = 30;

/*
 * Deterministic pseudo-random generator (Numerical Recipes LCG).
 */
class Lcg
{
public:
    explicit Lcg(uint32_t seed) : state(seed)
    {
    }

    uint32_t next()
    {
        state = (state * 1664525u) + 1013904223u;
        return state;
    }

    uint8_t nextByte()
    {
        return static_cast<uint8_t>(next() >> 24);
    }

private:
    uint32_t state;
};

/*
 * Bit sequence, first bit in the least significant bit of the first byte.
 */
class BitStream
{
public:
    void put(bool bit)
    {
        if ((count % 8) == 0)
            bytes.push_back(0);

        if (bit)
            bytes[count / 8] |= static_cast<uint8_t>(1 << (count % 8));

        count++;
    }

    void put(const uint8_t *data, size_t numBits)
    {
        for (size_t i = 0; i < numBits; i++)
            put(((data[i / 8] >> (i % 8)) & 0x01) != 0);
    }

    bool get(size_t pos) const
    {
        return ((bytes[pos / 8] >> (pos % 8)) & 0x01) != 0;
    }

    size_t size() const
    {
        return count;
    }

private:
    std::vector<uint8_t> bytes;
    size_t count = 0;
};

/*
 * A complete D-STAR transmission.
 */
struct Transmission {
    uint8_t header[HEADER_SIZE];
    std::vector<std::array<uint8_t, FRAME_SIZE>> frames;
    std::vector<size_t> frameStart; ///< First bit of each frame
    BitStream bits;
};

static Transmission makeTransmission(bool withEnd)
{
    Transmission tx;

    // Radio header: flags, RPT2, RPT1, YOUR, MY, MY suffix, CRC
    memset(tx.header, ' ', HEADER_SIZE);
    tx.header[0] = 0x00;
    tx.header[1] = 0x00;
    tx.header[2] = 0x00;
    memcpy(tx.header + 3, "DIRECT  ", 8);
    memcpy(tx.header + 11, "DIRECT  ", 8);
    memcpy(tx.header + 19, "CQCQCQ  ", 8);
    memcpy(tx.header + 27, "N0CALL  ", 8);
    memcpy(tx.header + 35, "TEST", 4);

    uint16_t crc = crcCcitt(tx.header, HEADER_SIZE - 2);
    tx.header[HEADER_SIZE - 2] = static_cast<uint8_t>(crc & 0xFF);
    tx.header[HEADER_SIZE - 1] = static_cast<uint8_t>(crc >> 8);

    uint8_t coded[HEADER_FEC_SIZE];
    encodeHeader(tx.header, coded);

    for (size_t i = 0; i < PREAMBLE_BYTES; i++)
        tx.bits.put(&PREAMBLE_BYTE, 8);

    tx.bits.put(FRAME_SYNC, FRAME_SYNC_BITS);
    tx.bits.put(coded, HEADER_FEC_BITS);

    Lcg rng(12345);
    for (size_t i = 0; i < NUM_FRAMES; i++) {
        std::array<uint8_t, FRAME_SIZE> frame;

        for (size_t j = 0; j < FRAME_SIZE; j++)
            frame[j] = rng.nextByte();

        if ((i % SUPERFRAME_FRAMES) == 0)
            memcpy(frame.data() + VOICE_SIZE, DATA_SYNC, SLOW_DATA_SIZE);

        tx.frames.push_back(frame);
        tx.frameStart.push_back(tx.bits.size());
        tx.bits.put(frame.data(), FRAME_BITS);
    }

    if (withEnd)
        tx.bits.put(END_SYNC, END_SYNC_BITS);

    return tx;
}

/*
 * GMSK modulation at 48 kHz, followed by a decimation to 24 kHz.
 */
static std::vector<int16_t> modulate(const BitStream &bits, bool invert)
{
    GmskShaper shaper;
    shaper.invertPhase(invert);

    std::vector<int16_t> tx;
    int16_t buf[GmskShaper::SAMPLES_PER_SYMBOL];

    for (size_t i = 0; i < bits.size(); i++) {
        size_t n = shaper.process(bits.get(i), buf);
        tx.insert(tx.end(), buf, buf + n);
    }

    for (size_t i = 0; i < 4; i++) {
        size_t n = shaper.flush(buf);
        tx.insert(tx.end(), buf, buf + n);
    }

    // Some trailing silence
    tx.resize(tx.size() + 400, 0);

    std::vector<int16_t> rx(tx.size() / 2);
    for (size_t i = 0; i < rx.size(); i++) {
        int32_t sum = static_cast<int32_t>(tx[2 * i]) + tx[(2 * i) + 1];
        rx[i] = static_cast<int16_t>(sum / 2);
    }

    return rx;
}

/*
 * Feed samples to a receiver, in chunks of varying size, collecting events.
 */
static std::vector<Event>
receive(Receiver &rx, const std::vector<int16_t> &samples, size_t start = 0)
{
    static const size_t CHUNKS[] = { 1, 7, 64, 333, 480, 5, 997, 123, 2, 250 };
    static const size_t NUM_CHUNKS = sizeof(CHUNKS) / sizeof(CHUNKS[0]);

    std::vector<Event> events;
    size_t pos = start;
    size_t chunk = 0;

    while (pos < samples.size()) {
        size_t len = CHUNKS[chunk % NUM_CHUNKS];
        if (len > (samples.size() - pos))
            len = samples.size() - pos;

        rx.process(samples.data() + pos, len);
        pos += len;
        chunk++;

        Event ev;
        while (rx.popEvent(ev))
            events.push_back(ev);
    }

    return events;
}

/*
 * Check the events of a complete reception: header, then frames from
 * firstFrame to the last one, then end of transmission.
 */
static void checkEvents(const std::vector<Event> &events,
                        const Transmission &tx, bool withHeader,
                        size_t firstFrame)
{
    size_t numFrames = NUM_FRAMES - firstFrame;
    size_t expected = numFrames + (withHeader ? 2 : 1);
    REQUIRE(events.size() == expected);

    size_t pos = 0;
    if (withHeader) {
        REQUIRE(events[0].type == Event::HEADER);
        REQUIRE(events[0].headerValid);
        REQUIRE(memcmp(events[0].header, tx.header, HEADER_SIZE) == 0);
        pos = 1;
    }

    for (size_t i = 0; i < numFrames; i++) {
        const Event &ev = events[pos + i];
        size_t index = (firstFrame + i) % SUPERFRAME_FRAMES;

        REQUIRE(ev.type == Event::FRAME);
        REQUIRE(static_cast<size_t>(ev.frameIndex) == index);
        REQUIRE(memcmp(ev.frame, tx.frames[firstFrame + i].data(), FRAME_SIZE)
                == 0);
    }

    REQUIRE(events.back().type == Event::END);
}

TEST_CASE("D-STAR GMSK shaper levels", "[dstar]")
{
    GmskShaper shaper;
    int16_t buf[GmskShaper::SAMPLES_PER_SYMBOL];

    REQUIRE(GmskShaper::SAMPLES_PER_SYMBOL == 10u);

    // A long run of equal bits reaches exactly the nominal level, with the
    // bit 1 on the positive side
    for (size_t i = 0; i < 4; i++)
        REQUIRE(shaper.process(true, buf) == GmskShaper::SAMPLES_PER_SYMBOL);

    for (size_t i = 0; i < GmskShaper::SAMPLES_PER_SYMBOL; i++)
        REQUIRE(buf[i] == GmskShaper::LEVEL);

    for (size_t i = 0; i < 4; i++)
        shaper.process(false, buf);

    for (size_t i = 0; i < GmskShaper::SAMPLES_PER_SYMBOL; i++)
        REQUIRE(buf[i] == -GmskShaper::LEVEL);

    // Inverted phase
    shaper.reset();
    shaper.invertPhase(true);
    for (size_t i = 0; i < 4; i++)
        shaper.process(true, buf);

    for (size_t i = 0; i < GmskShaper::SAMPLES_PER_SYMBOL; i++)
        REQUIRE(buf[i] == -GmskShaper::LEVEL);

    // Flushing brings the signal back to zero
    for (size_t i = 0; i < 3; i++)
        shaper.flush(buf);

    for (size_t i = 0; i < GmskShaper::SAMPLES_PER_SYMBOL; i++)
        REQUIRE(buf[i] == 0);
}

TEST_CASE("D-STAR modem loopback", "[dstar]")
{
    static Receiver rx;
    Transmission tx = makeTransmission(true);

    REQUIRE(sizeof(Receiver) < 8192u);

    SECTION("Clean signal")
    {
        rx.reset();
        rx.invertPhase(false);
        std::vector<Event> events = receive(rx, modulate(tx.bits, false));
        checkEvents(events, tx, true, 0);
        REQUIRE_FALSE(rx.isLocked());
    }

    SECTION("Inverted phase on both sides")
    {
        rx.reset();
        rx.invertPhase(true);
        std::vector<Event> events = receive(rx, modulate(tx.bits, true));
        checkEvents(events, tx, true, 0);
    }

    SECTION("Late entry")
    {
        // Start in the middle of frame 5: no header, the reception starts
        // with the data sync of frame 21
        rx.reset();
        rx.invertPhase(false);
        std::vector<int16_t> samples = modulate(tx.bits, false);
        size_t start = (tx.frameStart[5] + 40) * Receiver::SAMPLES_PER_SYMBOL;
        std::vector<Event> events = receive(rx, samples, start);
        checkEvents(events, tx, false, SUPERFRAME_FRAMES);
    }

    SECTION("Half amplitude and DC offset")
    {
        rx.reset();
        rx.invertPhase(false);
        std::vector<int16_t> samples = modulate(tx.bits, false);
        for (size_t i = 0; i < samples.size(); i++)
            samples[i] = static_cast<int16_t>((samples[i] / 2) + 2000);

        std::vector<Event> events = receive(rx, samples);
        checkEvents(events, tx, true, 0);
    }

    SECTION("Noise")
    {
        rx.reset();
        rx.invertPhase(false);
        std::vector<int16_t> samples = modulate(tx.bits, false);
        Lcg rng(42);
        for (size_t i = 0; i < samples.size(); i++) {
            int32_t noise = static_cast<int32_t>(rng.next() >> 20) - 2048;
            samples[i] = static_cast<int16_t>(samples[i] + noise);
        }

        std::vector<Event> events = receive(rx, samples);
        checkEvents(events, tx, true, 0);
    }
}

TEST_CASE("D-STAR modem loss of synchronisation", "[dstar]")
{
    static Receiver rx;
    Transmission tx = makeTransmission(false);

    // Transmission without end sync, followed by silence
    std::vector<int16_t> samples = modulate(tx.bits, false);
    samples.resize(samples.size()
                       + ((Receiver::MAX_FRAMES + 10) * FRAME_BITS
                          * Receiver::SAMPLES_PER_SYMBOL),
                   0);

    rx.reset();
    rx.invertPhase(false);
    std::vector<Event> events = receive(rx, samples);

    REQUIRE(events.size() > 2u);
    REQUIRE(events.front().type == Event::HEADER);
    REQUIRE(events.back().type == Event::LOST);
    REQUIRE_FALSE(rx.isLocked());

    // Frames until the last data sync, then MAX_FRAMES frames without it
    size_t frames = 0;
    for (const Event &ev : events) {
        REQUIRE(ev.type != Event::END);
        if (ev.type == Event::FRAME)
            frames++;
    }

    REQUIRE(frames == (SUPERFRAME_FRAMES + Receiver::MAX_FRAMES));
}
