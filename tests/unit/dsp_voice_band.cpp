/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "core/dsp.h"

static constexpr float SAMPLE_RATE = 8000.0f;
static constexpr size_t SETTLE = 800;
static constexpr size_t MEASURE = 8000;

/// Gain of the filter for a sine, in dB
static float gainDb(float freq, float amplitude)
{
    struct voiceBand vb;
    dsp_resetState(vb);

    double in = 0.0;
    double out = 0.0;
    for (size_t i = 0; i < SETTLE + MEASURE; i++) {
        float x = amplitude * std::sin(2.0f * M_PI * freq * i / SAMPLE_RATE);
        int16_t y = dsp_voiceBandFilter(&vb, static_cast<int16_t>(x));

        if (i >= SETTLE) {
            in += x * x;
            out += static_cast<double>(y) * y;
        }
    }

    return 10.0f * std::log10(out / in);
}

TEST_CASE("Voice band filter passes speech frequencies", "[dsp][voiceband]")
{
    REQUIRE(std::fabs(gainDb(500.0f, 10000.0f)) < 1.0f);
    REQUIRE(std::fabs(gainDb(1000.0f, 10000.0f)) < 0.5f);
    REQUIRE(std::fabs(gainDb(2000.0f, 10000.0f)) < 0.5f);
}

TEST_CASE("Voice band filter cuts below 300 Hz and above 3 kHz",
          "[dsp][voiceband]")
{
    REQUIRE(gainDb(100.0f, 10000.0f) < -15.0f);
    REQUIRE(gainDb(3400.0f, 10000.0f) < -15.0f);
    REQUIRE(gainDb(3800.0f, 10000.0f) < -40.0f);
}

TEST_CASE("Voice band filter saturates instead of wrapping", "[dsp][voiceband]")
{
    struct voiceBand vb;
    dsp_resetState(vb);

    // Full scale square wave at 1 kHz: overshoots on each edge
    int16_t prev = 0;
    for (size_t i = 0; i < 4000; i++) {
        int16_t x = ((i / 4) % 2) ? INT16_MAX : INT16_MIN;
        int16_t y = dsp_voiceBandFilter(&vb, x);

        // A wrapped sample would jump across the whole range
        if (i > 100)
            REQUIRE(std::abs(y - prev) < 60000);
        prev = y;
    }
}
