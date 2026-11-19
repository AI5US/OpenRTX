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
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Lc.hpp"
#include "core/audio_codec.h"
#include "rtx/OpMode_DMR.hpp"

#ifdef PLATFORM_MOD17
#include "calibration/calibInfo_Mod17.h"

extern mod17Calib_t mod17CalData;
#endif

using namespace DMR;

OpMode_DMR::OpMode_DMR()
    : startRx(false)
    , startTx(false)
    , invertTxPhase(false)
    , invertRxPhase(false)
    , rxAudioPath(-1)
    , txAudioPath(-1)
{
}

OpMode_DMR::~OpMode_DMR()
{
    disable();
}

void OpMode_DMR::enable()
{
    codec_init();
    modulator.init();
    demodulator.init();
    voiceDec.reset();
    startRx = true;
    startTx = false;
}

void OpMode_DMR::disable()
{
    startRx = false;
    startTx = false;
    voiceDec.reset();
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

void OpMode_DMR::update(rtxStatus_t *const status, const bool newCfg)
{
    (void)newCfg;

    // Same baseband phase inversions as M17: they depend on the radio
    // hardware, not on the modulation.
#if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    const hwInfo_t *hwinfo = platform_getHwInfo();
    invertTxPhase = true;
    invertRxPhase = (hwinfo->vhf_band == 1);
#elif defined(PLATFORM_MOD17)
    invertTxPhase = (mod17CalData.bb_tx_invert == 1);
    invertRxPhase = (mod17CalData.bb_rx_invert == 1);
#elif defined(PLATFORM_CS7000) || defined(PLATFORM_CS7000P) \
    || defined(PLATFORM_C62)
    invertTxPhase = true;
#elif defined(PLATFORM_DM1701)
    invertTxPhase = true;
    invertRxPhase = true;
#endif

    voiceDec.setColorCode(status->dmrRxColorCode);

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
            if (voiceDec.inCall())
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

void OpMode_DMR::offState(rtxStatus_t *const status)
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

void OpMode_DMR::rxState(rtxStatus_t *const status)
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
        voiceDec.reset();
        stopPlayback(status);
        status->opStatus = OFF;
    }
}

void OpMode_DMR::rxEvent(const Event &ev, rtxStatus_t *const status)
{
    if (ev.type == Event::LOST) {
        voiceDec.reset();
        stopPlayback(status);
        return;
    }

    VoiceEvent vev;
    voiceDec.process(ev.burst, ev.sync, vev);

    if (vev.callStart) {
        // New call: forget the link control of the previous one
        status->dmrLcOk = false;
        status->DMR_src = 0;
        status->DMR_dst = 0;
        startPlayback();
    }

    if (vev.lcValid) {
        status->dmrLcOk = true;
        status->DMR_src = vev.lc.src;
        status->DMR_dst = vev.lc.dst;
        status->DMR_group = vev.lc.isGroup();
    }

    if (vev.voice) {
        startPlayback();

        if (codec_running()) {
            for (size_t i = 0; i < VOICE_FRAMES; i++)
                codec_pushFrame(vev.frames[i], false);
        }
    }

    if (vev.callEnd) {
        stopPlayback(status);

        // The next call may come with another timing
        demodulator.unlock();
    }
}

void OpMode_DMR::startPlayback()
{
    if ((rxAudioPath < 0)
        || (audioPath_getStatus(rxAudioPath) == PATH_CLOSED)) {
        rxAudioPath = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_RX);
    }

    if (audioPath_getStatus(rxAudioPath) != PATH_OPEN)
        return;

    if (codec_running() == false)
        codec_startDecodeType(rxAudioPath, CODEC_DMR_AMBE);
}

void OpMode_DMR::stopPlayback(rtxStatus_t *const status)
{
    if (rxAudioPath >= 0) {
        codec_stop(rxAudioPath);
        audioPath_release(rxAudioPath);
        rxAudioPath = -1;
    }

    status->dmrLcOk = false;
    status->DMR_src = 0;
    status->DMR_dst = 0;
}

void OpMode_DMR::txState(rtxStatus_t *const status)
{
    uint8_t burst[BURST_BYTES];

    if (startTx) {
        startTx = false;

        Lc lc = {};
        lc.flco = status->dmrPrivateCall ? FLCO_USER_USER : FLCO_GROUP;
        lc.src = status->dmrSrcId & 0xFFFFFF;
        lc.dst = status->dmrDstId & 0xFFFFFF;
        voiceEnc.start(lc, status->dmrTxColorCode);

        txAudioPath = audioPath_request(SOURCE_MIC, SINK_MCU, PRIO_TX);
        codec_startEncodeType(txAudioPath, CODEC_DMR_AMBE);
        radio_enableTx();

        modulator.invertPhase(invertTxPhase);
        modulator.start();
        modulator.sendPreamble(PREAMBLE_BYTES);

        voiceEnc.header(burst);
        for (size_t i = 0; i < TX_HEADERS; i++)
            modulator.sendFrame(burst);

        // A voice burst needs three frames of speech, and only one block of
        // baseband is buffered while waiting for them: should a frame come
        // late, the output stream would replay a block and shift all the
        // following bursts. Keep sending headers, which also keeps the
        // output going, until frames to spare are queued.
        for (size_t i = 0; i < TX_PREFILL_MAX_HEADERS; i++) {
            if ((codec_running() == false)
                || (codec_queuedFrames() >= TX_PREFILL_FRAMES))
                break;

            modulator.sendFrame(burst);
        }
    }

    // Wait for 60 ms of encoded speech. If the encoder is not running (e.g.
    // the microphone path could not be opened) send silence instead.
    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
    for (size_t i = 0; i < VOICE_FRAMES; i++) {
        if (codec_popFrame(frames[i], true) < 0) {
            memcpy(frames[i], SILENCE_FRAME, VOICE_FRAME_BYTES);
            sleepFor(0, 20);
        }
    }

    voiceEnc.voice(frames, burst);
    modulator.sendFrame(burst);

    if (platform_getPttStatus() == false) {
        voiceEnc.terminator(burst);
        modulator.sendFrame(burst);
        modulator.stop();
        startRx = true;
        status->opStatus = OFF;
    }
}
