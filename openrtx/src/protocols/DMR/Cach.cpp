/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Cach.hpp"

using namespace DMR;

/*
 * Positions of the 7 TACT bits in the CACH: AT, TC, LCSS(1), LCSS(0), then
 * the three Hamming (7,4) parity bits (MMDVM DMRTX.cpp createCACH).
 */
static constexpr uint8_t TACT_POS[7] = { 0, 4, 8, 12, 14, 18, 22 };

/// Parity bits of a TACT, from its four data bits
static void tactParity(const bool *d, bool *p)
{
    p[0] = d[0] ^ d[1] ^ d[2];
    p[1] = d[1] ^ d[2] ^ d[3];
    p[2] = d[0] ^ d[1] ^ d[3];
}

void DMR::encodeCach(const Tact &tact, uint8_t *cach)
{
    bool d[4] = { tact.at, tact.slot != 0, (tact.lcss & 0x02) != 0,
                  (tact.lcss & 0x01) != 0 };
    bool p[3];
    tactParity(d, p);

    memset(cach, 0x00, 3);
    for (size_t i = 0; i < 4; i++)
        writeBit(cach, TACT_POS[i], d[i]);
    for (size_t i = 0; i < 3; i++)
        writeBit(cach, TACT_POS[4 + i], p[i]);
}

void DMR::decodeCach(const uint8_t *cach, Tact &tact)
{
    bool d[4];
    bool r[3];
    for (size_t i = 0; i < 4; i++)
        d[i] = readBit(cach, TACT_POS[i]);
    for (size_t i = 0; i < 3; i++)
        r[i] = readBit(cach, TACT_POS[4 + i]);

    bool p[3];
    tactParity(d, p);
    uint8_t syndrome = static_cast<uint8_t>(((p[0] ^ r[0]) ? 0x04 : 0)
                                            | ((p[1] ^ r[1]) ? 0x02 : 0)
                                            | ((p[2] ^ r[2]) ? 0x01 : 0));

    // Data bit in error for each syndrome; the others point at a parity bit
    switch (syndrome) {
        case 0x05:
            d[0] = !d[0];
            break;
        case 0x07:
            d[1] = !d[1];
            break;
        case 0x06:
            d[2] = !d[2];
            break;
        case 0x03:
            d[3] = !d[3];
            break;
        default:
            break;
    }

    tact.at = d[0];
    tact.slot = d[1] ? 1 : 0;
    tact.lcss = static_cast<uint8_t>((d[2] ? 0x02 : 0) | (d[3] ? 0x01 : 0));
}
