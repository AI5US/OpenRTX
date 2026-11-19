/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_BITS_H
#define YSF_BITS_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

namespace YSF
{

/**
 * Read a bit of a byte array, first bit in the most significant position.
 *
 * @param data: byte array.
 * @param pos: bit position.
 * @return the bit.
 */
bool readBit(const uint8_t *data, size_t pos);

/**
 * Write a bit of a byte array, first bit in the most significant position.
 *
 * @param data: byte array.
 * @param pos: bit position.
 * @param bit: value to write.
 */
void writeBit(uint8_t *data, size_t pos, bool bit);

} // namespace YSF

#endif // YSF_BITS_H
