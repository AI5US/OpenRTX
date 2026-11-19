/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2021 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "core/crc.h"
#include "protocols/M17/Golay.hpp"
#include "protocols/YSF/Bits.hpp"
#include "protocols/YSF/Constants.hpp"
#include "protocols/YSF/Fich.hpp"

using namespace YSF;

/// Coded bit pairs of the FICH
static constexpr size_t FICH_PAIRS = 100;

/// Information bits of the FICH: 4 Golay words, then a zero tail byte
static constexpr size_t FICH_BITS = 96;

/// Interleave: position of each coded bit pair in the FICH (MMDVMHost)
static constexpr uint8_t INTERLEAVE[FICH_PAIRS] = {
    0,  40, 80,  120, 160, 2,  42, 82,  122, 162, 4,  44, 84,  124, 164,
    6,  46, 86,  126, 166, 8,  48, 88,  128, 168, 10, 50, 90,  130, 170,
    12, 52, 92,  132, 172, 14, 54, 94,  134, 174, 16, 56, 96,  136, 176,
    18, 58, 98,  138, 178, 20, 60, 100, 140, 180, 22, 62, 102, 142, 182,
    24, 64, 104, 144, 184, 26, 66, 106, 146, 186, 28, 68, 108, 148, 188,
    30, 70, 110, 150, 190, 32, 72, 112, 152, 192, 34, 74, 114, 154, 194,
    36, 76, 116, 156, 196, 38, 78, 118, 158, 198,
};

/// CRC of the FICH, as sent: CRC-16/CCITT, inverted (MMDVMHost CRC.cpp
/// addCCITT162)
static uint16_t fichCrc(const uint8_t *data)
{
    return static_cast<uint16_t>(~crc_ccitt(data, 4));
}

Fich::Fich()
{
    fich.fill(0);
}

Fich::~Fich()
{
}

bool Fich::decode(const uint8_t *frame, Convolution &conv)
{
    const uint8_t *bytes = frame + SYNC_BYTES;

    conv.start();
    for (size_t i = 0; i < FICH_PAIRS; i++) {
        size_t n = INTERLEAVE[i];
        uint8_t s0 = readBit(bytes, n) ? 1 : 0;
        uint8_t s1 = readBit(bytes, n + 1) ? 1 : 0;

        conv.decode(s0, s1);
    }

    uint8_t out[13];
    conv.chainback(out, FICH_BITS);

    uint16_t b[4];
    for (size_t i = 0; i < 4; i++) {
        const uint8_t *w = &out[3 * i];
        uint32_t word = (static_cast<uint32_t>(w[0]) << 16)
                      | (static_cast<uint32_t>(w[1]) << 8) | w[2];

        b[i] = M17::golay24_decode(word);
        if (b[i] == 0xFFFF)
            return false;
    }

    fich[0] = static_cast<uint8_t>(b[0] >> 4);
    fich[1] = static_cast<uint8_t>((b[0] << 4) | (b[1] >> 8));
    fich[2] = static_cast<uint8_t>(b[1]);
    fich[3] = static_cast<uint8_t>(b[2] >> 4);
    fich[4] = static_cast<uint8_t>((b[2] << 4) | (b[3] >> 8));
    fich[5] = static_cast<uint8_t>(b[3]);

    uint16_t crc = fichCrc(fich.data());
    return (fich[4] == static_cast<uint8_t>(crc >> 8))
        && (fich[5] == static_cast<uint8_t>(crc));
}

void Fich::encode(uint8_t *frame)
{
    uint16_t crc = fichCrc(fich.data());
    fich[4] = static_cast<uint8_t>(crc >> 8);
    fich[5] = static_cast<uint8_t>(crc);

    uint16_t b[4];
    b[0] = static_cast<uint16_t>((fich[0] << 4) | (fich[1] >> 4));
    b[1] = static_cast<uint16_t>(((fich[1] & 0x0F) << 8) | fich[2]);
    b[2] = static_cast<uint16_t>((fich[3] << 4) | (fich[4] >> 4));
    b[3] = static_cast<uint16_t>(((fich[4] & 0x0F) << 8) | fich[5]);

    uint8_t words[13];
    for (size_t i = 0; i < 4; i++) {
        uint32_t c = M17::golay24_encode(b[i]);

        words[3 * i] = static_cast<uint8_t>(c >> 16);
        words[(3 * i) + 1] = static_cast<uint8_t>(c >> 8);
        words[(3 * i) + 2] = static_cast<uint8_t>(c);
    }
    words[12] = 0x00;

    uint8_t coded[FICH_BYTES];
    Convolution conv;
    conv.encode(words, coded, FICH_PAIRS);

    uint8_t *bytes = frame + SYNC_BYTES;
    for (size_t i = 0; i < FICH_PAIRS; i++) {
        size_t n = INTERLEAVE[i];

        writeBit(bytes, n, readBit(coded, 2 * i));
        writeBit(bytes, n + 1, readBit(coded, (2 * i) + 1));
    }
}

uint8_t Fich::fi() const
{
    return (fich[0] >> 6) & 0x03;
}

uint8_t Fich::cs() const
{
    return (fich[0] >> 4) & 0x03;
}

uint8_t Fich::cm() const
{
    return (fich[0] >> 2) & 0x03;
}

uint8_t Fich::fn() const
{
    return (fich[1] >> 3) & 0x07;
}

uint8_t Fich::ft() const
{
    return fich[1] & 0x07;
}

uint8_t Fich::dt() const
{
    return fich[2] & 0x03;
}

uint8_t Fich::mr() const
{
    return (fich[2] >> 3) & 0x07;
}

uint8_t Fich::dgId() const
{
    return fich[3] & 0x7F;
}

void Fich::setFi(uint8_t fi)
{
    fich[0] = static_cast<uint8_t>((fich[0] & 0x3F) | ((fi & 0x03) << 6));
}

void Fich::setCs(uint8_t cs)
{
    fich[0] = static_cast<uint8_t>((fich[0] & 0xCF) | ((cs & 0x03) << 4));
}

void Fich::setCm(uint8_t cm)
{
    fich[0] = static_cast<uint8_t>((fich[0] & 0xF3) | ((cm & 0x03) << 2));
}

void Fich::setFn(uint8_t fn)
{
    fich[1] = static_cast<uint8_t>((fich[1] & 0xC7) | ((fn & 0x07) << 3));
}

void Fich::setFt(uint8_t ft)
{
    fich[1] = static_cast<uint8_t>((fich[1] & 0xF8) | (ft & 0x07));
}

void Fich::setDt(uint8_t dt)
{
    fich[2] = static_cast<uint8_t>((fich[2] & 0xFC) | (dt & 0x03));
}

void Fich::setMr(uint8_t mr)
{
    fich[2] = static_cast<uint8_t>((fich[2] & 0xC7) | ((mr & 0x07) << 3));
}

void Fich::setDgId(uint8_t id)
{
    fich[3] = static_cast<uint8_t>((fich[3] & 0x80) | (id & 0x7F));
}
