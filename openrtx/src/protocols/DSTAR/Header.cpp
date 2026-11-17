/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DSTAR/Crc.hpp"
#include "protocols/DSTAR/Header.hpp"

using namespace DStar;

Header::Header()
{
    clear();
}

void Header::clear()
{
    memset(data, ' ', sizeof(data));

    for (size_t i = 0; i < HEADER_FLAGS; i++)
        data[i] = 0x00;

    data[HEADER_CRC_OFFSET] = 0x00;
    data[HEADER_CRC_OFFSET + 1] = 0x00;
}

uint8_t Header::getFlag(size_t n) const
{
    if (n >= HEADER_FLAGS)
        return 0;

    return data[n];
}

void Header::setFlag(size_t n, uint8_t v)
{
    if (n >= HEADER_FLAGS)
        return;

    data[n] = v;
}

void Header::setMy(const char *cs)
{
    setField(HEADER_MY_OFFSET, CALLSIGN_SIZE, cs);
}

void Header::setSuffix(const char *s)
{
    setField(HEADER_SUFFIX_OFFSET, SUFFIX_SIZE, s);
}

void Header::setUr(const char *cs)
{
    setField(HEADER_UR_OFFSET, CALLSIGN_SIZE, cs);
}

void Header::setRpt1(const char *cs)
{
    setField(HEADER_RPT1_OFFSET, CALLSIGN_SIZE, cs);
}

void Header::setRpt2(const char *cs)
{
    setField(HEADER_RPT2_OFFSET, CALLSIGN_SIZE, cs);
}

void Header::getMy(char *out) const
{
    getField(HEADER_MY_OFFSET, CALLSIGN_SIZE, out);
}

void Header::getSuffix(char *out) const
{
    getField(HEADER_SUFFIX_OFFSET, SUFFIX_SIZE, out);
}

void Header::getUr(char *out) const
{
    getField(HEADER_UR_OFFSET, CALLSIGN_SIZE, out);
}

void Header::getRpt1(char *out) const
{
    getField(HEADER_RPT1_OFFSET, CALLSIGN_SIZE, out);
}

void Header::getRpt2(char *out) const
{
    getField(HEADER_RPT2_OFFSET, CALLSIGN_SIZE, out);
}

void Header::toBytes(uint8_t out[HEADER_SIZE]) const
{
    memcpy(out, data, HEADER_CRC_OFFSET);

    uint16_t crc = crcCcitt(out, HEADER_CRC_OFFSET);
    out[HEADER_CRC_OFFSET] = static_cast<uint8_t>(crc & 0xFF);
    out[HEADER_CRC_OFFSET + 1] = static_cast<uint8_t>(crc >> 8);
}

bool Header::fromBytes(const uint8_t in[HEADER_SIZE])
{
    memcpy(data, in, HEADER_SIZE);

    uint16_t crc = crcCcitt(in, HEADER_CRC_OFFSET);
    uint16_t rxCrc = static_cast<uint16_t>(in[HEADER_CRC_OFFSET])
                   | static_cast<uint16_t>(in[HEADER_CRC_OFFSET + 1] << 8);

    return crc == rxCrc;
}

bool Header::isRepeater() const
{
    return (data[0] & FLAG_REPEATER) != 0;
}

void Header::setField(size_t offset, size_t size, const char *text)
{
    size_t i = 0;

    if (text != nullptr) {
        for (; (i < size) && (text[i] != '\0'); i++) {
            char c = text[i];

            if ((c >= 'a') && (c <= 'z'))
                c = static_cast<char>(c - 'a' + 'A');

            data[offset + i] = static_cast<uint8_t>(c);
        }
    }

    for (; i < size; i++)
        data[offset + i] = ' ';
}

void Header::getField(size_t offset, size_t size, char *out) const
{
    for (size_t i = 0; i < size; i++)
        out[i] = static_cast<char>(data[offset + i]);

    size_t len = size;
    while ((len > 0) && (out[len - 1] == ' '))
        len--;

    out[len] = '\0';
}
