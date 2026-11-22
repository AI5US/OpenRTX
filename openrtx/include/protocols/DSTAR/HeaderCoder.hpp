/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2020 Jonathan Naylor G4KLX
 * SPDX-FileCopyrightText: Copyright 2017 Andy Uribe CA6JAU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_HEADERCODER_H
#define DSTAR_HEADERCODER_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "protocols/DSTAR/Constants.hpp"

namespace DStar
{

/**
 * \brief Apply the forward error correction to a D-STAR radio header.
 *
 * The 328 header bits, followed by two zero tail bits, are encoded with the
 * rate 1/2, K = 3 convolutional code (G1 = 1 + D + D^2, sent first, and
 * G0 = 1 + D^2), interleaved and scrambled, as done by MMDVM
 * CDStarTX::txHeader(). The result is the 660 bits sent on air right after
 * the frame synchronisation, in transmission order: bit i is bit (i % 8) of
 * out[i / 8], the four most significant bits of out[82] are zero.
 *
 * @param header: radio header, CRC included, in D-STAR byte order.
 * @param out: encoded header.
 */
void encodeHeader(const uint8_t header[HEADER_SIZE],
                  uint8_t out[HEADER_FEC_SIZE]);

/**
 * \brief Decode a D-STAR radio header received over the air.
 *
 * Descrambles, deinterleaves and decodes the header with a hard decision
 * Viterbi decoder, as done by MMDVM CDStarRX::rxHeader(). Input bit order is
 * the one produced by encodeHeader(); the four most significant bits of
 * in[82] are ignored.
 *
 * @param in: 660 received bits, in transmission order.
 * @param header: decoded radio header, CRC included.
 * @return true if the CRC of the decoded header is valid.
 */
bool decodeHeader(const uint8_t in[HEADER_FEC_SIZE],
                  uint8_t header[HEADER_SIZE]);

} // namespace DStar

#endif // DSTAR_HEADERCODER_H
