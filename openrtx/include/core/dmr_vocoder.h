/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_VOCODER_H
#define DMR_VOCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Software AMBE+2 3600x2450 vocoder for DMR digital voice, based on
 * mbelib-neo. Available only when DMR support is enabled at compile time
 * (CONFIG_DMR).
 *
 * Each 20 ms voice frame is made of 160 samples of 8 kHz, 16-bit audio and
 * is coded in the 9 bytes (72 bits, FEC included) carried, three at a time,
 * by a DMR voice burst. Bytes are in air order, first bit in the most
 * significant bit of the first byte, the DMR bit order.
 *
 * Encoder and decoder keep separate state, since a transmission and a
 * reception never happen at the same time. Both are meant to be used from a
 * single thread.
 */

/**
 * Number of audio samples in a voice frame.
 */
#define DMR_VOCODER_FRAME_SAMPLES 160

/**
 * Size, in bytes, of an encoded voice frame.
 */
#define DMR_VOCODER_FRAME_BYTES 9

/// Bytes of the 49 parameter bits of a frame, first bit in the most
/// significant position: the form YSF sends, without the DMR frame FEC
#define DMR_VOCODER_PARAMS_BYTES 7

typedef struct dmrEncoder dmrEncoder_t;
typedef struct dmrDecoder dmrDecoder_t;

/**
 * \brief Allocate and initialise a new encoder.
 *
 * @return the new encoder or NULL if there was not enough memory.
 */
dmrEncoder_t *dmrEncoder_create();

/**
 * \brief Free an encoder previously allocated with dmrEncoder_create().
 *
 * @param enc: encoder, NULL is accepted.
 */
void dmrEncoder_destroy(dmrEncoder_t *enc);

/**
 * \brief Reset the encoder state, to start encoding a new voice stream.
 *
 * @param enc: encoder.
 */
void dmrEncoder_reset(dmrEncoder_t *enc);

/**
 * \brief Encode a 20 ms voice frame.
 *
 * Encoded speech lags the input audio by about 10 ms: encode a frame of
 * silence at the end of a stream to flush it.
 *
 * @param enc: encoder.
 * @param pcm: DMR_VOCODER_FRAME_SAMPLES input samples.
 * @param frame: output buffer of DMR_VOCODER_FRAME_BYTES bytes.
 * @return 0 on success, a negative error code otherwise.
 */
int dmrEncoder_encode(dmrEncoder_t *enc, const int16_t *pcm, uint8_t *frame);

/**
 * \brief Encode a 20 ms voice frame into its 49 parameter bits, without the
 * frame FEC (Golay codes and interleave) of DMR.
 *
 * @param enc: encoder.
 * @param pcm: DMR_VOCODER_FRAME_SAMPLES input samples.
 * @param params: output buffer of DMR_VOCODER_PARAMS_BYTES bytes.
 * @return 0 on success, a negative error code otherwise.
 */
int dmrEncoder_encodeParams(dmrEncoder_t *enc, const int16_t *pcm,
                            uint8_t *params);

/**
 * \brief Allocate and initialise a new decoder.
 *
 * @return the new decoder or NULL if there was not enough memory.
 */
dmrDecoder_t *dmrDecoder_create();

/**
 * \brief Free a decoder previously allocated with dmrDecoder_create().
 *
 * @param dec: decoder, NULL is accepted.
 */
void dmrDecoder_destroy(dmrDecoder_t *dec);

/**
 * \brief Reset the decoder state, to start decoding a new voice stream.
 *
 * @param dec: decoder.
 */
void dmrDecoder_reset(dmrDecoder_t *dec);

/**
 * \brief Decode a 20 ms voice frame.
 *
 * Bit errors are corrected by the frame FEC as far as possible; frames that
 * cannot be recovered are concealed by repeating, then muting, the previous
 * frame.
 *
 * @param dec: decoder.
 * @param frame: DMR_VOCODER_FRAME_BYTES bytes of encoded voice.
 * @param pcm: output buffer of DMR_VOCODER_FRAME_SAMPLES samples.
 * @return number of bit errors detected in the frame, a negative error code
 * in case of failure.
 */
int dmrDecoder_decode(dmrDecoder_t *dec, const uint8_t *frame, int16_t *pcm);

/**
 * \brief Decode a 20 ms voice frame from its 49 parameter bits, as YSF sends
 * them: the transport has corrected what it could.
 *
 * @param dec: decoder.
 * @param params: DMR_VOCODER_PARAMS_BYTES bytes.
 * @param pcm: output buffer of DMR_VOCODER_FRAME_SAMPLES samples.
 * @return 0 on success, a negative error code in case of failure.
 */
int dmrDecoder_decodeParams(dmrDecoder_t *dec, const uint8_t *params,
                            int16_t *pcm);

/**
 * \brief Convert a voice frame from the DMR air layout to the 4 x 24 bit
 * plane layout of mbelib-neo (C0, C1, C2 and C3 code words), undoing the DMR
 * interleave. Exposed for testing.
 *
 * @param frame: DMR_VOCODER_FRAME_BYTES bytes, air layout.
 * @param planes: output, 4 x 24 bits, one bit per byte.
 */
void dmrVocoder_deinterleave(const uint8_t *frame, char planes[4][24]);

/**
 * \brief Convert a voice frame from the 4 x 24 bit plane layout of
 * mbelib-neo to the DMR air layout, inverse of dmrVocoder_deinterleave().
 * Exposed for testing.
 *
 * @param planes: 4 x 24 bits, one bit per byte.
 * @param frame: output, DMR_VOCODER_FRAME_BYTES bytes, air layout.
 */
void dmrVocoder_interleave(const char planes[4][24], uint8_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* DMR_VOCODER_H */
