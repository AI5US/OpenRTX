/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Demodulator.hpp"
#include "protocols/DMR/Modulator.hpp"
#include "protocols/DMR/Voice.hpp"

using namespace DMR;

static constexpr size_t PREAMBLE_BYTES = 30;
static constexpr size_t HEADERS = 2;
static constexpr size_t SUPERFRAMES = 4;

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

    /// Approximately gaussian, zero mean, unit variance
    double gauss()
    {
        double acc = 0.0;
        for (int i = 0; i < 12; i++)
            acc += static_cast<double>(next() >> 8) / 16777216.0;

        return acc - 6.0;
    }

private:
    uint32_t state;
};

/*
 * A complete direct mode transmission and its baseband signal.
 */
struct Transmission {
    Lc lc;
    std::vector<std::array<uint8_t, VOICE_FRAMES * VOICE_FRAME_BYTES>> voice;
    std::vector<int16_t> baseband; ///< 48 kHz
};

static void shapeBytes(FskShaper &shaper, const uint8_t *data, size_t len,
                       std::vector<int16_t> &out)
{
    int16_t samples[FskShaper::SAMPLES_PER_SYMBOL];

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        for (size_t s = 0; s < 4; s++) {
            shaper.process((c >> 6) & 0x03, samples);
            out.insert(out.end(), samples,
                       samples + FskShaper::SAMPLES_PER_SYMBOL);
            c = static_cast<uint8_t>(c << 2);
        }
    }
}

static void shapeFrame(FskShaper &shaper, const uint8_t *burst,
                       std::vector<int16_t> &out)
{
    shapeBytes(shaper, burst, BURST_BYTES, out);
    shapeBytes(shaper, IDLE_FILL, IDLE_FILL_BYTES, out);
}

static Transmission makeTransmission(uint8_t cc, bool invert, uint32_t seed)
{
    Transmission tx;
    tx.lc = {};
    tx.lc.flco = FLCO_GROUP;
    tx.lc.src = 2345678;
    tx.lc.dst = 9;

    FskShaper shaper;
    shaper.invertPhase(invert);

    // Leading silence, carrier without modulation
    int16_t samples[FskShaper::SAMPLES_PER_SYMBOL];
    for (size_t i = 0; i < 200; i++) {
        shaper.flush(samples);
        tx.baseband.insert(tx.baseband.end(), samples,
                           samples + FskShaper::SAMPLES_PER_SYMBOL);
    }

    for (size_t i = 0; i < PREAMBLE_BYTES; i++)
        shapeBytes(shaper, &PREAMBLE_BYTE, 1, tx.baseband);

    VoiceEncoder enc;
    enc.start(tx.lc, cc);

    uint8_t burst[BURST_BYTES];
    for (size_t i = 0; i < HEADERS; i++) {
        enc.header(burst);
        shapeFrame(shaper, burst, tx.baseband);
    }

    Lcg rng(seed);
    for (size_t s = 0; s < (SUPERFRAMES * SUPERFRAME_BURSTS); s++) {
        uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
        std::array<uint8_t, VOICE_FRAMES * VOICE_FRAME_BYTES> flat;

        for (size_t i = 0; i < flat.size(); i++)
            flat[i] = rng.nextByte();

        memcpy(frames, flat.data(), flat.size());
        tx.voice.push_back(flat);

        enc.voice(frames, burst);
        shapeFrame(shaper, burst, tx.baseband);
    }

    enc.terminator(burst);
    shapeFrame(shaper, burst, tx.baseband);

    // Trailing silence, long enough for the receiver to lose the sync
    for (size_t i = 0; i < 6000; i++) {
        shaper.flush(samples);
        tx.baseband.insert(tx.baseband.end(), samples,
                           samples + FskShaper::SAMPLES_PER_SYMBOL);
    }

    return tx;
}

/*
 * Channel model: decimation to 24 kHz from a given 48 kHz sample, gain, DC
 * offset and gaussian noise.
 */
static std::vector<int16_t> channel(const std::vector<int16_t> &in,
                                    size_t phase, double gain, double dc,
                                    double noise, uint32_t seed)
{
    std::vector<int16_t> out;
    Lcg rng(seed);

    for (size_t i = phase; i < in.size(); i += 2) {
        double v = (in[i] * gain) + dc + (noise * rng.gauss());

        if (v > 32767.0)
            v = 32767.0;
        if (v < -32768.0)
            v = -32768.0;

        out.push_back(static_cast<int16_t>(std::lround(v)));
    }

    return out;
}

static size_t countBits(uint8_t value)
{
    size_t count = 0;

    while (value != 0) {
        value &= static_cast<uint8_t>(value - 1);
        count++;
    }

    return count;
}

struct RxResult {
    size_t callStarts = 0;
    size_t lcCount = 0;
    size_t calls = 0;
    size_t voiceBursts = 0;
    size_t voiceErrors = 0; ///< Voice bursts not matching the transmitted
    size_t bitErrors = 0;   ///< Voice bits not matching the transmitted
    size_t lost = 0;
    Lc lc = {};
};

static RxResult receive(const Transmission &tx,
                        const std::vector<int16_t> &baseband, uint8_t cc,
                        bool invert)
{
    Receiver rx;
    rx.invertPhase(invert);

    VoiceDecoder dec;
    dec.setColorCode(cc);

    RxResult res;
    size_t voiceIndex = 0;

    // Blocks of 20 ms, as the demodulator processes them
    for (size_t pos = 0; pos < baseband.size(); pos += 480) {
        size_t len = std::min<size_t>(480, baseband.size() - pos);
        rx.process(&baseband[pos], len);

        Event ev;
        while (rx.popEvent(ev)) {
            if (ev.type == Event::LOST) {
                res.lost++;
                dec.reset();
                continue;
            }

            VoiceEvent vev;
            dec.process(ev.burst, ev.sync, vev);

            if (vev.callStart)
                res.callStarts++;

            if (vev.lcValid) {
                res.lcCount++;
                res.lc = vev.lc;
            }

            if (vev.voice) {
                if (voiceIndex >= tx.voice.size()) {
                    res.voiceErrors++;
                } else {
                    const uint8_t *rxd = &vev.frames[0][0];
                    const uint8_t *txd = tx.voice[voiceIndex].data();
                    size_t errs = 0;

                    for (size_t i = 0; i < tx.voice[voiceIndex].size(); i++)
                        errs += countBits(rxd[i] ^ txd[i]);

                    res.bitErrors += errs;
                    if (errs > 0)
                        res.voiceErrors++;
                }

                voiceIndex++;
                res.voiceBursts++;
            }

            if (vev.callEnd) {
                res.calls++;
                rx.unlock();
            }
        }
    }

    return res;
}

TEST_CASE("DMR shaper output levels", "[dmr][modem]")
{
    FskShaper shaper;
    int16_t out[FskShaper::SAMPLES_PER_SYMBOL];

    // A long run of +3 symbols (dibit 01) settles at 3 units (1944 Hz)
    for (size_t i = 0; i < 20; i++)
        shaper.process(0x01, out);

    for (size_t j = 0; j < FskShaper::SAMPLES_PER_SYMBOL; j++) {
        REQUIRE(out[j] > (3 * FskShaper::UNIT_LEVEL * 95) / 100);
        REQUIRE(out[j] < (3 * FskShaper::UNIT_LEVEL * 105) / 100);
    }

    // -1 symbols (dibit 10), inverted phase: +1 unit
    shaper.invertPhase(true);
    for (size_t i = 0; i < 20; i++)
        shaper.process(0x02, out);

    for (size_t j = 0; j < FskShaper::SAMPLES_PER_SYMBOL; j++) {
        REQUIRE(out[j] > (FskShaper::UNIT_LEVEL * 95) / 100);
        REQUIRE(out[j] < (FskShaper::UNIT_LEVEL * 105) / 100);
    }

    // Flushing brings the signal back to zero
    for (size_t i = 0; i < 20; i++)
        shaper.flush(out);

    for (size_t j = 0; j < FskShaper::SAMPLES_PER_SYMBOL; j++)
        REQUIRE(out[j] == 0);
}

TEST_CASE("DMR modem receives a clean transmission", "[dmr][modem]")
{
    Transmission tx = makeTransmission(1, false, 1);

    // All sample phases of the decimation
    for (size_t phase = 0; phase < 2; phase++) {
        auto bb = channel(tx.baseband, phase, 1.0, 0.0, 0.0, 1);
        RxResult res = receive(tx, bb, 1, false);

        REQUIRE(res.callStarts == 1);
        REQUIRE(res.lcCount == 1);
        REQUIRE(res.lc.src == tx.lc.src);
        REQUIRE(res.lc.dst == tx.lc.dst);
        REQUIRE(res.voiceBursts == tx.voice.size());
        REQUIRE(res.voiceErrors == 0);
        REQUIRE(res.calls == 1);
    }
}

TEST_CASE("DMR modem tolerates level, offset and timing", "[dmr][modem]")
{
    Transmission tx = makeTransmission(3, false, 2);
    const size_t voiceBits = tx.voice.size() * VOICE_FRAMES * VOICE_FRAME_BYTES
                           * 8;

    // Different 24 kHz sample timings: drop leading samples at 48 kHz
    for (size_t skip = 0; skip < 10; skip++) {
        std::vector<int16_t> shifted(tx.baseband.begin() + skip,
                                     tx.baseband.end());
        auto bb = channel(shifted, 0, 0.3, 1500.0, 0.0, 2);
        RxResult res = receive(tx, bb, 3, false);

        // Symbols are read between the samples, at the estimated symbol
        // centres: a few marginal symbols are left, well below 0.1%.
        INFO("skip " << skip << ", bit errors " << res.bitErrors);
        REQUIRE(res.callStarts == 1);
        REQUIRE(res.voiceBursts == tx.voice.size());
        REQUIRE(res.bitErrors < (voiceBits / 1000));
        REQUIRE(res.calls == 1);
    }
}

TEST_CASE("DMR modem handles phase inversion", "[dmr][modem]")
{
    Transmission tx = makeTransmission(1, true, 3);
    auto bb = channel(tx.baseband, 0, 1.0, 0.0, 0.0, 3);

    // Not inverted on receive: nothing decoded
    RxResult wrong = receive(tx, bb, 1, false);
    REQUIRE(wrong.voiceBursts == 0);

    RxResult res = receive(tx, bb, 1, true);
    REQUIRE(res.callStarts == 1);
    REQUIRE(res.voiceErrors == 0);
    REQUIRE(res.voiceBursts == tx.voice.size());
}

TEST_CASE("DMR modem works through noise", "[dmr][modem]")
{
    Transmission tx = makeTransmission(1, false, 4);

    // Noise at a fifth of the symbol unit: few, if any, voice bit errors
    double noise = FskShaper::UNIT_LEVEL / 5.0;
    auto bb = channel(tx.baseband, 1, 1.0, 0.0, noise, 4);
    RxResult res = receive(tx, bb, 1, false);

    const size_t voiceBits = tx.voice.size() * VOICE_FRAMES * VOICE_FRAME_BYTES
                           * 8;

    INFO("bit errors " << res.bitErrors);
    REQUIRE(res.callStarts == 1);
    REQUIRE(res.lc.src == tx.lc.src);
    REQUIRE(res.voiceBursts == tx.voice.size());
    REQUIRE(res.bitErrors < (voiceBits / 100));
    REQUIRE(res.calls == 1);
}

TEST_CASE("DMR modem filters the colour code", "[dmr][modem]")
{
    Transmission tx = makeTransmission(4, false, 5);
    auto bb = channel(tx.baseband, 0, 1.0, 0.0, 0.0, 5);

    RxResult res = receive(tx, bb, 5, false);
    REQUIRE(res.callStarts == 0);
    REQUIRE(res.voiceBursts == 0);
}

TEST_CASE("DMR modem loses sync without terminator", "[dmr][modem]")
{
    Transmission tx = makeTransmission(1, false, 6);
    auto bb = channel(tx.baseband, 0, 1.0, 0.0, 0.0, 6);

    // Cut the transmission in the middle of the voice: the receiver keeps
    // emitting bursts without sync for a while, then reports the loss.
    size_t cut = bb.size() / 2;
    for (size_t i = cut; i < bb.size(); i++)
        bb[i] = 0;

    Receiver rx;
    size_t lost = 0;
    size_t bursts = 0;

    for (size_t pos = 0; pos < bb.size(); pos += 480) {
        rx.process(&bb[pos], std::min<size_t>(480, bb.size() - pos));

        Event ev;
        while (rx.popEvent(ev)) {
            if (ev.type == Event::LOST)
                lost++;
            else
                bursts++;
        }
    }

    REQUIRE(lost == 1);
    REQUIRE(rx.isLocked() == false);
    REQUIRE(bursts > 0);
}

TEST_CASE("DMR modem joins a transmission late", "[dmr][modem]")
{
    Transmission tx = makeTransmission(2, false, 7);

    // Start receiving 300 ms after the start of the voice: past the header
    size_t start =
        (200 + (PREAMBLE_BYTES * 4) + ((HEADERS + 5) * FRAME_SYMBOLS))
        * FskShaper::SAMPLES_PER_SYMBOL;
    std::vector<int16_t> late(tx.baseband.begin() + start, tx.baseband.end());
    auto bb = channel(late, 0, 1.0, 0.0, 0.0, 7);

    Receiver rx;
    VoiceDecoder dec;
    dec.setColorCode(2);

    bool started = false;
    bool gotLc = false;
    size_t voice = 0;

    for (size_t pos = 0; pos < bb.size(); pos += 480) {
        rx.process(&bb[pos], std::min<size_t>(480, bb.size() - pos));

        Event ev;
        while (rx.popEvent(ev)) {
            if (ev.type != Event::BURST)
                continue;

            VoiceEvent vev;
            dec.process(ev.burst, ev.sync, vev);
            started |= vev.callStart;
            voice += vev.voice ? 1 : 0;

            if (vev.lcValid) {
                gotLc = true;
                REQUIRE(vev.lc.src == tx.lc.src);
                REQUIRE(vev.lc.dst == tx.lc.dst);
            }
        }
    }

    REQUIRE(started);
    REQUIRE(gotLc);
    REQUIRE(voice > 0);
}
