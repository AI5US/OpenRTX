/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_CSBK_H
#define DMR_CSBK_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

namespace DMR
{

/// Size of a control signalling block, CRC included
static constexpr size_t CSBK_BYTES = 12;

/**
 * Build a BS outbound activation CSBK burst, sent by a radio to wake up an
 * idle repeater (MMDVMHost DMRCSBK.cpp): BPTC coded CSBK with its masked
 * CRC, CSBK slot type and MS data sync.
 *
 * @param bsId: repeater to wake up, ALL_ID for any.
 * @param srcId: radio ID.
 * @param colorCode: colour code, 0 to 15.
 * @param burst: output, BURST_BYTES bytes.
 */
void encodeBsDwnAct(uint32_t bsId, uint32_t srcId, uint8_t colorCode,
                    uint8_t *burst);

/**
 * Read the CSBK of a data burst.
 *
 * @param burst: burst carrying a CSBK.
 * @param csbk: output, CSBK_BYTES bytes, written in any case.
 * @return true if the CRC is valid.
 */
bool decodeCsbk(const uint8_t *burst, uint8_t *csbk);

} // namespace DMR

#endif // DMR_CSBK_H
