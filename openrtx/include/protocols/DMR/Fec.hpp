/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2012 Ian Wraith
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_FEC_H
#define DMR_FEC_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

/*
 * Error correcting codes of DMR Layer 2, ported from MMDVMHost (Hamming.cpp,
 * Golay2087.cpp, QR1676.cpp, RS129.cpp, CRC.cpp, BPTC19696.cpp).
 *
 * The short block codes (Golay and quadratic residue) are decoded by
 * searching the nearest code word, which needs no syndrome table.
 */

namespace DMR
{

/**
 * \brief Encode 8 bits with the Golay (20,8,7) code of the slot type.
 *
 * @param data: data bits.
 * @return the 20 bit code word, data in the 8 most significant bits.
 */
uint32_t golay2087Encode(uint8_t data);

/**
 * \brief Decode a Golay (20,8,7) code word, correcting up to 3 errors.
 *
 * @param code: received 20 bit code word.
 * @param errors: if not null, receives the number of bits corrected.
 * @return the decoded data bits.
 */
uint8_t golay2087Decode(uint32_t code, uint8_t *errors = nullptr);

/**
 * \brief Encode 7 bits with the quadratic residue (16,7,6) code of the EMB.
 *
 * @param data: data bits, 7 least significant bits.
 * @return the 16 bit code word, data in the 7 most significant bits.
 */
uint16_t qr1676Encode(uint8_t data);

/**
 * \brief Decode a quadratic residue (16,7,6) code word, correcting up to 2
 * errors.
 *
 * @param code: received 16 bit code word.
 * @param errors: if not null, receives the number of bits corrected.
 * @return the decoded 7 data bits.
 */
uint8_t qr1676Decode(uint16_t code, uint8_t *errors = nullptr);

/**
 * \brief Compute the Reed-Solomon (12,9) parity of a full link control.
 *
 * @param msg: 9 data bytes.
 * @param parity: the 3 parity bytes, in transmission order.
 */
void rs129Encode(const uint8_t *msg, uint8_t *parity);

/**
 * \brief Check the Reed-Solomon (12,9) parity of a full link control.
 *
 * @param data: 9 data bytes followed by 3 parity bytes.
 * @return true if the parity matches the data.
 */
bool rs129Check(const uint8_t *data);

/**
 * \brief Compute the 5 bit checksum of an embedded link control.
 *
 * @param lc: 9 bytes of link control.
 * @return the checksum, the sum of the bytes modulo 31.
 */
uint8_t crc5(const uint8_t *lc);

/**
 * \brief Encode 96 bits with the BPTC (196,96) code and place the 196 coded
 * bits in the information fields of a data burst (bits 0 to 97 and 166 to
 * 263), leaving the other bits untouched.
 *
 * @param data: 12 bytes of data.
 * @param burst: burst, BURST_BYTES long.
 */
void bptc19696Encode(const uint8_t *data, uint8_t *burst);

/**
 * \brief Decode the BPTC (196,96) code from the information fields of a data
 * burst, correcting errors with the row and column Hamming codes.
 *
 * @param burst: burst, BURST_BYTES long.
 * @param data: the 12 decoded bytes.
 * @return true if no uncorrectable error has been left in the code.
 */
bool bptc19696Decode(const uint8_t *burst, uint8_t *data);

/**
 * \brief Hamming (16,11,4) code, used by the rows of the embedded link
 * control. Bits are one per byte, data in d[0..10], parity in d[11..15].
 *
 * @param d: 16 bits, parity bits are written.
 */
void hamming16114Encode(uint8_t *d);

/**
 * \brief Check and correct a Hamming (16,11,4) code word.
 *
 * @param d: 16 bits, corrected in place.
 * @return false if the code word has an uncorrectable error.
 */
bool hamming16114Decode(uint8_t *d);

} // namespace DMR

#endif // DMR_FEC_H
