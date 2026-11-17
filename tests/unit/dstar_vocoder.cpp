/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <vector>

extern "C" {
#include "core/dstar_vocoder.h"
}

static const size_t SAMPLES = DSTAR_VOCODER_FRAME_SAMPLES;
static const size_t BYTES = DSTAR_VOCODER_FRAME_BYTES;

// AMBE null frame, sent by D-STAR equipment (e.g. MMDVM, ircDDBGateway) as
// silence
static const uint8_t nullFrame[BYTES] = { 0x9E, 0x8D, 0x32, 0x88, 0x26,
                                          0x1A, 0x3F, 0x61, 0xE8 };

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

static std::vector<uint8_t> encodeAll(dstarEncoder_t *enc,
                                      const std::vector<int16_t> &pcm)
{
    size_t frames = pcm.size() / SAMPLES;
    std::vector<uint8_t> out(frames * BYTES);

    for (size_t f = 0; f < frames; f++)
        REQUIRE(dstarEncoder_encode(enc, &pcm[f * SAMPLES], &out[f * BYTES])
                == 0);

    return out;
}

TEST_CASE("D-STAR vocoder decodes the null frame without errors",
          "[dstar][vocoder]")
{
    dstarDecoder_t *dec = dstarDecoder_create();
    REQUIRE(dec != nullptr);

    int16_t pcm[SAMPLES];
    for (int i = 0; i < 10; i++)
        REQUIRE(dstarDecoder_decode(dec, nullFrame, pcm) == 0);

    dstarDecoder_destroy(dec);
}

TEST_CASE("D-STAR vocoder round trip keeps the speech level",
          "[dstar][vocoder]")
{
    const size_t frames = 50;
    std::vector<int16_t> voice = makeVoice(frames);

    dstarEncoder_t *enc = dstarEncoder_create();
    dstarDecoder_t *dec = dstarDecoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(dec != nullptr);

    std::vector<uint8_t> coded = encodeAll(enc, voice);
    std::vector<int16_t> decoded(frames * SAMPLES);

    // Error-free channel: no bit errors to correct
    for (size_t f = 0; f < frames; f++) {
        int ret = dstarDecoder_decode(dec, &coded[f * BYTES],
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

    // Speech decodes louder than the null frame
    int16_t silence[SAMPLES];
    dstarDecoder_reset(dec);
    double nullLevel = 0.0;
    for (int i = 0; i < 10; i++) {
        REQUIRE(dstarDecoder_decode(dec, nullFrame, silence) == 0);
        nullLevel = rms(silence, SAMPLES);
    }

    INFO("null frame RMS " << nullLevel);
    REQUIRE(nullLevel < outLevel / 4.0);

    dstarEncoder_destroy(enc);
    dstarDecoder_destroy(dec);
}

TEST_CASE("D-STAR vocoder encoding is repeatable after reset",
          "[dstar][vocoder]")
{
    std::vector<int16_t> voice = makeVoice(20);

    dstarEncoder_t *enc = dstarEncoder_create();
    dstarEncoder_t *other = dstarEncoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(other != nullptr);

    std::vector<uint8_t> first = encodeAll(enc, voice);
    dstarEncoder_reset(enc);
    std::vector<uint8_t> again = encodeAll(enc, voice);
    std::vector<uint8_t> fresh = encodeAll(other, voice);

    REQUIRE(first == again);
    REQUIRE(first == fresh);

    dstarEncoder_destroy(enc);
    dstarEncoder_destroy(other);
}

TEST_CASE("D-STAR vocoder reports bit errors", "[dstar][vocoder]")
{
    std::vector<int16_t> voice = makeVoice(20);

    dstarEncoder_t *enc = dstarEncoder_create();
    dstarDecoder_t *dec = dstarDecoder_create();
    REQUIRE(enc != nullptr);
    REQUIRE(dec != nullptr);

    std::vector<uint8_t> coded = encodeAll(enc, voice);
    int16_t pcm[SAMPLES];

    for (size_t f = 0; f < 10; f++)
        REQUIRE(dstarDecoder_decode(dec, &coded[f * BYTES], pcm) == 0);

    // A single error in each of the two Golay protected codewords is
    // corrected and counted: the first air bit is in the first codeword, the
    // fifth air bit in the second one.
    uint8_t frame[BYTES];
    memcpy(frame, &coded[10 * BYTES], BYTES);
    frame[0] ^= 0x01;
    REQUIRE(dstarDecoder_decode(dec, frame, pcm) == 1);

    memcpy(frame, &coded[11 * BYTES], BYTES);
    frame[0] ^= 0x10;
    REQUIRE(dstarDecoder_decode(dec, frame, pcm) == 1);

    // The next, undamaged, frame decodes cleanly again
    REQUIRE(dstarDecoder_decode(dec, &coded[12 * BYTES], pcm) == 0);

    dstarEncoder_destroy(enc);
    dstarDecoder_destroy(dec);
}
