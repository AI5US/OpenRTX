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

#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Cach.hpp"
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Csbk.hpp"
#include "protocols/DMR/Demodulator.hpp"
#include "protocols/DMR/Modulator.hpp"
#include "protocols/DMR/Voice.hpp"

using namespace DMR;

static constexpr size_t SUPERFRAMES = 3;

/*
 * CACH as MMDVM builds it (DMRTX.cpp createCACH), an independent restatement
 * of the TACT bit positions and parity.
 */
static void mmdvmCach(bool at, bool tc, bool ls1, bool ls0, uint8_t *cach)
{
    bool h0 = at ^ tc ^ ls1;
    bool h1 = tc ^ ls1 ^ ls0;
    bool h2 = at ^ tc ^ ls0;

    memset(cach, 0x00, 3);
    cach[0] |= at ? 0x80 : 0x00;
    cach[0] |= tc ? 0x08 : 0x00;
    cach[1] |= ls1 ? 0x80 : 0x00;
    cach[1] |= ls0 ? 0x08 : 0x00;
    cach[1] |= h0 ? 0x02 : 0x00;
    cach[2] |= h1 ? 0x20 : 0x00;
    cach[2] |= h2 ? 0x02 : 0x00;
}

static void flipBit(uint8_t *data, size_t pos)
{
    data[pos / 8] ^= static_cast<uint8_t>(0x80 >> (pos % 8));
}

TEST_CASE("DMR CACH matches MMDVM and corrects single errors",
          "[dmr][repeater]")
{
    static constexpr uint8_t TACT_POS[7] = { 0, 4, 8, 12, 14, 18, 22 };

    for (uint8_t v = 0; v < 16; v++) {
        Tact tact = {};
        tact.at = (v & 0x08) != 0;
        tact.slot = (v & 0x04) ? 1 : 0;
        tact.lcss = v & 0x03;

        uint8_t cach[3];
        encodeCach(tact, cach);

        uint8_t ref[3];
        mmdvmCach(tact.at, tact.slot != 0, (tact.lcss & 0x02) != 0,
                  (tact.lcss & 0x01) != 0, ref);
        REQUIRE(memcmp(cach, ref, 3) == 0);

        // Any single error on a TACT bit is corrected
        for (size_t i = 0; i < 7; i++) {
            uint8_t bad[3];
            memcpy(bad, cach, 3);
            flipBit(bad, TACT_POS[i]);

            Tact out = {};
            decodeCach(bad, out);
            REQUIRE(out.at == tact.at);
            REQUIRE(out.slot == tact.slot);
            REQUIRE(out.lcss == tact.lcss);
        }
    }
}

TEST_CASE("DMR BS activation CSBK round trip", "[dmr][repeater]")
{
    uint8_t burst[BURST_BYTES];
    encodeBsDwnAct(ALL_ID, 2630423, 3, burst);

    REQUIRE(getSync(burst) == MS_DATA_SYNC);

    uint8_t cc, dt;
    REQUIRE(decodeSlotType(burst, cc, dt));
    REQUIRE(cc == 3);
    REQUIRE(dt == DT_CSBK);

    uint8_t csbk[CSBK_BYTES];
    REQUIRE(decodeCsbk(burst, csbk));
    REQUIRE((csbk[0] & 0x3F) == CSBKO_BS_DWN_ACT);
    REQUIRE((csbk[0] & 0x80) != 0);
    REQUIRE(csbk[4] == 0xFF);
    REQUIRE(csbk[5] == 0xFF);
    REQUIRE(csbk[6] == 0xFF);
    uint32_t src = (static_cast<uint32_t>(csbk[7]) << 16)
                 | (static_cast<uint32_t>(csbk[8]) << 8) | csbk[9];
    REQUIRE(src == 2630423);

    // A CSBK corrupted beyond what BPTC corrects fails its CRC
    for (size_t i = 0; i < 12; i++)
        burst[i] ^= 0x5A;
    REQUIRE(decodeCsbk(burst, csbk) == false);
}

/*
 * Repeater downlink: a TDMA frame is CACH + TS1 burst + CACH + TS2 burst,
 * with base station syncs. Each timeslot carries its own call.
 */
struct Downlink {
    Lc lc[2];
    std::vector<std::array<uint8_t, VOICE_FRAMES * VOICE_FRAME_BYTES> >
        voice[2];
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

static void shapeCarrier(FskShaper &shaper, size_t symbols,
                         std::vector<int16_t> &out)
{
    int16_t samples[FskShaper::SAMPLES_PER_SYMBOL];

    for (size_t i = 0; i < symbols; i++) {
        shaper.flush(samples);
        out.insert(out.end(), samples, samples + FskShaper::SAMPLES_PER_SYMBOL);
    }
}

/// Burst number n of a timeslot: header, voice superframes, terminator
static void slotBurst(VoiceEncoder &enc, Downlink &dl, size_t slot, size_t n,
                      uint32_t &seed, uint8_t *burst)
{
    size_t voiceBursts = SUPERFRAMES * SUPERFRAME_BURSTS;

    if (n == 0) {
        enc.header(burst);
    } else if (n <= voiceBursts) {
        std::array<uint8_t, VOICE_FRAMES * VOICE_FRAME_BYTES> flat;
        for (auto &b : flat) {
            seed = (seed * 1664525u) + 1013904223u;
            b = static_cast<uint8_t>(seed >> 24);
        }

        uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
        memcpy(frames, flat.data(), flat.size());
        dl.voice[slot].push_back(flat);
        enc.voice(frames, burst);
    } else {
        enc.terminator(burst);
    }
}

static Downlink makeDownlink(uint8_t cc, bool corruptCach)
{
    Downlink dl;
    for (size_t s = 0; s < 2; s++) {
        dl.lc[s] = {};
        dl.lc[s].flco = FLCO_GROUP;
        dl.lc[s].src = 1001 + (1001 * s);
        dl.lc[s].dst = 1 + s;
    }

    VoiceEncoder enc[2];
    enc[0].start(dl.lc[0], cc, true);
    enc[1].start(dl.lc[1], cc, true);

    FskShaper shaper;
    shapeCarrier(shaper, 300, dl.baseband);

    uint32_t seed[2] = { 11, 22 };
    size_t bursts = (SUPERFRAMES * SUPERFRAME_BURSTS) + 2;

    for (size_t n = 0; n < bursts; n++) {
        for (size_t s = 0; s < 2; s++) {
            Tact tact = {};
            tact.slot = static_cast<uint8_t>(s);

            uint8_t cach[3];
            encodeCach(tact, cach);

            // Two errors in a CACH: undetected, wrong slot decoded
            if (corruptCach && (n == 5) && (s == 0)) {
                flipBit(cach, 4);
                flipBit(cach, 0);
            }

            uint8_t burst[BURST_BYTES];
            slotBurst(enc[s], dl, s, n, seed[s], burst);
            shapeBytes(shaper, cach, sizeof(cach), dl.baseband);
            shapeBytes(shaper, burst, sizeof(burst), dl.baseband);
        }
    }

    shapeCarrier(shaper, 6000, dl.baseband);
    return dl;
}

struct SlotResult {
    size_t calls = 0;
    size_t lcCount = 0;
    size_t voiceErrors = 0;
    size_t voiceBursts = 0;
    size_t ends = 0;
    Lc lc = {};
};

static void receiveDownlink(const Downlink &dl, uint8_t cc,
                            SlotResult res[2], std::vector<uint32_t> &times)
{
    Receiver rx;
    rx.setRepeater(true);

    VoiceDecoder dec[2];
    dec[0].setColorCode(cc);
    dec[1].setColorCode(cc);
    size_t voiceIndex[2] = { 0, 0 };

    std::vector<int16_t> bb;
    for (size_t i = 0; i < dl.baseband.size(); i += 2)
        bb.push_back(dl.baseband[i]);

    for (size_t pos = 0; pos < bb.size(); pos += 480) {
        rx.process(&bb[pos], std::min<size_t>(480, bb.size() - pos));

        Event ev;
        while (rx.popEvent(ev)) {
            if (ev.type != Event::BURST)
                continue;

            times.push_back(ev.sampleIndex);
            REQUIRE(ev.slot >= 0);

            size_t s = static_cast<size_t>(ev.slot);
            VoiceEvent vev;
            dec[s].process(ev.burst, ev.sync, vev);

            res[s].calls += vev.callStart ? 1 : 0;
            res[s].ends += vev.callEnd ? 1 : 0;
            if (vev.lcValid) {
                res[s].lcCount++;
                res[s].lc = vev.lc;
            }

            if (vev.voice) {
                if ((voiceIndex[s] >= dl.voice[s].size())
                    || (memcmp(vev.frames, dl.voice[s][voiceIndex[s]].data(),
                               dl.voice[s][voiceIndex[s]].size())
                        != 0))
                    res[s].voiceErrors++;

                voiceIndex[s]++;
                res[s].voiceBursts++;
            }
        }
    }
}

TEST_CASE("DMR receiver follows both timeslots of a repeater",
          "[dmr][repeater]")
{
    Downlink dl = makeDownlink(1, false);

    SlotResult res[2];
    std::vector<uint32_t> times;
    receiveDownlink(dl, 1, res, times);

    for (size_t s = 0; s < 2; s++) {
        INFO("slot " << s);
        REQUIRE(res[s].calls == 1);
        REQUIRE(res[s].lcCount == 1);
        REQUIRE(res[s].lc.src == dl.lc[s].src);
        REQUIRE(res[s].lc.dst == dl.lc[s].dst);
        REQUIRE(res[s].voiceBursts == dl.voice[s].size());
        REQUIRE(res[s].voiceErrors == 0);
        REQUIRE(res[s].ends == 1);
    }

    // A burst every 30 ms
    REQUIRE(times.size() > 10);
    for (size_t i = 1; i < 10; i++)
        REQUIRE(times[i] - times[i - 1] == Receiver::SLOT_SAMPLES);
}

TEST_CASE("DMR receiver keeps the timeslots through a bad CACH",
          "[dmr][repeater]")
{
    Downlink dl = makeDownlink(2, true);

    SlotResult res[2];
    std::vector<uint32_t> times;
    receiveDownlink(dl, 2, res, times);

    for (size_t s = 0; s < 2; s++) {
        INFO("slot " << s);
        REQUIRE(res[s].calls == 1);
        REQUIRE(res[s].voiceBursts == dl.voice[s].size());
        REQUIRE(res[s].voiceErrors == 0);
    }
}

TEST_CASE("DMR receiver modes ignore the other sync family",
          "[dmr][repeater]")
{
    // A repeater downlink is not received in direct mode
    Downlink dl = makeDownlink(1, false);

    std::vector<int16_t> bb;
    for (size_t i = 0; i < dl.baseband.size(); i += 2)
        bb.push_back(dl.baseband[i]);

    Receiver rx;
    size_t bursts = 0;
    for (size_t pos = 0; pos < bb.size(); pos += 480) {
        rx.process(&bb[pos], std::min<size_t>(480, bb.size() - pos));

        Event ev;
        while (rx.popEvent(ev))
            bursts += (ev.type == Event::BURST) ? 1 : 0;
    }

    REQUIRE(bursts == 0);
}
