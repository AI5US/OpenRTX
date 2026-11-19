/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2010 DSD Author
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <mbelib-neo/mbelib.h>
#include <stdlib.h>
#include <string.h>
#include "core/dmr_vocoder.h"

/*
 * DMR interleave of the 72 bits of an AMBE+2 frame, from DSD
 * (include/dmr_const.h, ISC license), as also used by mbelib-neo's DVSI test
 * vector import. The frame is a sequence of 36 dibits: the high bit of dibit
 * i goes to planes[ROW_HIGH[i]][COL_HIGH[i]], its low bit to
 * planes[ROW_LOW[i]][COL_LOW[i]].
 */
static const uint8_t ROW_HIGH[36] = {
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2,
};

static const uint8_t COL_HIGH[36] = {
    23, 10, 22, 9, 21, 8,  20, 7, 19, 6, 18, 5, 17, 4, 16, 3, 15, 2,
    14, 1,  13, 0, 12, 10, 11, 9, 10, 8, 9,  7, 8,  6, 7,  5, 6,  4,
};

static const uint8_t ROW_LOW[36] = {
    0, 2, 0, 2, 0, 2, 0, 2, 0, 3, 0, 3, 1, 3, 1, 3, 1, 3,
    1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3,
};

static const uint8_t COL_LOW[36] = {
    5,  3, 4,  2, 3,  1, 2,  0, 1,  13, 0,  12, 22, 11, 21, 10, 20, 9,
    19, 8, 18, 7, 17, 6, 16, 5, 15, 4,  14, 3,  13, 2,  12, 1,  11, 0,
};

struct dmrEncoder {
    mbe_ambe2450_encoder *ctx;
    mbe_parms cur;  // Quantised parameters of the current frame
    mbe_parms prev; // Quantised parameters of the previous frame
};

struct dmrDecoder {
    mbe_parms cur;
    mbe_parms prev;
    mbe_parms prevEnhanced;
};

static inline uint8_t getBit(const uint8_t *buf, size_t pos)
{
    return (buf[pos / 8] >> (7 - (pos % 8))) & 0x01;
}

void dmrVocoder_deinterleave(const uint8_t *frame, char planes[4][24])
{
    memset(planes, 0, 4 * 24);

    for (size_t i = 0; i < 36; i++) {
        planes[ROW_HIGH[i]][COL_HIGH[i]] = (char)getBit(frame, 2 * i);
        planes[ROW_LOW[i]][COL_LOW[i]] = (char)getBit(frame, (2 * i) + 1);
    }
}

void dmrVocoder_interleave(const char planes[4][24], uint8_t *frame)
{
    memset(frame, 0, DMR_VOCODER_FRAME_BYTES);

    for (size_t i = 0; i < 36; i++) {
        size_t hi = 2 * i;
        size_t lo = hi + 1;

        if (planes[ROW_HIGH[i]][COL_HIGH[i]] != 0)
            frame[hi / 8] |= (uint8_t)(0x80 >> (hi % 8));
        if (planes[ROW_LOW[i]][COL_LOW[i]] != 0)
            frame[lo / 8] |= (uint8_t)(0x80 >> (lo % 8));
    }
}

dmrEncoder_t *dmrEncoder_create()
{
    dmrEncoder_t *enc = malloc(sizeof(dmrEncoder_t));
    if (enc == NULL)
        return NULL;

    enc->ctx = mbe_ambe2450EncoderAlloc();
    if (enc->ctx == NULL) {
        free(enc);
        return NULL;
    }

    dmrEncoder_reset(enc);

    return enc;
}

void dmrEncoder_destroy(dmrEncoder_t *enc)
{
    if (enc == NULL)
        return;

    mbe_ambe2450EncoderFree(enc->ctx);
    free(enc);
}

void dmrEncoder_reset(dmrEncoder_t *enc)
{
    mbe_ambe2450EncoderReset(enc->ctx);

    // The encoder has no use for the enhanced parameter set, let the
    // initialisation overwrite the current one instead.
    mbe_initMbeParms(&enc->cur, &enc->prev, &enc->cur);
}

int dmrEncoder_encode(dmrEncoder_t *enc, const int16_t *pcm, uint8_t *frame)
{
    char params[49];
    char planes[4][24];

    int ret = mbe_encodeAmbe2450ParmsShort(enc->ctx, (const short *)pcm, params,
                                           &enc->cur, &enc->prev);
    if (ret < 0)
        return ret;

    mbe_moveMbeParms(&enc->cur, &enc->prev);

    ret = mbe_encodeAmbe3600x2450Frame(params, planes);
    if (ret < 0)
        return ret;

    dmrVocoder_interleave((const char(*)[24])planes, frame);

    return 0;
}

/// Pack 49 parameter bits, one per char, first bit in the MSB of byte 0
static void packParams(const char *bits, uint8_t *out)
{
    memset(out, 0x00, DMR_VOCODER_PARAMS_BYTES);
    for (size_t i = 0; i < 49; i++) {
        if (bits[i] != 0)
            out[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    }
}

int dmrEncoder_encodeParams(dmrEncoder_t *enc, const int16_t *pcm,
                            uint8_t *params)
{
    char bits[49];

    int ret = mbe_encodeAmbe2450ParmsShort(enc->ctx, (const short *)pcm, bits,
                                           &enc->cur, &enc->prev);
    if (ret < 0)
        return ret;

    mbe_moveMbeParms(&enc->cur, &enc->prev);
    packParams(bits, params);

    return 0;
}

dmrDecoder_t *dmrDecoder_create()
{
    dmrDecoder_t *dec = malloc(sizeof(dmrDecoder_t));
    if (dec == NULL)
        return NULL;

    dmrDecoder_reset(dec);

    return dec;
}

void dmrDecoder_destroy(dmrDecoder_t *dec)
{
    free(dec);
}

void dmrDecoder_reset(dmrDecoder_t *dec)
{
    mbe_initMbeParms(&dec->cur, &dec->prev, &dec->prevEnhanced);
}

int dmrDecoder_decode(dmrDecoder_t *dec, const uint8_t *frame, int16_t *pcm)
{
    char params[49];
    char planes[4][24];

    dmrVocoder_deinterleave(frame, planes);

    // Error correction, parameter decoding, state update and synthesis
    return mbe_processAmbe3600x2450Frame((short *)pcm, NULL,
                                         (const char(*)[24])planes, params,
                                         &dec->cur, &dec->prev,
                                         &dec->prevEnhanced);
}

int dmrDecoder_decodeParams(dmrDecoder_t *dec, const uint8_t *params,
                            int16_t *pcm)
{
    char bits[49];
    for (size_t i = 0; i < 49; i++)
        bits[i] = (char)getBit(params, i);

    int ret = mbe_processAmbe2450Data((short *)pcm, NULL, bits, &dec->cur,
                                      &dec->prev, &dec->prevEnhanced);
    return (ret < 0) ? ret : 0;
}
