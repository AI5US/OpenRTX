/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Fec.hpp"
#include "protocols/DMR/Lc.hpp"
#include "protocols/DMR/Voice.hpp"

using namespace DMR;

/*
 * Reference bursts generated with MMDVMHost (CDMRFullLC, CDMRSlotType,
 * CDMREmbeddedData, CDMREMB) for a group call from 3100001 to talkgroup 91,
 * colour code 1, sync fields left to zero: voice LC header, terminator with
 * LC, then voice bursts B to F with only the EMB and embedded signalling set.
 */
static const char *REF_HEADER =
    "03C30FD218701F4042101540446000000000000E31500A7025200AA066C1750354";
static const char *REF_TERMINATOR =
    "03AC0F0618C01F38426015E044A000000000000964440948224006E06EC16C0367";
static const char *REF_VOICE[5] = {
    "00000000000000000000000000013050F0C0691000000000000000000000000000",
    "000000000000000000000000000170A0C000074000000000000000000000000000",
    "000000000000000000000000000170C17050074000000000000000000000000000",
    "00000000000000000000000000015141E091B07000000000000000000000000000",
    "0000000000000000000000000001100000000E2000000000000000000000000000",
};

/// Same call as a private call (MMDVMHost FLCO::USER_USER)
static const char *REF_PRIVATE_HEADER =
    "43A70F5919D21E7046601C00446000000000000E31700B4820B008A060D1530371";

static std::string toHex(const uint8_t *data, size_t len)
{
    std::string out;
    char buf[3];

    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        out += buf;
    }

    return out;
}

static void fromHex(const char *hex, uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned int v;
        sscanf(hex + (2 * i), "%2x", &v);
        data[i] = static_cast<uint8_t>(v);
    }
}

static void flipBit(uint8_t *data, size_t pos)
{
    data[pos / 8] ^= static_cast<uint8_t>(0x80 >> (pos % 8));
}

static Lc makeLc(uint8_t flco)
{
    Lc lc = {};
    lc.flco = flco;
    lc.src = 3100001;
    lc.dst = 91;
    return lc;
}

static void makeFrames(uint8_t seed,
                       uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES])
{
    for (size_t f = 0; f < VOICE_FRAMES; f++) {
        for (size_t i = 0; i < VOICE_FRAME_BYTES; i++)
            frames[f][i] = static_cast<uint8_t>((seed * 31) + (f * 9) + i);
    }
}

TEST_CASE("DMR Golay (20,8) corrects up to 3 errors", "[dmr][fec]")
{
    for (uint32_t d = 0; d < 256; d++) {
        uint32_t code = golay2087Encode(static_cast<uint8_t>(d));
        REQUIRE((code >> 12) == d);

        uint8_t errors;
        REQUIRE(golay2087Decode(code, &errors) == d);
        REQUIRE(errors == 0);

        // Three errors spread over the code word
        uint32_t bad = code ^ (1u << (d % 20)) ^ (1u << ((d + 7) % 20))
                     ^ (1u << ((d + 13) % 20));
        REQUIRE(golay2087Decode(bad, &errors) == d);
        REQUIRE(errors == 3);
    }
}

TEST_CASE("DMR QR (16,7) corrects up to 2 errors", "[dmr][fec]")
{
    for (uint8_t d = 0; d < 128; d++) {
        uint16_t code = qr1676Encode(d);
        REQUIRE((code >> 9) == d);

        uint16_t bad = code ^ static_cast<uint16_t>(1u << (d % 16))
                     ^ static_cast<uint16_t>(1u << ((d + 5) % 16));

        uint8_t errors;
        REQUIRE(qr1676Decode(bad, &errors) == d);
        REQUIRE(errors == 2);
    }
}

TEST_CASE("DMR BPTC (196,96) corrects single errors", "[dmr][fec]")
{
    uint8_t data[12];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = static_cast<uint8_t>(0x5A ^ (i * 37));

    uint8_t burst[BURST_BYTES];
    memset(burst, 0xFF, sizeof(burst));
    bptc19696Encode(data, burst);

    // The slot type and sync fields are left untouched
    for (size_t pos = 98; pos < 166; pos++)
        REQUIRE(readBit(burst, pos));

    uint8_t out[12];
    REQUIRE(bptc19696Decode(burst, out));
    REQUIRE(memcmp(out, data, sizeof(data)) == 0);

    // Any single error in the information fields is corrected
    for (size_t pos = 0; pos < BURST_BITS; pos++) {
        if ((pos >= 98) && (pos < 166))
            continue;

        uint8_t bad[BURST_BYTES];
        memcpy(bad, burst, sizeof(bad));
        flipBit(bad, pos);

        REQUIRE(bptc19696Decode(bad, out));
        REQUIRE(memcmp(out, data, sizeof(data)) == 0);
    }
}

TEST_CASE("DMR Hamming (16,11,4) corrects single errors", "[dmr][fec]")
{
    uint8_t ref[16] = { 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1 };
    hamming16114Encode(ref);

    for (size_t pos = 0; pos < 16; pos++) {
        uint8_t d[16];
        memcpy(d, ref, sizeof(d));
        d[pos] ^= 0x01;

        REQUIRE(hamming16114Decode(d));
        REQUIRE(memcmp(d, ref, sizeof(d)) == 0);
    }

    // Two errors are detected
    uint8_t d[16];
    memcpy(d, ref, sizeof(d));
    d[0] ^= 0x01;
    d[1] ^= 0x01;
    REQUIRE(hamming16114Decode(d) == false);
}

TEST_CASE("DMR bursts match MMDVMHost", "[dmr][protocol]")
{
    VoiceEncoder enc;
    enc.start(makeLc(FLCO_GROUP), 1);

    uint8_t burst[BURST_BYTES];

    enc.header(burst);
    REQUIRE(getSync(burst) == MS_DATA_SYNC);
    setSync(burst, 0);
    REQUIRE(toHex(burst, BURST_BYTES) == REF_HEADER);

    enc.terminator(burst);
    setSync(burst, 0);
    REQUIRE(toHex(burst, BURST_BYTES) == REF_TERMINATOR);

    // Voice bursts with null frames: burst A, then B to F
    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
    memset(frames, 0x00, sizeof(frames));

    enc.voice(frames, burst);
    REQUIRE(getSync(burst) == MS_VOICE_SYNC);

    for (size_t n = 0; n < 5; n++) {
        enc.voice(frames, burst);
        REQUIRE(toHex(burst, BURST_BYTES) == REF_VOICE[n]);
    }

    // Next superframe
    REQUIRE(enc.nextVoiceIndex() == 0);

    VoiceEncoder priv;
    priv.start(makeLc(FLCO_USER_USER), 1);
    priv.header(burst);
    setSync(burst, 0);
    REQUIRE(toHex(burst, BURST_BYTES) == REF_PRIVATE_HEADER);
}

TEST_CASE("DMR full LC decodes MMDVMHost bursts", "[dmr][protocol]")
{
    uint8_t burst[BURST_BYTES];
    fromHex(REF_HEADER, burst, BURST_BYTES);

    uint8_t cc, dt;
    REQUIRE(decodeSlotType(burst, cc, dt));
    REQUIRE(cc == 1);
    REQUIRE(dt == DT_VOICE_LC_HEADER);

    Lc lc;
    REQUIRE(decodeFullLc(burst, DT_VOICE_LC_HEADER, lc));
    REQUIRE(lc.isGroup());
    REQUIRE(lc.src == 3100001);
    REQUIRE(lc.dst == 91);

    // The checksum mask tells headers and terminators apart
    REQUIRE(decodeFullLc(burst, DT_TERMINATOR_WITH_LC, lc) == false);

    fromHex(REF_TERMINATOR, burst, BURST_BYTES);
    REQUIRE(decodeFullLc(burst, DT_TERMINATOR_WITH_LC, lc));

    fromHex(REF_PRIVATE_HEADER, burst, BURST_BYTES);
    REQUIRE(decodeFullLc(burst, DT_VOICE_LC_HEADER, lc));
    REQUIRE(lc.isGroup() == false);
    REQUIRE(lc.flco == FLCO_USER_USER);
}

TEST_CASE("DMR voice burst fields round trip", "[dmr][protocol]")
{
    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
    makeFrames(3, frames);

    uint8_t burst[BURST_BYTES];
    memset(burst, 0x00, sizeof(burst));
    setVoiceFrames(burst, frames);

    // The second frame is split around the sync field
    REQUIRE(getSync(burst) == 0);

    uint8_t out[VOICE_FRAMES][VOICE_FRAME_BYTES];
    getVoiceFrames(burst, out);
    REQUIRE(memcmp(out, frames, sizeof(frames)) == 0);

    encodeEmb(burst, 9, true, LCSS_CONT);

    uint8_t cc, lcss;
    bool pi;
    REQUIRE(decodeEmb(burst, cc, pi, lcss));
    REQUIRE(cc == 9);
    REQUIRE(pi);
    REQUIRE(lcss == LCSS_CONT);

    // The EMB does not touch the voice
    getVoiceFrames(burst, out);
    REQUIRE(memcmp(out, frames, sizeof(frames)) == 0);
}

TEST_CASE("DMR embedded LC round trip", "[dmr][protocol]")
{
    Lc lc = makeLc(FLCO_USER_USER);
    lc.src = 0xABCDEF;
    lc.dst = 0x123456;

    EmbeddedLcEncoder enc;
    enc.setLc(lc);

    EmbeddedLcDecoder dec;
    Lc out = {};
    uint8_t data[4];

    for (size_t n = 0; n < 3; n++) {
        uint8_t lcss = enc.getFragment(n, data);
        REQUIRE(dec.push(lcss, data, out) == false);
    }

    uint8_t lcss = enc.getFragment(3, data);
    REQUIRE(lcss == LCSS_LAST);

    // A single bit error per fragment row is corrected
    uint8_t bad[4];
    memcpy(bad, data, sizeof(bad));
    flipBit(bad, 5);
    REQUIRE(dec.push(lcss, bad, out));
    REQUIRE(out.src == 0xABCDEF);
    REQUIRE(out.dst == 0x123456);
    REQUIRE(out.flco == FLCO_USER_USER);

    // Fragments out of order are rejected
    REQUIRE(dec.push(LCSS_CONT, data, out) == false);
    REQUIRE(dec.push(LCSS_LAST, data, out) == false);
}

/*
 * A transmission as a sequence of bursts with the sync type the receiver
 * would report: header, a number of voice superframes, terminator.
 */
struct TxBurst {
    uint8_t data[BURST_BYTES];
    SyncType sync;
};

static size_t makeTransmission(TxBurst *out, size_t superframes, uint8_t cc,
                               const Lc &lc)
{
    VoiceEncoder enc;
    enc.start(lc, cc);

    size_t n = 0;
    enc.header(out[n].data);
    out[n++].sync = SyncType::DATA;

    for (size_t s = 0; s < superframes; s++) {
        for (size_t v = 0; v < SUPERFRAME_BURSTS; v++) {
            uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
            makeFrames(static_cast<uint8_t>(n), frames);

            enc.voice(frames, out[n].data);
            out[n++].sync = (v == 0) ? SyncType::VOICE : SyncType::NONE;
        }
    }

    enc.terminator(out[n].data);
    out[n++].sync = SyncType::DATA;

    return n;
}

TEST_CASE("DMR voice decoder follows a call", "[dmr][protocol]")
{
    TxBurst tx[32];
    Lc lc = makeLc(FLCO_GROUP);
    size_t count = makeTransmission(tx, 3, 7, lc);
    REQUIRE(count == 20);

    VoiceDecoder dec;
    dec.setColorCode(7);

    VoiceEvent ev;
    dec.process(tx[0].data, tx[0].sync, ev);
    REQUIRE(ev.callStart);
    REQUIRE(ev.lcValid);
    REQUIRE(ev.lc.src == lc.src);
    REQUIRE(ev.lc.dst == lc.dst);
    REQUIRE(ev.voice == false);
    REQUIRE(dec.inCall());

    // A repeated header does not start a new call
    dec.process(tx[0].data, tx[0].sync, ev);
    REQUIRE(ev.callStart == false);
    REQUIRE(ev.lcValid == false);
    REQUIRE(dec.inCall());

    for (size_t i = 1; i < (count - 1); i++) {
        dec.process(tx[i].data, tx[i].sync, ev);
        REQUIRE(ev.voice);
        REQUIRE(ev.callStart == false);
        REQUIRE(ev.callEnd == false);

        uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
        makeFrames(static_cast<uint8_t>(i), frames);
        REQUIRE(memcmp(ev.frames, frames, sizeof(frames)) == 0);
    }

    dec.process(tx[count - 1].data, tx[count - 1].sync, ev);
    REQUIRE(ev.callEnd);
    REQUIRE(dec.inCall() == false);
}

TEST_CASE("DMR voice decoder joins a call late", "[dmr][protocol]")
{
    TxBurst tx[32];
    Lc lc = makeLc(FLCO_USER_USER);
    size_t count = makeTransmission(tx, 3, 2, lc);

    VoiceDecoder dec;
    dec.setColorCode(2);

    // Header and burst A of the first superframe missed: the call starts
    // at the next burst A, then burst B gives the colour code.
    VoiceEvent ev;
    size_t i = 2;
    for (; i < 7; i++) {
        dec.process(tx[i].data, tx[i].sync, ev);
        REQUIRE(ev.voice == false);
    }

    // Burst A of the second superframe: no colour code yet
    dec.process(tx[7].data, tx[7].sync, ev);
    REQUIRE(ev.voice == false);

    // Burst B: colour code matches, the call starts without LC
    dec.process(tx[8].data, tx[8].sync, ev);
    REQUIRE(ev.callStart);
    REQUIRE(ev.lcValid == false);
    REQUIRE(ev.voice);

    // Bursts C, D: no LC yet. Burst E completes the embedded LC.
    dec.process(tx[9].data, tx[9].sync, ev);
    REQUIRE(ev.lcValid == false);
    dec.process(tx[10].data, tx[10].sync, ev);
    REQUIRE(ev.lcValid == false);
    dec.process(tx[11].data, tx[11].sync, ev);
    REQUIRE(ev.lcValid);
    REQUIRE(ev.lc.flco == FLCO_USER_USER);
    REQUIRE(ev.lc.src == lc.src);
    REQUIRE(ev.lc.dst == lc.dst);

    // The LC is reported once
    for (i = 12; i < (count - 1); i++) {
        dec.process(tx[i].data, tx[i].sync, ev);
        REQUIRE(ev.voice);
        REQUIRE(ev.lcValid == false);
    }

    dec.process(tx[count - 1].data, tx[count - 1].sync, ev);
    REQUIRE(ev.callEnd);
}

TEST_CASE("DMR voice decoder ignores other colour codes", "[dmr][protocol]")
{
    TxBurst tx[32];
    size_t count = makeTransmission(tx, 2, 5, makeLc(FLCO_GROUP));

    VoiceDecoder dec;
    dec.setColorCode(6);

    for (size_t i = 0; i < count; i++) {
        VoiceEvent ev;
        dec.process(tx[i].data, tx[i].sync, ev);
        REQUIRE(ev.callStart == false);
        REQUIRE(ev.voice == false);
        REQUIRE(ev.callEnd == false);
    }

    REQUIRE(dec.inCall() == false);
}

TEST_CASE("DMR voice decoder survives bit errors", "[dmr][protocol]")
{
    TxBurst tx[32];
    Lc lc = makeLc(FLCO_GROUP);
    size_t count = makeTransmission(tx, 2, 1, lc);

    // Errors in the slot type, full LC, EMB and embedded signalling
    flipBit(tx[0].data, 3);
    flipBit(tx[0].data, 100);
    flipBit(tx[0].data, 200);
    for (size_t i = 2; i < 7; i++) {
        flipBit(tx[i].data, 109);
        flipBit(tx[i].data, 130);
    }

    VoiceDecoder dec;
    dec.setColorCode(1);

    VoiceEvent ev;
    dec.process(tx[0].data, tx[0].sync, ev);
    REQUIRE(ev.callStart);
    REQUIRE(ev.lcValid);
    REQUIRE(ev.lc.src == lc.src);

    for (size_t i = 1; i < (count - 1); i++) {
        dec.process(tx[i].data, tx[i].sync, ev);
        REQUIRE(ev.voice);
    }

    dec.process(tx[count - 1].data, tx[count - 1].sync, ev);
    REQUIRE(ev.callEnd);
}

TEST_CASE("DMR voice decoder recovers a missed voice sync", "[dmr][protocol]")
{
    TxBurst tx[32];
    size_t count = makeTransmission(tx, 3, 1, makeLc(FLCO_GROUP));

    // Burst A of the second superframe reported without sync
    tx[7].sync = SyncType::NONE;

    VoiceDecoder dec;
    dec.setColorCode(1);

    for (size_t i = 0; i < count; i++) {
        VoiceEvent ev;
        dec.process(tx[i].data, tx[i].sync, ev);

        if ((i > 0) && (i < (count - 1)))
            REQUIRE(ev.voice);
    }
}
