/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

extern "C" {
#include "core/dmr_vocoder.h"
}

static const size_t SAMPLES = DMR_VOCODER_FRAME_SAMPLES;
static const size_t BYTES = DMR_VOCODER_FRAME_BYTES;

// AMBE+2 silence frame, sent by DMR equipment (e.g. MMDVMHost,
// DMR_SILENCE_DATA) to fill voice bursts
static const uint8_t silenceFrame[BYTES] = { 0xB9, 0xE8, 0x81, 0x52, 0x61,
                                             0x73, 0x00, 0x2A, 0x6B };

/*
 * Vowel-like test signal: 125 Hz fundamental with decaying harmonics up to
 * 3 kHz, 8 kHz sampling rate.
 */
static std::vector<int16_t> makeVoice(size_t frames)
{
    std::vector<int16_t> out(frames * SAMPLES);

    for (size_t i = 0; i < out.size(); i++) {
        double t = static_cast<double>(i) / 8000.0;
        double v = 0.0;

        for (int h = 1; h <= 24; h++)
            v += std::sin(2.0 * M_PI * 125.0 * h * t) / h;

        out[i] = static_cast<int16_t>(v * 4000.0);
    }

    return out;
}

static double rms(const int16_t *pcm, size_t len)
{
    double acc = 0.0;

    for (size_t i = 0; i < len; i++)
        acc += static_cast<double>(pcm[i]) * pcm[i];

    return std::sqrt(acc / len);
}

static std::vector<uint8_t> encodeAll(dmrEncoder_t *enc,
                                      const std::vector<int16_t> &pcm)
{
    size_t frames = pcm.size() / SAMPLES;
    std::vector<uint8_t> out(frames * BYTES);

    for (size_t f = 0; f < frames; f++)
        REQUIRE(dmrEncoder_encode(enc, &pcm[f * SAMPLES], &out[f * BYTES])
                == 0);

    return out;
}

TEST_CASE("DMR vocoder interleave is a bijection", "[dmr][vocoder]")
{
    // Each air bit lands on a distinct plane bit, and the 72 plane bits used
    // are the C0 (24), C1 (23), C2 (11) and C3 (14) code word bits.
    std::set<std::pair<int, int>> seen;

    for (size_t bit = 0; bit < 72; bit++) {
        uint8_t frame[BYTES] = { 0 };
        frame[bit / 8] = static_cast<uint8_t>(0x80 >> (bit % 8));

        char planes[4][24];
        dmrVocoder_deinterleave(frame, planes);

        int count = 0;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 24; c++) {
                if (planes[r][c] != 0) {
                    seen.insert(std::make_pair(r, c));
                    count++;
                }
            }
        }

        REQUIRE(count == 1);

        uint8_t back[BYTES];
        dmrVocoder_interleave(planes, back);
        REQUIRE(memcmp(back, frame, BYTES) == 0);
    }

    REQUIRE(seen.size() == 72);

    const int widths[4] = { 24, 23, 11, 14 };
    for (const auto &p : seen)
        REQUIRE(p.second < widths[p.first]);
}

TEST_CASE("DMR vocoder decodes the silence frame without errors",
          "[dmr][vocoder]")
{
    dmrDecoder_t *dec = dmrDecoder_create();
    REQUIRE(dec != nullptr);

    int16_t pcm[SAMPLES];
    for (int i = 0; i < 10; i++)
        REQUIRE(dmrDecoder_decode(dec, silenceFrame, pcm) == 0);

    dmrDecoder_destroy(dec);
}

TEST_CASE("DMR vocoder round trip keeps the speech level", "[dmr][vocoder]")
{
    const size_t frames = 50;
    std::vector<int16_t> voice = makeVoice(frames);

    dmrEncoder_t *enc = dmrEncoder_create();
    dmrDecoder_t *dec = dmrDecoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(dec != nullptr);

    std::vector<uint8_t> coded = encodeAll(enc, voice);
    std::vector<int16_t> decoded(frames * SAMPLES);

    // Error-free channel: no bit errors to correct
    for (size_t f = 0; f < frames; f++) {
        int ret = dmrDecoder_decode(dec, &coded[f * BYTES],
                                    &decoded[f * SAMPLES]);
        REQUIRE(ret == 0);
    }

    // Skip the first 10 frames, giving encoder and decoder time to settle
    size_t skip = 10 * SAMPLES;
    double inLevel = rms(&voice[skip], voice.size() - skip);
    double outLevel = rms(&decoded[skip], decoded.size() - skip);

    INFO("input RMS " << inLevel << ", output RMS " << outLevel);
    REQUIRE(outLevel > inLevel / 4.0);
    REQUIRE(outLevel < inLevel * 4.0);

    // Speech decodes louder than the silence frame
    int16_t silence[SAMPLES];
    dmrDecoder_reset(dec);
    double silenceLevel = 0.0;
    for (int i = 0; i < 10; i++) {
        REQUIRE(dmrDecoder_decode(dec, silenceFrame, silence) == 0);
        silenceLevel = rms(silence, SAMPLES);
    }

    INFO("silence frame RMS " << silenceLevel);
    REQUIRE(silenceLevel < outLevel / 4.0);

    dmrEncoder_destroy(enc);
    dmrDecoder_destroy(dec);
}

TEST_CASE("DMR vocoder encoding is repeatable after reset", "[dmr][vocoder]")
{
    std::vector<int16_t> voice = makeVoice(20);

    dmrEncoder_t *enc = dmrEncoder_create();
    dmrEncoder_t *other = dmrEncoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(other != nullptr);

    std::vector<uint8_t> first = encodeAll(enc, voice);
    dmrEncoder_reset(enc);
    std::vector<uint8_t> again = encodeAll(enc, voice);
    std::vector<uint8_t> fresh = encodeAll(other, voice);

    REQUIRE(first == again);
    REQUIRE(first == fresh);

    dmrEncoder_destroy(enc);
    dmrEncoder_destroy(other);
}

TEST_CASE("DMR vocoder reports bit errors", "[dmr][vocoder]")
{
    std::vector<int16_t> voice = makeVoice(20);

    dmrEncoder_t *enc = dmrEncoder_create();
    dmrDecoder_t *dec = dmrDecoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(dec != nullptr);

    std::vector<uint8_t> coded = encodeAll(enc, voice);
    int16_t pcm[SAMPLES];

    for (size_t f = 0; f < 10; f++)
        REQUIRE(dmrDecoder_decode(dec, &coded[f * BYTES], pcm) == 0);

    // A single error in the C0 Golay code word (first air bit) and one in
    // the C1 code word (second air bit) are corrected and counted.
    uint8_t frame[BYTES];
    memcpy(frame, &coded[10 * BYTES], BYTES);
    frame[0] ^= 0x80;
    REQUIRE(dmrDecoder_decode(dec, frame, pcm) == 1);

    memcpy(frame, &coded[11 * BYTES], BYTES);
    frame[0] ^= 0x20;
    REQUIRE(dmrDecoder_decode(dec, frame, pcm) == 1);

    // The next, undamaged, frame decodes cleanly again
    REQUIRE(dmrDecoder_decode(dec, &coded[12 * BYTES], pcm) == 0);

    dmrEncoder_destroy(enc);
    dmrDecoder_destroy(dec);
}
