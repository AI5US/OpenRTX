/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_VOCODER_H
#define DSTAR_VOCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Software AMBE 3600x2400 vocoder for D-STAR digital voice, based on
 * mbelib-neo. Available only when D-STAR support is enabled at compile time
 * (CONFIG_DSTAR).
 *
 * Each 20 ms voice frame is made of 160 samples of 8 kHz, 16-bit audio and
 * is coded in the 9 bytes (72 bits, FEC included) carried by a D-STAR DV
 * frame, in air bit order.
 *
 * Encoder and decoder keep separate state, since a transmission and a
 * reception never happen at the same time. Both are meant to be used from a
 * single thread.
 */

/**
 * Number of audio samples in a voice frame.
 */
#define DSTAR_VOCODER_FRAME_SAMPLES 160

/**
 * Size, in bytes, of an encoded voice frame.
 */
#define DSTAR_VOCODER_FRAME_BYTES 9

typedef struct dstarEncoder dstarEncoder_t;
typedef struct dstarDecoder dstarDecoder_t;

/**
 * \brief Allocate and initialise a new encoder.
 *
 * @return the new encoder or NULL if there was not enough memory.
 */
dstarEncoder_t *dstarEncoder_create();

/**
 * \brief Free an encoder previously allocated with dstarEncoder_create().
 *
 * @param enc: encoder, NULL is accepted.
 */
void dstarEncoder_destroy(dstarEncoder_t *enc);

/**
 * \brief Reset the encoder state, to start encoding a new voice stream.
 *
 * @param enc: encoder.
 */
void dstarEncoder_reset(dstarEncoder_t *enc);

/**
 * \brief Encode a 20 ms voice frame.
 *
 * Encoded speech lags the input audio by about 10 ms: encode a frame of
 * silence at the end of a stream to flush it.
 *
 * @param enc: encoder.
 * @param pcm: DSTAR_VOCODER_FRAME_SAMPLES input samples.
 * @param frame: output buffer of DSTAR_VOCODER_FRAME_BYTES bytes.
 * @return 0 on success, a negative error code otherwise.
 */
int dstarEncoder_encode(dstarEncoder_t *enc, const int16_t *pcm,
                        uint8_t *frame);

/**
 * \brief Allocate and initialise a new decoder.
 *
 * @return the new decoder or NULL if there was not enough memory.
 */
dstarDecoder_t *dstarDecoder_create();

/**
 * \brief Free a decoder previously allocated with dstarDecoder_create().
 *
 * @param dec: decoder, NULL is accepted.
 */
void dstarDecoder_destroy(dstarDecoder_t *dec);

/**
 * \brief Reset the decoder state, to start decoding a new voice stream.
 *
 * @param dec: decoder.
 */
void dstarDecoder_reset(dstarDecoder_t *dec);

/**
 * \brief Decode a 20 ms voice frame.
 *
 * Bit errors are corrected by the frame FEC as far as possible; frames that
 * cannot be recovered are concealed by repeating, then muting, the previous
 * frame.
 *
 * @param dec: decoder.
 * @param frame: DSTAR_VOCODER_FRAME_BYTES bytes of encoded voice.
 * @param pcm: output buffer of DSTAR_VOCODER_FRAME_SAMPLES samples.
 * @return number of bit errors detected in the frame, a negative error code
 * in case of failure.
 */
int dstarDecoder_decode(dstarDecoder_t *dec, const uint8_t *frame,
                        int16_t *pcm);

#ifdef __cplusplus
}
#endif

#endif /* DSTAR_VOCODER_H */
