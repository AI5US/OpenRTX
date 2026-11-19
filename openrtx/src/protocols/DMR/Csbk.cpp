/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "core/crc.h"
#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Csbk.hpp"
#include "protocols/DMR/Fec.hpp"

using namespace DMR;

/// Last block flag of a CSBK
static constexpr uint8_t CSBK_LAST_BLOCK = 0x80;

/**
 * CRC of the first 10 bytes of a CSBK, masked, as sent in its last two
 * bytes (MMDVMHost CRC.cpp addCCITT162: CRC-16/CCITT, inverted).
 */
static uint16_t csbkCrc(const uint8_t *csbk)
{
    uint16_t crc = static_cast<uint16_t>(~crc_ccitt(csbk, CSBK_BYTES - 2));

    return crc ^ ((CSBK_CRC_MASK[0] << 8) | CSBK_CRC_MASK[1]);
}

void DMR::encodeBsDwnAct(uint32_t bsId, uint32_t srcId, uint8_t colorCode,
                         uint8_t *burst)
{
    uint8_t csbk[CSBK_BYTES];
    memset(csbk, 0x00, sizeof(csbk));

    csbk[0] = CSBK_LAST_BLOCK | CSBKO_BS_DWN_ACT;
    csbk[4] = static_cast<uint8_t>(bsId >> 16);
    csbk[5] = static_cast<uint8_t>(bsId >> 8);
    csbk[6] = static_cast<uint8_t>(bsId);
    csbk[7] = static_cast<uint8_t>(srcId >> 16);
    csbk[8] = static_cast<uint8_t>(srcId >> 8);
    csbk[9] = static_cast<uint8_t>(srcId);

    uint16_t crc = csbkCrc(csbk);
    csbk[10] = static_cast<uint8_t>(crc >> 8);
    csbk[11] = static_cast<uint8_t>(crc);

    memset(burst, 0x00, BURST_BYTES);
    bptc19696Encode(csbk, burst);
    encodeSlotType(burst, colorCode, DT_CSBK);
    setSync(burst, MS_DATA_SYNC);
}

bool DMR::decodeCsbk(const uint8_t *burst, uint8_t *csbk)
{
    bptc19696Decode(burst, csbk);

    uint16_t crc = csbkCrc(csbk);
    return (csbk[10] == static_cast<uint8_t>(crc >> 8))
        && (csbk[11] == static_cast<uint8_t>(crc));
}
