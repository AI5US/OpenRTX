/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2009-2020 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_CRC_H
#define DSTAR_CRC_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>

namespace DStar
{

/**
 * \brief Compute the D-STAR radio header CRC.
 *
 * CRC-16/CCITT in its reflected form (polynomial 0x1021, processed LSB
 * first as 0x8408), initial value 0xFFFF, result inverted. This is the CRC
 * also known as CRC-16/X-25 (check value for "123456789": 0x906E), as
 * computed by MMDVM CDStarRX::checksum() and MMDVMHost CCRC::addCCITT161().
 *
 * In the radio header the CRC covers bytes 0 to 38 and is stored in bytes
 * 39 and 40 low byte first: header[39] = crc & 0xFF, header[40] = crc >> 8.
 *
 * @param data: data to be processed.
 * @param length: number of bytes of data.
 * @return the CRC value.
 */
inline uint16_t crcCcitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001) != 0)
                crc = (crc >> 1) ^ 0x8408;
            else
                crc = crc >> 1;
        }
    }

    return static_cast<uint16_t>(~crc);
}

} // namespace DStar

#endif // DSTAR_CRC_H
