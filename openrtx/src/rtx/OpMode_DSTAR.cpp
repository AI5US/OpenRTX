/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "interfaces/platform.h"
#include "interfaces/audio.h"
#include "interfaces/delays.h"
#include "interfaces/radio.h"
#include "protocols/DSTAR/Constants.hpp"
#include "protocols/DSTAR/Header.hpp"
#include "protocols/DSTAR/HeaderCoder.hpp"
#include "core/audio_codec.h"
#include "core/state.h"
#include "rtx/OpMode_DSTAR.hpp"

#ifdef PLATFORM_MOD17
#include "calibration/calibInfo_Mod17.h"

extern mod17Calib_t mod17CalData;
#endif

using namespace DStar;

OpMode_DSTAR::OpMode_DSTAR()
    : startRx(false)
    , startTx(false)
    , dataValid(false)
    , invertTxPhase(false)
    , invertRxPhase(false)
    , txFrameIndex(0)
    , rxAudioPath(-1)
    , txAudioPath(-1)
{
}

OpMode_DSTAR::~OpMode_DSTAR()
{
    disable();
}

void OpMode_DSTAR::enable()
{
    codec_init();
    modulator.init();
    demodulator.init();
    slowDataDec.reset();
    dataValid = false;
    startRx = true;
    startTx = false;
}

void OpMode_DSTAR::disable()
{
    startRx = false;
    startTx = false;
    dataValid = false;
    platform_ledOff(GREEN);
    platform_ledOff(RED);

    if (rxAudioPath >= 0)
        audioPath_release(rxAudioPath);
    if (txAudioPath >= 0)
        audioPath_release(txAudioPath);
    rxAudioPath = -1;
    txAudioPath = -1;

    codec_terminate();
    radio_disableRtx();
    modulator.terminate();
    demodulator.terminate();
}

void OpMode_DSTAR::update(rtxStatus_t *const status, const bool newCfg)
{
    (void)newCfg;

    // Baseband phase inversions of M17, which give the on air polarity: they
    // depend on the radio hardware, not on the modulation.
    bool m17InvertTx = false;
    bool m17InvertRx = false;
#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    const hwInfo_t *hwinfo = platform_getHwInfo();
    m17InvertTx = true;
    m17InvertRx = (hwinfo->vhf_band == 1);
#elif defined(PLATFORM_MOD17)
    m17InvertTx = (mod17CalData.bb_tx_invert == 1);
    m17InvertRx = (mod17CalData.bb_rx_invert == 1);
#elif defined(PLATFORM_CS7000) || defined(PLATFORM_CS7000P) \
    || defined(PLATFORM_C62)
    m17InvertTx = true;
#elif defined(PLATFORM_DM1701)
    m17InvertTx = true;
    m17InvertRx = true;
#endif

    // The modem follows MMDVM's convention, a bit 1 being a negative level.
    // MMDVM's levels are the opposite of the on air polarity (its DMR
    // levels send the -3 symbol as a positive level, and MMDVM hardware
    // inverts all modes alike): on air, a D-STAR bit 1 is a positive
    // deviation, so D-STAR inverts the M17 phases.
    invertTxPhase = !m17InvertTx;
    invertRxPhase = !m17InvertRx;

    switch (status->opStatus) {
        case OFF:
            offState(status);
            break;

        case RX:
            rxState(status);
            break;

        case TX:
            txState(status);
            break;

        default:
            break;
    }

    switch (status->opStatus) {
        case RX:
            if (dataValid)
                platform_ledOn(GREEN);
            else
                platform_ledOff(GREEN);
            break;

        case TX:
            platform_ledOff(GREEN);
            platform_ledOn(RED);
            break;

        default:
            platform_ledOff(GREEN);
            platform_ledOff(RED);
            break;
    }
}

void OpMode_DSTAR::offState(rtxStatus_t *const status)
{
    radio_disableRtx();

    if (txAudioPath >= 0) {
        codec_stop(txAudioPath);
        audioPath_release(txAudioPath);
        txAudioPath = -1;
    }

    if (startRx) {
        status->opStatus = RX;
        return;
    }

    if (platform_getPttStatus() && (status->txDisable == 0)) {
        startTx = true;
        status->opStatus = TX;
        return;
    }

    // Nothing else to do: sleep, not to lock up the other threads
    sleepFor(0, 30);
}

void OpMode_DSTAR::rxState(rtxStatus_t *const status)
{
    if (startRx) {
        demodulator.startBasebandSampling();
        radio_enableRx();
        startRx = false;
    }

    demodulator.update(invertRxPhase);

    Event ev;
    while (demodulator.popEvent(ev))
        rxEvent(ev, status);

    if (platform_getPttStatus()) {
        demodulator.stopBasebandSampling();
        stopPlayback(status);
        status->opStatus = OFF;
    }
}

void OpMode_DSTAR::rxEvent(const Event &ev, rtxStatus_t *const status)
{
    switch (ev.type) {
        case Event::HEADER: {
            // New transmission: forget slow data of the previous one
            slowDataDec.reset();

            if (ev.headerValid == false)
                break;

            Header h;
            h.fromBytes(ev.header);
            setRxHeader(h, status);
            dataValid = true;
            startPlayback();
            break;
        }

        case Event::FRAME: {
            // Also joins a transmission whose header was missed
            dataValid = true;
            startPlayback();

            if (ev.frameIndex == 0) {
                slowDataDec.sync();
            } else {
                slowDataDec.push(&ev.frame[VOICE_SIZE]);

                if (slowDataDec.hasMessage()) {
                    strncpy(status->DSTAR_message, slowDataDec.getMessage(),
                            sizeof(status->DSTAR_message) - 1);
                    status->DSTAR_message[sizeof(status->DSTAR_message) - 1] =
                        '\0';
                }

                // Header repeated in the slow data, for late entry
                Header h;
                if ((status->dstarHeaderOk == false)
                    && slowDataDec.getHeader(h))
                    setRxHeader(h, status);
            }

            if (codec_running())
                codec_pushFrame(ev.frame, false);
            break;
        }

        case Event::END:
        case Event::LOST:
            stopPlayback(status);
            break;

        default:
            break;
    }
}

void OpMode_DSTAR::startPlayback()
{
    if ((rxAudioPath < 0)
        || (audioPath_getStatus(rxAudioPath) == PATH_CLOSED)) {
        rxAudioPath = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_RX);
    }

    if (audioPath_getStatus(rxAudioPath) != PATH_OPEN)
        return;

    if (codec_running() == false)
        codec_startDecodeType(rxAudioPath, CODEC_DSTAR_AMBE);
}

void OpMode_DSTAR::stopPlayback(rtxStatus_t *const status)
{
    dataValid = false;

    if (rxAudioPath >= 0) {
        codec_stop(rxAudioPath);
        audioPath_release(rxAudioPath);
        rxAudioPath = -1;
    }

    slowDataDec.reset();
    status->dstarHeaderOk = false;
    status->DSTAR_my[0] = '\0';
    status->DSTAR_suffix[0] = '\0';
    status->DSTAR_ur[0] = '\0';
    status->DSTAR_rpt1[0] = '\0';
    status->DSTAR_rpt2[0] = '\0';
    status->DSTAR_message[0] = '\0';
}

void OpMode_DSTAR::setRxHeader(const Header &h, rtxStatus_t *const status)
{
    h.getMy(status->DSTAR_my);
    h.getSuffix(status->DSTAR_suffix);
    h.getUr(status->DSTAR_ur);
    h.getRpt1(status->DSTAR_rpt1);
    h.getRpt2(status->DSTAR_rpt2);
    status->dstarHeaderOk = true;
}

void OpMode_DSTAR::txState(rtxStatus_t *const status)
{
    if (startTx) {
        startTx = false;

        // Radio header: simplex unless a repeater is set
        Header h;
        h.setMy(status->source_address);
        h.setUr(state.settings.dstar_ur);
        h.setRpt1(state.settings.dstar_rpt1);
        h.setRpt2(state.settings.dstar_rpt2);
        if (strncmp(state.settings.dstar_rpt1, "DIRECT", 6) != 0)
            h.setFlag(0, FLAG_REPEATER);

        uint8_t header[HEADER_SIZE];
        uint8_t coded[HEADER_FEC_SIZE];
        h.toBytes(header);
        encodeHeader(header, coded);

        slowDataEnc.reset();
        slowDataEnc.setMessage(state.settings.dstar_message);
        slowDataEnc.setHeader(h);

        txAudioPath = audioPath_request(SOURCE_MIC, SINK_MCU, PRIO_TX);
        codec_startEncodeType(txAudioPath, CODEC_DSTAR_AMBE);
        radio_enableTx();

        modulator.invertPhase(invertTxPhase);
        modulator.start();
        modulator.sendPreamble(PREAMBLE_BYTES);
        modulator.send(FRAME_SYNC, FRAME_SYNC_BITS);
        modulator.send(coded, HEADER_FEC_BITS);
        txFrameIndex = 0;
    }

    // Wait for 20 ms of encoded speech. If the encoder is not running (e.g.
    // the microphone path could not be opened) send silence instead.
    uint8_t frame[FRAME_SIZE];
    if (codec_popFrame(frame, true) < 0) {
        memcpy(frame, NULL_AMBE, VOICE_SIZE);
        sleepFor(0, 20);
    }

    // Slow data: data sync at the start of each superframe
    if (txFrameIndex == 0)
        memcpy(&frame[VOICE_SIZE], DATA_SYNC, SLOW_DATA_SIZE);
    else
        slowDataEnc.next(&frame[VOICE_SIZE]);

    modulator.send(frame, FRAME_BITS);
    txFrameIndex = (txFrameIndex + 1) % SUPERFRAME_FRAMES;

    if (platform_getPttStatus() == false) {
        modulator.send(END_SYNC, END_SYNC_BITS);
        modulator.stop();
        startRx = true;
        status->opStatus = OFF;
    }
}
