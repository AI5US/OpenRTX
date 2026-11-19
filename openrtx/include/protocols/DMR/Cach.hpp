/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_CACH_H
#define DMR_CACH_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace DMR
{

/**
 * TDMA access channel type (TACT) of a CACH, the 24 bits a repeater sends
 * before each burst (ETSI TS 102 361-1, 9.3.6; MMDVM DMRTX.cpp createCACH).
 * The other 17 bits of the CACH carry short link control, not decoded here.
 */
struct Tact {
    bool at;      ///< Access type: the inbound timeslot is busy
    uint8_t slot; ///< Timeslot of the following burst, 0 (TS1) or 1 (TS2)
    uint8_t lcss; ///< Short link control start/stop
};

/**
 * Build a CACH carrying a TACT and an empty short link control.
 *
 * @param tact: TACT to send.
 * @param cach: output, 3 bytes, first bit in the most significant bit.
 */
void encodeCach(const Tact &tact, uint8_t *cach);

/**
 * Read the TACT of a CACH, correcting a single bit error with its Hamming
 * (7,4) code. Two or more errors cannot be detected: check the result for
 * consistency, e.g. timeslots alternating from burst to burst.
 *
 * @param cach: 3 bytes, first bit in the most significant bit.
 * @param tact: decoded TACT.
 */
void decodeCach(const uint8_t *cach, Tact &tact);

} // namespace DMR

#endif // DMR_CACH_H
