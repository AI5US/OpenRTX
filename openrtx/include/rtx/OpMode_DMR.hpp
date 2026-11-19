/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPMODE_DMR_H
#define OPMODE_DMR_H

#include "protocols/DMR/Demodulator.hpp"
#include "protocols/DMR/Modulator.hpp"
#include "protocols/DMR/Voice.hpp"
#include "core/audio_path.h"
#include "OpMode.hpp"

/**
 * DMR direct mode (DMO) voice: 4FSK modem on the radio baseband path,
 * software AMBE+2 vocoder through the codec thread.
 *
 * Transmission: a continuous direct mode transmission (one timeslot used,
 * the other one filled with the idle pattern) of voice LC headers, voice
 * superframes and a terminator, from the radio DMR ID to the destination
 * and with the colour code of the rtx status.
 * Reception: any voice call with the receive colour code is played back,
 * starting from its header or, joining late, from the next voice burst.
 *
 * Repeater mode, when the transmit and receive frequencies differ: only the
 * selected timeslot of the downlink is played back, and transmissions go in
 * the same timeslot of the uplink, timed from the last downlink burst. With
 * no recent downlink, a BS activation CSBK wakes the repeater up first.
 * Between bursts the power amplifier is switched off on the targets that
 * can (CONFIG_RADIO_PA_KEYING), the others send unmodulated carrier.
 */
class OpMode_DMR : public OpMode
{
public:
    OpMode_DMR();
    ~OpMode_DMR();

    virtual void enable() override;
    virtual void disable() override;
    virtual void update(rtxStatus_t *const status, const bool newCfg) override;

    virtual opmode getID() override
    {
        return OPMODE_DMR;
    }

    virtual bool rxSquelchOpen() override
    {
        return voiceDec.inCall();
    }

private:
    void offState(rtxStatus_t *const status);
    void rxState(rtxStatus_t *const status);
    void txState(rtxStatus_t *const status);

    /**
     * Handle a receiver event while receiving.
     */
    void rxEvent(const DMR::Event &ev, rtxStatus_t *const status);

    /**
     * Open the speaker path and start the AMBE+2 decoder, if not done yet.
     */
    void startPlayback();

    /**
     * Stop the AMBE+2 decoder, release the speaker and clear the received
     * call information.
     */
    void stopPlayback(rtxStatus_t *const status);

    /**
     * Follow the clock of the receiver: estimate, in units of receiver
     * samples, the offset between the system tick and the sample count.
     */
    void trackRxClock();

    /**
     * Follow the TDMA timing of a repeater downlink from a received burst.
     */
    void trackDownlink(const DMR::Event &ev, rtxStatus_t *const status);

    /**
     * @return true if a repeater downlink was received recently enough to
     * time a transmission on it.
     */
    bool downlinkKnown() const;

    /**
     * Wake up an idle repeater: transmit BS activation CSBKs, then return to
     * reception to wait for the downlink.
     */
    void sendWakeup(rtxStatus_t *const status);

    /**
     * Repeater mode: send carrier up to the start of the next uplink burst
     * of the selected timeslot.
     */
    void alignToSlot(rtxStatus_t *const status, long long startTick);

    /**
     * Send a burst: in direct mode followed by the idle fill, in repeater
     * mode by carrier up to the next burst of the same timeslot.
     */
    void sendBurst(const uint8_t *burst);

    /**
     * Modulator block callback, see Modulator::BlockCallback.
     */
    static void blockPlaying(void *ctx, uint32_t sample, long long tick);

    /**
     * Repeater mode: switch the power amplifier on and off around the
     * bursts that reach the radio output while a block plays.
     *
     * @param sample: modulator sample index of the block now playing.
     * @param tick: system tick when the block started playing.
     */
    void keyPa(uint32_t sample, long long tick);

    /// Preamble length, 120 bytes are 100 ms
    static constexpr size_t PREAMBLE_BYTES = 120;

    /// Voice LC headers sent at the start of a transmission
    static constexpr size_t TX_HEADERS = 2;

    /// Encoded frames queued before the first voice burst: three for the
    /// burst, two to spare
    static constexpr uint8_t TX_PREFILL_FRAMES = 5;

    /// Most headers sent while waiting for the frames, 600 ms
    static constexpr size_t TX_PREFILL_MAX_HEADERS = 10;

    /// Receiver samples per system tick (1 ms)
    static constexpr long long TICK_SAMPLES = DMR::Receiver::SAMPLE_RATE / 1000;

    /// Uplink burst start after the end of the downlink burst of the same
    /// timeslot: the inbound timeslot starts there (MMDVM DMRTX.cpp
    /// createCACH) and its burst after a 1.25 ms guard time
    static constexpr long long TX_GUARD_SYMBOLS = 6;

    /// Output stream latency: one block of silence, 20 ms, plays before the
    /// first block filled
    static constexpr long long TX_STREAM_DELAY_MS = 20;

    /// Power amplifier switched on before and off after a burst, about
    /// 1 ms: the switching follows the system tick
    static constexpr uint32_t PA_MARGIN_SYMBOLS = 5;

    /// Consecutive PTT readings, one per burst, needed to end a
    /// transmission: switching the PA can glitch the PTT input
    static constexpr uint8_t PTT_RELEASE_READS = 2;

    /// Age of the last downlink burst still good to time a transmission
    static constexpr long long DOWNLINK_TIMEOUT_MS = 1000;

    /// Time to wait for the downlink after a wake-up
    static constexpr long long WAKEUP_WAIT_MS = 500;

    /// Wake-up attempts before giving up, until the PTT is released
    static constexpr uint8_t WAKEUP_TRIES = 3;

    /// BS activation CSBKs sent in a wake-up
    static constexpr size_t WAKEUP_BURSTS = 2;

    long long rxClock;      ///< System tick minus sample count, in samples
    long long downlinkEnd;  ///< End of a downlink burst of the timeslot
    long long downlinkTick; ///< System tick of the last downlink burst
    long long wakeupTick;   ///< System tick of the last wake-up
    uint32_t paEdge;        ///< Modulator sample of the next PA switching
    uint32_t paBurst;       ///< Burst of the next PA switching
    uint32_t txBursts;      ///< Bursts sent in the transmission
    uint32_t rxClockCount;  ///< Sample count of the last rxClock update
    bool rxClockValid;      ///< rxClock estimated since sampling started
    bool repeater;          ///< Repeater mode
    uint8_t wakeups;        ///< Wake-ups sent for the current PTT press
    uint8_t pttReleases;    ///< Consecutive PTT readings found released
    bool keying;            ///< Switching the PA around the bursts
    bool paNextOn;          ///< Next PA switching is on
    bool startRx;           ///< Start reception
    bool startTx;           ///< Start transmission
    bool invertTxPhase;     ///< TX signal phase inversion
    bool invertRxPhase;     ///< RX signal phase inversion
    pathId rxAudioPath;     ///< Speaker path for received speech
    pathId txAudioPath;     ///< Microphone path for transmitted speech
    DMR::Modulator modulator;
    DMR::Demodulator demodulator;
    DMR::VoiceEncoder voiceEnc;
    DMR::VoiceDecoder voiceDec;
};

#endif /* OPMODE_DMR_H */
