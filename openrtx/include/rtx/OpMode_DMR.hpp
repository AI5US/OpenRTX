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

    /// Preamble length, 120 bytes are 100 ms
    static constexpr size_t PREAMBLE_BYTES = 120;

    /// Voice LC headers sent at the start of a transmission
    static constexpr size_t TX_HEADERS = 2;

    bool startRx;       ///< Start reception
    bool startTx;       ///< Start transmission
    bool invertTxPhase; ///< TX signal phase inversion
    bool invertRxPhase; ///< RX signal phase inversion
    pathId rxAudioPath; ///< Speaker path for received speech
    pathId txAudioPath; ///< Microphone path for transmitted speech
    DMR::Modulator modulator;
    DMR::Demodulator demodulator;
    DMR::VoiceEncoder voiceEnc;
    DMR::VoiceDecoder voiceDec;
};

#endif /* OPMODE_DMR_H */
