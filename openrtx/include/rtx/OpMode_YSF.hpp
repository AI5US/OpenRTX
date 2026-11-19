/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPMODE_YSF_H
#define OPMODE_YSF_H

#include "protocols/DMR/Modulator.hpp"
#include "protocols/YSF/Demodulator.hpp"
#include "protocols/YSF/Voice.hpp"
#include "core/audio_path.h"
#include "OpMode.hpp"

/**
 * Yaesu System Fusion (C4FM) V/D mode 2 (DN) voice: 4FSK modem on the radio
 * baseband path, software AMBE+2 vocoder through the codec thread.
 *
 * Transmission: a header, a communications frame per 100 ms of speech and a
 * terminator, from the radio callsign to "ALL" in DG-ID 0. YSF transmissions
 * are continuous, through repeaters as in simplex.
 * Reception: V/D mode 2 transmissions are played back, starting from the
 * header or, joining late, from the next communications frame.
 */
class OpMode_YSF : public OpMode
{
public:
    /**
     * Constructor.
     */
    OpMode_YSF();

    /**
     * Destructor.
     */
    ~OpMode_YSF();

    virtual void enable() override;
    virtual void disable() override;
    virtual void update(rtxStatus_t *const status, const bool newCfg) override;

    virtual opmode getID() override
    {
        return OPMODE_YSF;
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
    void rxEvent(const YSF::Event &ev, rtxStatus_t *const status);

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
     * Deviation of a symbol unit: +/-700 Hz and +/-2100 Hz. Nominal is
     * 900 Hz (MMDVM YSFTX.cpp levels, against DMR's), but the baseband
     * scaled like M17 tops out at about 3600 Hz and the filter overshoot
     * of a +/-2700 Hz signal clips 3% of its samples, 0.06% at 700 Hz.
     * Receivers follow the levels of the sync: MMDVM even offers half the
     * nominal deviation for YSF.
     */
    static constexpr uint16_t DEVIATION = 700;

    /// Preamble: alternating +3 and -3 symbols (MMDVM YSF_START_SYNC), 100 ms
    static constexpr uint8_t PREAMBLE_BYTE = 0x77;
    static constexpr size_t PREAMBLE_BYTES = 120;

    /// Encoded frames queued before the first communications frame: five
    /// for the frame, two to spare
    static constexpr uint8_t TX_PREFILL_FRAMES = 7;

    /// Most headers sent while waiting for the frames, 500 ms
    static constexpr size_t TX_PREFILL_MAX_HEADERS = 5;

    /// Consecutive released PTT readings, one per frame, ending a
    /// transmission
    static constexpr uint8_t PTT_RELEASE_READS = 2;

    YSF::VoiceEncoder voiceEnc; ///< Transmission frames
    YSF::VoiceDecoder voiceDec; ///< Reception frames
    DMR::Modulator modulator;   ///< 4FSK modulator, shared with DMR
    YSF::Demodulator demodulator;
    pathId rxAudioPath;         ///< Speaker path for received speech
    pathId txAudioPath;         ///< Microphone path for transmitted speech
    bool startRx;               ///< Start reception
    bool startTx;               ///< Start transmission
    bool invertTxPhase;         ///< TX signal phase inversion
    bool invertRxPhase;         ///< RX signal phase inversion
    uint8_t pttReleases;        ///< Consecutive released PTT readings
};

#endif /* OPMODE_YSF_H */
