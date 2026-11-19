/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/YSF/Bits.hpp"

bool YSF::readBit(const uint8_t *data, size_t pos)
{
    return ((data[pos / 8] >> (7 - (pos % 8))) & 0x01) != 0;
}

void YSF::writeBit(uint8_t *data, size_t pos, bool bit)
{
    uint8_t mask = static_cast<uint8_t>(0x80 >> (pos % 8));

    if (bit)
        data[pos / 8] |= mask;
    else
        data[pos / 8] &= static_cast<uint8_t>(~mask);
}
