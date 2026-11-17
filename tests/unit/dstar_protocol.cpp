/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include "protocols/DSTAR/Constants.hpp"
#include "protocols/DSTAR/Crc.hpp"
#include "protocols/DSTAR/Header.hpp"
#include "protocols/DSTAR/HeaderCoder.hpp"
#include "protocols/DSTAR/SlowData.hpp"

using namespace DStar;

/*
 * Reference header: flags 0, RPT2 "DIRECT", RPT1 "DIRECT", UR "CQCQCQ",
 * MY "DL1ABC" / "ID51". The CRC (0xBF55) was computed with the table driven
 * algorithm of MMDVM CDStarRX::checksum().
 */
static const uint8_t refHeader[HEADER_SIZE] = {
    0x00, 0x00, 0x00, 'D', 'I', 'R', 'E', 'C', 'T', ' ', ' ', 'D', 'I', 'R',
    'E',  'C',  'T',  ' ', ' ', 'C', 'Q', 'C', 'Q', 'C', 'Q', ' ', ' ', 'D',
    'L',  '1',  'A',  'B', 'C', ' ', ' ', 'I', 'D', '5', '1', 0x55, 0xBF
};

/*
 * refHeader after FEC, interleaving and scrambling, as produced by MMDVM
 * CDStarTX::txHeader() (tail bits set to zero) once its output is shifted
 * by the 4 frame sync bits it carries in its first byte.
 */
static const uint8_t refHeaderFec[HEADER_FEC_SIZE] = {
    0x38, 0xCD, 0xC3, 0x4D, 0x40, 0xFC, 0x38, 0xA8, 0xA0, 0x7C, 0xA1, 0x1C,
    0xFB, 0xA2, 0xB1, 0x5E, 0x07, 0x66, 0x1B, 0xF6, 0x81, 0x06, 0xD9, 0x36,
    0x92, 0xAB, 0x32, 0x82, 0x13, 0xCC, 0x0B, 0xF4, 0x5F, 0x83, 0x1C, 0x58,
    0x2A, 0x94, 0x95, 0xD8, 0xC3, 0xFF, 0xF7, 0xA0, 0x4B, 0xC1, 0xEA, 0xF8,
    0xF3, 0x96, 0x67, 0xF5, 0x1D, 0x8E, 0xCC, 0xE7, 0x9D, 0x6F, 0xAC, 0x2E,
    0xD0, 0xBF, 0x52, 0xAB, 0xBE, 0x32, 0x2B, 0x07, 0x95, 0x53, 0xC9, 0x05,
    0xD6, 0x6A, 0x24, 0xE0, 0x4A, 0x77, 0x1F, 0xAE, 0x49, 0xA6, 0x00
};

static Header makeRefHeader()
{
    Header h;

    h.setRpt2("DIRECT");
    h.setRpt1("direct");
    h.setUr("CQCQCQ");
    h.setMy("DL1ABC");
    h.setSuffix("ID51");

    return h;
}

static void flipBit(uint8_t *buf, size_t i)
{
    buf[i / 8] ^= static_cast<uint8_t>(1 << (i % 8));
}

TEST_CASE("D-STAR header CRC known values", "[dstar]")
{
    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    REQUIRE(crcCcitt(check, sizeof(check)) == 0x906E);
    REQUIRE(crcCcitt(refHeader, HEADER_SIZE - 2) == 0xBF55);
}

TEST_CASE("D-STAR header CRC detects single bit errors", "[dstar]")
{
    uint8_t buf[HEADER_SIZE];
    uint16_t crc = crcCcitt(refHeader, HEADER_SIZE - 2);

    for (size_t i = 0; i < (HEADER_SIZE - 2) * 8; i++) {
        memcpy(buf, refHeader, sizeof(buf));
        flipBit(buf, i);
        REQUIRE(crcCcitt(buf, HEADER_SIZE - 2) != crc);
    }
}

TEST_CASE("D-STAR header fields", "[dstar]")
{
    Header h;
    char out[CALLSIGN_SIZE + 1];

    h.getMy(out);
    REQUIRE(strcmp(out, "") == 0);
    REQUIRE(h.getFlag(0) == 0);
    REQUIRE(h.isRepeater() == false);

    h.setMy("dl1abc");
    h.getMy(out);
    REQUIRE(strcmp(out, "DL1ABC") == 0);

    h.setUr("CQCQCQTOOLONG");
    h.getUr(out);
    REQUIRE(strcmp(out, "CQCQCQTO") == 0);

    h.setRpt1("DB0ABC B");
    h.getRpt1(out);
    REQUIRE(strcmp(out, "DB0ABC B") == 0);

    h.setRpt2("DB0ABC G");
    h.getRpt2(out);
    REQUIRE(strcmp(out, "DB0ABC G") == 0);

    char sfx[SUFFIX_SIZE + 1];
    h.setSuffix("id5");
    h.getSuffix(sfx);
    REQUIRE(strcmp(sfx, "ID5") == 0);

    h.setFlag(0, FLAG_REPEATER);
    REQUIRE(h.isRepeater() == true);
    REQUIRE(h.getFlag(0) == FLAG_REPEATER);
    REQUIRE(h.getFlag(3) == 0);

    h.clear();
    h.getRpt1(out);
    REQUIRE(strcmp(out, "") == 0);
    REQUIRE(h.isRepeater() == false);
}

TEST_CASE("D-STAR header serialisation", "[dstar]")
{
    Header h = makeRefHeader();
    uint8_t buf[HEADER_SIZE];

    h.toBytes(buf);
    REQUIRE(memcmp(buf, refHeader, HEADER_SIZE) == 0);

    h.setFlag(0, FLAG_REPEATER);
    h.setRpt1("DB0ABC B");
    h.toBytes(buf);

    Header r;
    REQUIRE(r.fromBytes(buf) == true);
    REQUIRE(r.isRepeater() == true);

    char out[CALLSIGN_SIZE + 1];
    r.getRpt1(out);
    REQUIRE(strcmp(out, "DB0ABC B") == 0);
    r.getMy(out);
    REQUIRE(strcmp(out, "DL1ABC") == 0);

    // Wrong CRC: fields are loaded anyway
    buf[HEADER_CRC_OFFSET] ^= 0x01;
    Header bad;
    REQUIRE(bad.fromBytes(buf) == false);
    bad.getUr(out);
    REQUIRE(strcmp(out, "CQCQCQ") == 0);
}

TEST_CASE("D-STAR header FEC known encoding", "[dstar]")
{
    uint8_t fec[HEADER_FEC_SIZE];

    encodeHeader(refHeader, fec);
    REQUIRE(memcmp(fec, refHeaderFec, HEADER_FEC_SIZE) == 0);
    REQUIRE((fec[HEADER_FEC_SIZE - 1] & 0xF0) == 0);
}

TEST_CASE("D-STAR header FEC round trip", "[dstar]")
{
    Header h = makeRefHeader();
    h.setFlag(0, FLAG_REPEATER);
    h.setRpt1("DB0XYZ C");
    h.setRpt2("DB0XYZ G");

    uint8_t hdr[HEADER_SIZE];
    uint8_t fec[HEADER_FEC_SIZE];
    uint8_t dec[HEADER_SIZE];

    h.toBytes(hdr);
    encodeHeader(hdr, fec);
    REQUIRE(decodeHeader(fec, dec) == true);
    REQUIRE(memcmp(dec, hdr, HEADER_SIZE) == 0);

    REQUIRE(decodeHeader(refHeaderFec, dec) == true);
    REQUIRE(memcmp(dec, refHeader, HEADER_SIZE) == 0);
}

TEST_CASE("D-STAR header FEC corrects isolated errors", "[dstar]")
{
    uint8_t fec[HEADER_FEC_SIZE];
    uint8_t dec[HEADER_SIZE];

    // Any single bit error
    for (size_t i = 0; i < HEADER_FEC_BITS; i++) {
        memcpy(fec, refHeaderFec, sizeof(fec));
        flipBit(fec, i);
        REQUIRE(decodeHeader(fec, dec) == true);
        REQUIRE(memcmp(dec, refHeader, HEADER_SIZE) == 0);
    }

    // Any two adjacent bit errors
    for (size_t i = 0; i < HEADER_FEC_BITS - 1; i++) {
        memcpy(fec, refHeaderFec, sizeof(fec));
        flipBit(fec, i);
        flipBit(fec, i + 1);
        REQUIRE(decodeHeader(fec, dec) == true);
        REQUIRE(memcmp(dec, refHeader, HEADER_SIZE) == 0);
    }

    // A few spread errors
    memcpy(fec, refHeaderFec, sizeof(fec));
    flipBit(fec, 5);
    flipBit(fec, 100);
    flipBit(fec, 300);
    flipBit(fec, 500);
    flipBit(fec, 650);
    REQUIRE(decodeHeader(fec, dec) == true);
    REQUIRE(memcmp(dec, refHeader, HEADER_SIZE) == 0);
}

TEST_CASE("D-STAR header FEC rejects heavy corruption", "[dstar]")
{
    uint8_t fec[HEADER_FEC_SIZE];
    uint8_t dec[HEADER_SIZE];

    memcpy(fec, refHeaderFec, sizeof(fec));
    for (size_t i = 0; i < HEADER_FEC_SIZE; i += 2)
        fec[i] ^= 0xFF;

    REQUIRE(decodeHeader(fec, dec) == false);
}

TEST_CASE("D-STAR slow data filler", "[dstar]")
{
    SlowDataEncoder enc;
    uint8_t buf[SLOW_DATA_SIZE];

    // 0x66 0x66 0x66 scrambled, MMDVMHost DSTAR_NULL_SLOW_DATA_BYTES
    for (size_t i = 0; i < 2 * (SUPERFRAME_FRAMES - 1); i++) {
        enc.next(buf);
        REQUIRE(buf[0] == 0x16);
        REQUIRE(buf[1] == 0x29);
        REQUIRE(buf[2] == 0xF5);
    }
}

TEST_CASE("D-STAR slow data message and header round trip", "[dstar]")
{
    SlowDataEncoder enc;
    SlowDataDecoder dec;
    uint8_t buf[SLOW_DATA_SIZE];

    Header h = makeRefHeader();
    enc.setMessage("Hello from OpenRTX");
    enc.setHeader(h);

    // First frame: text block 0, scrambled
    enc.next(buf);
    REQUIRE(buf[0] == (0x40 ^ 0x70));
    REQUIRE(buf[1] == ('H' ^ 0x4F));
    REQUIRE(buf[2] == ('e' ^ 0x93));
    enc.reset();
    enc.setMessage("Hello from OpenRTX");
    enc.setHeader(h);

    for (size_t sf = 0; sf < 3; sf++) {
        dec.sync();
        for (size_t f = 1; f < SUPERFRAME_FRAMES; f++) {
            enc.next(buf);
            dec.push(buf);
        }

        // Superframe 0 carries the message, superframe 1 the header
        REQUIRE(dec.hasMessage() == true);
        REQUIRE(strcmp(dec.getMessage(), "Hello from OpenRTX") == 0);
        REQUIRE(dec.hasHeader() == (sf >= 1));
    }

    Header r;
    REQUIRE(dec.getHeader(r) == true);

    uint8_t bytes[HEADER_SIZE];
    r.toBytes(bytes);
    REQUIRE(memcmp(bytes, refHeader, HEADER_SIZE) == 0);

    dec.reset();
    REQUIRE(dec.hasMessage() == false);
    REQUIRE(dec.hasHeader() == false);
    REQUIRE(strcmp(dec.getMessage(), "") == 0);
    REQUIRE(dec.getHeader(r) == false);
}

TEST_CASE("D-STAR slow data header only", "[dstar]")
{
    SlowDataEncoder enc;
    SlowDataDecoder dec;
    uint8_t buf[SLOW_DATA_SIZE];

    Header h = makeRefHeader();
    h.setFlag(0, FLAG_REPEATER);
    enc.setHeader(h);

    dec.sync();
    for (size_t f = 1; f < SUPERFRAME_FRAMES; f++) {
        enc.next(buf);
        dec.push(buf);
    }

    REQUIRE(dec.hasMessage() == false);
    REQUIRE(dec.hasHeader() == true);

    Header r;
    REQUIRE(dec.getHeader(r) == true);
    REQUIRE(r.isRepeater() == true);

    char out[CALLSIGN_SIZE + 1];
    r.getMy(out);
    REQUIRE(strcmp(out, "DL1ABC") == 0);
}

TEST_CASE("D-STAR slow data message only, joined mid stream", "[dstar]")
{
    SlowDataEncoder enc;
    SlowDataDecoder dec;
    uint8_t buf[SLOW_DATA_SIZE];

    enc.setMessage("CQ CQ  ");

    // Receiver misses the first 3 frames of superframe 0
    for (size_t f = 1; f < 4; f++)
        enc.next(buf);

    for (size_t f = 4; f < SUPERFRAME_FRAMES; f++) {
        enc.next(buf);
        dec.push(buf);
    }

    REQUIRE(dec.hasMessage() == false);

    dec.sync();
    for (size_t f = 1; f < SUPERFRAME_FRAMES; f++) {
        enc.next(buf);
        dec.push(buf);
    }

    REQUIRE(dec.hasMessage() == true);
    REQUIRE(strcmp(dec.getMessage(), "CQ CQ") == 0);
    REQUIRE(dec.hasHeader() == false);
}
