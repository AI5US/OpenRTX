/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

#include "protocols/YSF/Constants.hpp"
#include "protocols/YSF/Convolution.hpp"
#include "protocols/YSF/Fich.hpp"
#include "protocols/YSF/Payload.hpp"
#include "protocols/YSF/Voice.hpp"

using namespace YSF;

static void flipBit(uint8_t *data, size_t pos)
{
    data[pos / 8] ^= static_cast<uint8_t>(0x80 >> (pos % 8));
}

/// Test voice frames: 49 bits, the unused low bits of the last byte clear
static void testVoice(uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES],
                      uint8_t seed)
{
    for (size_t i = 0; i < VOICE_FRAMES; i++) {
        for (size_t j = 0; j < VOICE_FRAME_BYTES; j++)
            voice[i][j] = static_cast<uint8_t>((seed + (37 * i) + (11 * j)));

        voice[i][VOICE_FRAME_BYTES - 1] &= 0x80;
    }
}

TEST_CASE("YSF FICH round trip with bit errors", "[ysf]")
{
    for (uint8_t fn = 0; fn < 8; fn++) {
        Fich fich;
        fich.setFi(FI_COMMUNICATIONS);
        fich.setCs(CS_RADIO);
        fich.setFn(fn);
        fich.setFt(VD_MODE2_FT);
        fich.setDt(DT_VD_MODE2);
        fich.setMr(MR_DIRECT);
        fich.setDgId(fn * 10);

        uint8_t frame[FRAME_BYTES] = {};
        fich.encode(frame);

        // A few spread errors are corrected by the convolutional code
        flipBit(frame, (SYNC_BYTES * 8) + 3);
        flipBit(frame, (SYNC_BYTES * 8) + 101);
        flipBit(frame, (SYNC_BYTES * 8) + 177);

        Convolution conv;
        Fich out;
        REQUIRE(out.decode(frame, conv));
        REQUIRE(out.fi() == FI_COMMUNICATIONS);
        REQUIRE(out.cs() == CS_RADIO);
        REQUIRE(out.fn() == fn);
        REQUIRE(out.ft() == VD_MODE2_FT);
        REQUIRE(out.dt() == DT_VD_MODE2);
        REQUIRE(out.dgId() == fn * 10);
    }
}

TEST_CASE("YSF DCH round trip and CRC", "[ysf]")
{
    Convolution conv;
    uint8_t frame[FRAME_BYTES] = {};

    const uint8_t vd2[VD2_DCH_BYTES] = { 'A', 'I', '5', 'U', 'S',
                                         ' ', ' ', ' ', ' ', ' ' };
    writeVd2Dch(vd2, frame);
    flipBit(frame, (PAYLOAD_OFFSET * 8) + 9);

    uint8_t out[FR_DCH_BYTES];
    REQUIRE(readVd2Dch(frame, conv, out));
    REQUIRE(memcmp(out, vd2, VD2_DCH_BYTES) == 0);

    uint8_t csd[2][FR_DCH_BYTES];
    for (size_t i = 0; i < FR_DCH_BYTES; i++) {
        csd[0][i] = static_cast<uint8_t>('A' + i);
        csd[1][i] = static_cast<uint8_t>('a' + i);
    }

    writeFrDch(0, csd[0], frame);
    writeFrDch(1, csd[1], frame);
    for (size_t i = 0; i < 2; i++) {
        REQUIRE(readFrDch(i, frame, conv, out));
        REQUIRE(memcmp(out, csd[i], FR_DCH_BYTES) == 0);
    }

    // Damage beyond correction is caught by the CRC
    for (size_t i = 0; i < 6; i++)
        frame[PAYLOAD_OFFSET + i] ^= 0xFF;
    REQUIRE(readFrDch(0, frame, conv, out) == false);
}

TEST_CASE("YSF voice channel corrects the repeated bits", "[ysf]")
{
    uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES];
    testVoice(voice, 7);

    uint8_t frame[FRAME_BYTES] = {};
    for (size_t i = 0; i < VOICE_FRAMES; i++)
        writeVch(i, voice[i], frame);

    // One error in the first triplet of each voice channel: VCH bit 0 is
    // the first bit after the block's 5 DCH bytes
    for (size_t i = 0; i < VOICE_FRAMES; i++)
        flipBit(frame, ((PAYLOAD_OFFSET + (i * BLOCK_BYTES) + 5) * 8));

    for (size_t i = 0; i < VOICE_FRAMES; i++) {
        uint8_t out[VOICE_FRAME_BYTES];
        REQUIRE(readVch(i, frame, out) == 1);
        REQUIRE(memcmp(out, voice[i], VOICE_FRAME_BYTES) == 0);
    }
}

TEST_CASE("YSF voice transmission decodes", "[ysf]")
{
    VoiceEncoder enc;
    VoiceDecoder dec;
    enc.start("AI5US", "ALL", 0);

    uint8_t frame[FRAME_BYTES];
    VoiceEvent ev;

    enc.header(frame);
    dec.process(frame, ev);
    REQUIRE(ev.callStart);
    REQUIRE(ev.sourceValid);
    REQUIRE(strcmp(ev.source, "AI5US") == 0);
    REQUIRE(strcmp(ev.destination, "ALL") == 0);
    REQUIRE(dec.inCall());

    for (uint8_t n = 0; n < 12; n++) {
        uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES];
        testVoice(voice, n);

        enc.voice(voice, frame);
        dec.process(frame, ev);
        REQUIRE(ev.voice);
        REQUIRE(ev.callStart == false);
        REQUIRE(ev.voiceErrors == 0);
        REQUIRE(memcmp(ev.frames, voice, sizeof(voice)) == 0);
    }

    enc.terminator(frame);
    dec.process(frame, ev);
    REQUIRE(ev.callEnd);
    REQUIRE(dec.inCall() == false);
}

TEST_CASE("YSF late entry finds the source in the DCH", "[ysf]")
{
    VoiceEncoder enc;
    VoiceDecoder dec;
    enc.start("AI5US", "ALL", 0);

    uint8_t frame[FRAME_BYTES];
    uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES];
    testVoice(voice, 3);

    // Skip the header and the destination frame
    enc.header(frame);
    enc.voice(voice, frame);

    VoiceEvent ev;
    enc.voice(voice, frame);
    dec.process(frame, ev);
    REQUIRE(ev.callStart);
    REQUIRE(ev.sourceValid);
    REQUIRE(strcmp(ev.source, "AI5US") == 0);
    REQUIRE(ev.voice);
}
