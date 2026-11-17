/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <mbelib-neo/mbelib.h>
#include <stdlib.h>
#include "core/dstar_vocoder.h"

struct dstarEncoder {
    mbe_ambe2400_encoder *ctx;
    mbe_parms cur;  // Quantised parameters of the current frame
    mbe_parms prev; // Quantised parameters of the previous frame
};

struct dstarDecoder {
    mbe_parms cur;
    mbe_parms prev;
    mbe_parms prevEnhanced;
};

dstarEncoder_t *dstarEncoder_create()
{
    dstarEncoder_t *enc = malloc(sizeof(dstarEncoder_t));
    if (enc == NULL)
        return NULL;

    enc->ctx = mbe_ambe2400EncoderAlloc();
    if (enc->ctx == NULL) {
        free(enc);
        return NULL;
    }

    dstarEncoder_reset(enc);

    return enc;
}

void dstarEncoder_destroy(dstarEncoder_t *enc)
{
    if (enc == NULL)
        return;

    mbe_ambe2400EncoderFree(enc->ctx);
    free(enc);
}

void dstarEncoder_reset(dstarEncoder_t *enc)
{
    mbe_ambe2400EncoderReset(enc->ctx);

    // The encoder has no use for the enhanced parameter set, let the
    // initialisation overwrite the current one instead.
    mbe_initMbeParms(&enc->cur, &enc->prev, &enc->cur);
}

int dstarEncoder_encode(dstarEncoder_t *enc, const int16_t *pcm,
                        uint8_t *frame)
{
    char params[49];
    char bits[4][24];

    int ret = mbe_encodeAmbe2400ParmsShort(enc->ctx, (const short *)pcm,
                                           params, &enc->cur, &enc->prev);
    if (ret < 0)
        return ret;

    mbe_moveMbeParms(&enc->cur, &enc->prev);

    ret = mbe_encodeAmbe3600x2400Frame(params, bits);
    if (ret < 0)
        return ret;

    return mbe_encodeDStarDVData((const char(*)[24])bits, frame);
}

dstarDecoder_t *dstarDecoder_create()
{
    dstarDecoder_t *dec = malloc(sizeof(dstarDecoder_t));
    if (dec == NULL)
        return NULL;

    dstarDecoder_reset(dec);

    return dec;
}

void dstarDecoder_destroy(dstarDecoder_t *dec)
{
    free(dec);
}

void dstarDecoder_reset(dstarDecoder_t *dec)
{
    mbe_initMbeParms(&dec->cur, &dec->prev, &dec->prevEnhanced);
}

int dstarDecoder_decode(dstarDecoder_t *dec, const uint8_t *frame,
                        int16_t *pcm)
{
    char params[49];
    char bits[4][24];

    int ret = mbe_decodeDStarDVData(frame, bits);
    if (ret < 0)
        return ret;

    // Error correction, parameter decoding, state update and synthesis
    return mbe_processAmbe3600x2400Frame((short *)pcm, NULL,
                                         (const char(*)[24])bits, params,
                                         &dec->cur, &dec->prev,
                                         &dec->prevEnhanced);
}
