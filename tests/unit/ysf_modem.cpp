/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protocols/DMR/Modulator.hpp"
#include "protocols/YSF/Demodulator.hpp"
#include "protocols/YSF/Voice.hpp"

using namespace YSF;

/// As OpMode_YSF::DEVIATION
static constexpr uint16_t DEVIATION = 700;
static constexpr size_t VOICE_FRAMES_SENT = 20;

static void shapeBytes(DMR::FskShaper &shaper, const uint8_t *data, size_t len,
                       std::vector<int16_t> &out)
{
    int16_t samples[DMR::FskShaper::SAMPLES_PER_SYMBOL];

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        for (size_t s = 0; s < 4; s++) {
            shaper.process((c >> 6) & 0x03, samples);
            out.insert(out.end(), samples,
                       samples + DMR::FskShaper::SAMPLES_PER_SYMBOL);
            c = static_cast<uint8_t>(c << 2);
        }
    }
}

static void shapeCarrier(DMR::FskShaper &shaper, size_t symbols,
                         std::vector<int16_t> &out)
{
    int16_t samples[DMR::FskShaper::SAMPLES_PER_SYMBOL];

    for (size_t i = 0; i < symbols; i++) {
        shaper.flush(samples);
        out.insert(out.end(), samples,
                   samples + DMR::FskShaper::SAMPLES_PER_SYMBOL);
    }
}

TEST_CASE("YSF transmission through the modem", "[ysf][modem]")
{
    DMR::FskShaper shaper;
    shaper.setDeviation(DEVIATION);

    std::vector<int16_t> baseband;
    shapeCarrier(shaper, 300, baseband);

    VoiceEncoder enc;
    enc.start("AI5US", "ALL", 0);

    uint8_t frame[FRAME_BYTES];
    enc.header(frame);
    shapeBytes(shaper, frame, FRAME_BYTES, baseband);

    std::vector<std::vector<uint8_t>> sent;
    for (size_t n = 0; n < VOICE_FRAMES_SENT; n++) {
        uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES];
        for (size_t i = 0; i < VOICE_FRAMES; i++) {
            for (size_t j = 0; j < VOICE_FRAME_BYTES; j++)
                voice[i][j] = static_cast<uint8_t>((n * 13) + (i * 7) + j);
            voice[i][VOICE_FRAME_BYTES - 1] &= 0x80;
        }

        sent.emplace_back(&voice[0][0], &voice[0][0] + sizeof(voice));
        enc.voice(voice, frame);
        shapeBytes(shaper, frame, FRAME_BYTES, baseband);
    }

    enc.terminator(frame);
    shapeBytes(shaper, frame, FRAME_BYTES, baseband);
    shapeCarrier(shaper, 2000, baseband);

    // The receiver takes 24 kHz
    std::vector<int16_t> rx;
    for (size_t i = 0; i < baseband.size(); i += 2)
        rx.push_back(baseband[i]);

    Receiver receiver;
    VoiceDecoder dec;
    size_t starts = 0, ends = 0, voiceFrames = 0, voiceErrors = 0;
    size_t mismatches = 0;
    bool source = false;

    for (size_t pos = 0; pos < rx.size(); pos += 480) {
        receiver.process(&rx[pos], std::min<size_t>(480, rx.size() - pos));

        Event ev;
        while (receiver.popEvent(ev)) {
            if (ev.type != Event::FRAME)
                continue;

            VoiceEvent vev;
            dec.process(ev.frame, vev);
            starts += vev.callStart ? 1 : 0;
            ends += vev.callEnd ? 1 : 0;
            if (vev.sourceValid && (strcmp(vev.source, "AI5US") == 0))
                source = true;

            if (vev.voice) {
                if ((voiceFrames >= sent.size())
                    || (memcmp(vev.frames, sent[voiceFrames].data(),
                               sent[voiceFrames].size())
                        != 0))
                    mismatches++;

                voiceErrors += vev.voiceErrors;
                voiceFrames++;
            }
        }
    }

    REQUIRE(starts == 1);
    REQUIRE(source);
    REQUIRE(voiceFrames == VOICE_FRAMES_SENT);
    REQUIRE(mismatches == 0);
    REQUIRE(voiceErrors == 0);
    REQUIRE(ends == 1);
}
