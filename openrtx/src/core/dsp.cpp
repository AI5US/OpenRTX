/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/dsp.h"
#include "core/utils.h"

/*
 * Voice band filter, at 8 kHz: second order Butterworth high pass at 300 Hz
 * and fourth order Butterworth low pass at 3 kHz, as second order sections
 * from scipy.signal.butter(2, 300, 'highpass', fs=8000, output='sos') and
 * scipy.signal.butter(4, 3000, 'lowpass', fs=8000, output='sos'). Each row
 * is b0, b1, b2, a1, a2, with a0 = 1.
 */
static constexpr float VOICE_BAND_SOS[][5] = {
    { 0.8464592541f, -1.6929185082f, 0.8464592541f, -1.6692031429f,
      0.7166338735f },
    { 0.3468218078f, 0.6936436157f, 0.3468218078f, 0.8553979328f,
      0.2097153578f },
    { 1.0f, 2.0f, 1.0f, 1.1130298542f, 0.5740619151f },
};

int16_t dsp_dcBlockFilter(struct dcBlock *dcb, int16_t sample)
{
    /*
     * Implementation of a fixed-point DC block filter with noise shaping,
     * ensuring zero DC component at the output.
     * Filter pole set at 0.995
     *
     * Code adapted from https://dspguru.com/dsp/tricks/fixed-point-dc-blocking-filter-with-noise-shaping/
     */
    int32_t in = static_cast<int32_t>(sample) * 32768;

    // Start settled on the first sample, so a DC level gives no step
    if (dcb->initialized == false) {
        dcb->prevIn = in;
        dcb->initialized = true;
    }

    dcb->accum -= dcb->prevIn;
    dcb->prevIn = in;
    dcb->accum += dcb->prevIn;
    dcb->accum -= 164 * dcb->prevOut; // 32768.0 * (1.0 - pole)
    dcb->prevOut = dcb->accum >> 15;

    return static_cast<int16_t>(dcb->prevOut);
}

int16_t dsp_voiceBandFilter(struct voiceBand *vb, int16_t sample)
{
    static_assert(ARRAY_SIZE(VOICE_BAND_SOS) == ARRAY_SIZE(vb->z),
                  "One state per filter section");

    // Direct form II transposed
    float x = static_cast<float>(sample);
    for (size_t i = 0; i < ARRAY_SIZE(VOICE_BAND_SOS); i++) {
        const float *c = VOICE_BAND_SOS[i];
        float *z = vb->z[i];
        float y = (c[0] * x) + z[0];

        z[0] = (c[1] * x) - (c[3] * y) + z[1];
        z[1] = (c[2] * x) - (c[4] * y);
        x = y;
    }

    // The filter overshoots a little on steps near full scale
    if (x > INT16_MAX)
        return INT16_MAX;
    if (x < INT16_MIN)
        return INT16_MIN;

    return static_cast<int16_t>(x);
}

bool dsp_decimator(struct decimatorState *state, int16_t *sample,
                   uint16_t ratio)
{
    state->accumulator += *sample;
    state->count++;
    if (state->count >= ratio) {
        // Integer division truncates toward zero; this is intentional as
        // rounding is unnecessary for audio signal averaging.
        *sample = state->accumulator / ratio;
        state->accumulator = 0;
        state->count = 0;
        return true;
    }

    return false;
}
