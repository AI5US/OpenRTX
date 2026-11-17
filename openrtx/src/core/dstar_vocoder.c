/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <mbefx/ambe.h>
#include <stdlib.h>
#include "core/dstar_vocoder.h"

struct dstarEncoder {
    struct ambe_encoder enc;
};

struct dstarDecoder {
    struct ambe_decoder dec;
};

dstarEncoder_t *dstarEncoder_create()
{
    dstarEncoder_t *enc = malloc(sizeof(dstarEncoder_t));
    if (enc == NULL)
        return NULL;

    dstarEncoder_reset(enc);

    return enc;
}

void dstarEncoder_destroy(dstarEncoder_t *enc)
{
    free(enc);
}

void dstarEncoder_reset(dstarEncoder_t *enc)
{
    ambe_encoderInit(&enc->enc);
}

int dstarEncoder_encode(dstarEncoder_t *enc, const int16_t *pcm, uint8_t *frame)
{
    ambe_encode(&enc->enc, pcm, frame);

    return 0;
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
    ambe_decoderInit(&dec->dec);
}

int dstarDecoder_decode(dstarDecoder_t *dec, const uint8_t *frame, int16_t *pcm)
{
    return ambe_decode(&dec->dec, frame, pcm);
}
