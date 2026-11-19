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
#include "protocols/DMR/Csbk.hpp"
#include "protocols/DMR/Lc.hpp"
#include "core/audio_codec.h"
#include "rtx/OpMode_DMR.hpp"

#ifdef PLATFORM_MOD17
#include "calibration/calibInfo_Mod17.h"

extern mod17Calib_t mod17CalData;
#endif

using namespace DMR;

/*
 * Delay from the baseband output stream to the radio output, beyond the
 * block of silence the stream starts with, in microseconds.
 */
#ifndef CONFIG_BASEBAND_TX_DELAY_US
#define CONFIG_BASEBAND_TX_DELAY_US 0
#endif

OpMode_DMR::OpMode_DMR()
    : rxClock(0)
    , downlinkEnd(0)
    , downlinkTick(0)
    , wakeupTick(0)
    , paEdge(0)
    , paBurst(0)
    , txBursts(0)
    , rxClockCount(0)
    , rxClockValid(false)
    , repeater(false)
    , wakeups(0)
    , pttReleases(0)
    , keying(false)
    , paNextOn(false)
    , startRx(false)
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
    modulator.setBlockCallback(&OpMode_DMR::blockPlaying, this);
    demodulator.init();
    voiceDec.reset();
    repeater = false;
    demodulator.setRepeater(false);
    downlinkTick = 0;
    wakeups = 0;
    startRx = true;
    startTx = false;
}

void OpMode_DMR::disable()
{
    startRx = false;
    startTx = false;
    keying = false;
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

    bool split = (status->txFrequency != status->rxFrequency);
    if (split != repeater) {
        repeater = split;
        demodulator.setRepeater(repeater);
        voiceDec.reset();
        downlinkTick = 0;
    }

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
        rxClockValid = false;
        startRx = false;
    }

    demodulator.update(invertRxPhase);
    trackRxClock();

    Event ev;
    while (demodulator.popEvent(ev))
        rxEvent(ev, status);

    if (platform_getPttStatus() == false) {
        wakeups = 0;
        return;
    }

    // A repeater transmission is timed on the downlink: wake the repeater
    // up and wait for it, giving up after a few attempts
    if (repeater && (downlinkKnown() == false)) {
        bool waited = (getTick() - wakeupTick) >= WAKEUP_WAIT_MS;
        if ((status->txDisable == 0) && (wakeups < WAKEUP_TRIES)
            && ((wakeups == 0) || waited))
            sendWakeup(status);

        return;
    }

    demodulator.stopBasebandSampling();
    voiceDec.reset();
    stopPlayback(status);
    status->opStatus = OFF;
}

void OpMode_DMR::rxEvent(const Event &ev, rtxStatus_t *const status)
{
    if (ev.type == Event::LOST) {
        voiceDec.reset();
        stopPlayback(status);
        return;
    }

    if (repeater) {
        trackDownlink(ev, status);
        if (ev.slot != status->dmrSlot)
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

        // The next direct mode call may come with another timing, while a
        // repeater keeps its own
        if (repeater == false)
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

void OpMode_DMR::trackRxClock()
{
    uint32_t count = demodulator.sampleCount();
    if (rxClockValid && (count == rxClockCount))
        return;

    // The lowest offset is the one with the shortest input latency. Let it
    // rise slowly, to follow the drift between the two clocks.
    long long offset = (getTick() * TICK_SAMPLES) - count;
    if ((rxClockValid == false) || (offset < rxClock))
        rxClock = offset;
    else
        rxClock++;

    rxClockCount = count;
    rxClockValid = true;
}

void OpMode_DMR::trackDownlink(const Event &ev, rtxStatus_t *const status)
{
    if ((ev.slot < 0) || (rxClockValid == false))
        return;

    // Bursts of the other timeslot end 30 ms before those of ours
    long long end = rxClock + ev.sampleIndex;
    if (ev.slot != status->dmrSlot)
        end += Receiver::SLOT_SAMPLES;

    downlinkEnd = end;
    downlinkTick = getTick();
}

bool OpMode_DMR::downlinkKnown() const
{
    return (downlinkTick != 0)
        && ((getTick() - downlinkTick) < DOWNLINK_TIMEOUT_MS);
}

void OpMode_DMR::sendWakeup(rtxStatus_t *const status)
{
    uint8_t burst[BURST_BYTES];
    encodeBsDwnAct(ALL_ID, status->dmrSrcId & ALL_ID, status->dmrTxColorCode,
                   burst);

    demodulator.stopBasebandSampling();
    radio_disableRtx();
    radio_enableTx();

    modulator.invertPhase(invertTxPhase);
    modulator.start();
    for (size_t i = 0; i < WAKEUP_BURSTS; i++)
        modulator.sendSlot(burst);
    modulator.stop();

    radio_disableRtx();
    wakeups++;
    wakeupTick = getTick();
    startRx = true;
}

void OpMode_DMR::alignToSlot(rtxStatus_t *const status, long long startTick)
{
    static constexpr long long SPS = Receiver::SAMPLES_PER_SYMBOL;
    static constexpr long long FRAME = FRAME_SYMBOLS * SPS;

    static constexpr long long TX_DELAY = CONFIG_BASEBAND_TX_DELAY_US
                                        * TICK_SAMPLES / 1000;

    // The output stream plays a block of silence before the first block
    // filled, see Modulator::start()
    long long start = ((startTick + TX_STREAM_DELAY_MS) * TICK_SAMPLES)
                    + TX_DELAY;
    long long advance = status->dmrTxTiming;
    long long target = downlinkEnd + ((TX_GUARD_SYMBOLS - advance) * SPS);

    long long lead = (target - start) % FRAME;
    if (lead < 0)
        lead += FRAME;

    // Leave time to switch the PA on before the first burst
    size_t symbols = static_cast<size_t>(lead / SPS);
    if (symbols < PA_MARGIN_SYMBOLS)
        symbols += FRAME_SYMBOLS;

#ifdef CONFIG_RADIO_PA_KEYING
    static constexpr uint32_t TX_SPS = FskShaper::SAMPLES_PER_SYMBOL;
    paEdge = (symbols - PA_MARGIN_SYMBOLS) * TX_SPS;
    paBurst = 0;
    paNextOn = true;
    txBursts = 0;
    keying = true;
#endif

    modulator.sendCarrier(symbols);
}

void OpMode_DMR::sendBurst(const uint8_t *burst)
{
    if (repeater == false) {
        modulator.sendFrame(burst);
        return;
    }

    txBursts++;
    modulator.sendSlot(burst);
}

void OpMode_DMR::blockPlaying(void *ctx, uint32_t sample, long long tick)
{
    static_cast<OpMode_DMR *>(ctx)->keyPa(sample, tick);
}

void OpMode_DMR::keyPa(uint32_t sample, long long tick)
{
#ifdef CONFIG_RADIO_PA_KEYING
    static constexpr uint32_t SPS = FskShaper::SAMPLES_PER_SYMBOL;
    static constexpr uint32_t RATE = FskShaper::SAMPLE_RATE;
    static constexpr uint32_t MARGIN = 2 * PA_MARGIN_SYMBOLS * SPS;
    static constexpr uint32_t ON = (BURST_SYMBOLS * SPS) + MARGIN;
    static constexpr uint32_t OFF = (FRAME_SYMBOLS * SPS) - ON;
    static constexpr uint32_t DELAY = static_cast<uint64_t>(RATE)
                                    * CONFIG_BASEBAND_TX_DELAY_US / 1000000;

    if (keying == false)
        return;

    // Switch when the samples of an edge reach the radio output
    uint32_t end = sample + Modulator::BLOCK_SAMPLES;
    while ((paEdge + DELAY) < end) {
        // Not before the burst has been sent
        if (paNextOn && (paBurst >= txBursts))
            break;

        uint32_t at = paEdge + DELAY;
        if (at > sample)
            sleepUntil(tick + ((((at - sample) * 1000) + (RATE / 2)) / RATE));

        radio_setPa(paNextOn);
        if (paNextOn) {
            paEdge += ON;
        } else {
            paEdge += OFF;
            paBurst++;
        }

        paNextOn = !paNextOn;
    }
#else
    (void)sample;
    (void)tick;
#endif
}

void OpMode_DMR::txState(rtxStatus_t *const status)
{
    uint8_t burst[BURST_BYTES];

    if (startTx) {
        startTx = false;
        pttReleases = 0;

        Lc lc = {};
        lc.flco = status->dmrPrivateCall ? FLCO_USER_USER : FLCO_GROUP;
        lc.src = status->dmrSrcId & 0xFFFFFF;
        lc.dst = status->dmrDstId & 0xFFFFFF;
        voiceEnc.start(lc, status->dmrTxColorCode);

        txAudioPath = audioPath_request(SOURCE_MIC, SINK_MCU, PRIO_TX);
        codec_startEncodeType(txAudioPath, CODEC_DMR_AMBE);
        radio_enableTx();

#ifdef CONFIG_RADIO_PA_KEYING
        // Repeater mode: on only for the bursts, see keyPa()
        if (repeater)
            radio_setPa(false);
#endif

        modulator.invertPhase(invertTxPhase);
        modulator.start();
        if (repeater)
            alignToSlot(status, getTick());
        else
            modulator.sendPreamble(PREAMBLE_BYTES);

        voiceEnc.header(burst);
        for (size_t i = 0; i < TX_HEADERS; i++)
            sendBurst(burst);

        // A voice burst needs three frames of speech, and only one block of
        // baseband is buffered while waiting for them: should a frame come
        // late, the output stream would replay a block and shift all the
        // following bursts. Keep sending headers, which also keeps the
        // output going, until frames to spare are queued.
        for (size_t i = 0; i < TX_PREFILL_MAX_HEADERS; i++) {
            if ((codec_running() == false)
                || (codec_queuedFrames() >= TX_PREFILL_FRAMES))
                break;

            sendBurst(burst);
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
    sendBurst(burst);

    if (platform_getPttStatus())
        pttReleases = 0;
    else
        pttReleases++;

    if (pttReleases >= PTT_RELEASE_READS) {
        voiceEnc.terminator(burst);
        sendBurst(burst);
        modulator.stop();
        keying = false;
        startRx = true;
        status->opStatus = OFF;
    }
}
