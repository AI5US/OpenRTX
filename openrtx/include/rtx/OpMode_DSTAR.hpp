/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPMODE_DSTAR_H
#define OPMODE_DSTAR_H

#include "protocols/DSTAR/Demodulator.hpp"
#include "protocols/DSTAR/Modulator.hpp"
#include "protocols/DSTAR/SlowData.hpp"
#include "core/audio_path.h"
#include "OpMode.hpp"

/**
 * D-STAR digital voice (DV) mode: GMSK modem on the radio baseband path,
 * software AMBE vocoder through the codec thread, slow data text message.
 *
 * Transmission: MY is the radio callsign, UR, RPT1, RPT2 and the text
 * message come from the D-STAR settings.
 * Reception: any D-STAR transmission is played back, starting from its
 * header or, joining late, from the next data sync.
 */
class OpMode_DSTAR : public OpMode
{
public:
    OpMode_DSTAR();
    ~OpMode_DSTAR();

    virtual void enable() override;
    virtual void disable() override;
    virtual void update(rtxStatus_t *const status, const bool newCfg) override;

    virtual opmode getID() override
    {
        return OPMODE_DSTAR;
    }

    virtual bool rxSquelchOpen() override
    {
        return dataValid;
    }

private:
    void offState(rtxStatus_t *const status);
    void rxState(rtxStatus_t *const status);
    void txState(rtxStatus_t *const status);

    /**
     * Handle a demodulator event while receiving.
     */
    void rxEvent(const DStar::Event &ev, rtxStatus_t *const status);

    /**
     * Open the speaker path and start the AMBE decoder, if not done yet.
     */
    void startPlayback();

    /**
     * Stop the AMBE decoder, release the speaker and clear the received
     * header and message.
     */
    void stopPlayback(rtxStatus_t *const status);

    /**
     * Copy the fields of a received header to the status.
     */
    static void setRxHeader(const DStar::Header &h, rtxStatus_t *const status);

    /// Preamble length, 60 bytes are 100 ms
    static constexpr size_t PREAMBLE_BYTES = 60;

    bool startRx;           ///< Start reception
    bool startTx;           ///< Start transmission
    bool dataValid;         ///< Receiving a D-STAR transmission
    bool invertTxPhase;     ///< TX signal phase inversion
    bool invertRxPhase;     ///< RX signal phase inversion
    uint8_t txFrameIndex;   ///< Position of the next TX frame in superframe
    pathId rxAudioPath;     ///< Speaker path for received speech
    pathId txAudioPath;     ///< Microphone path for transmitted speech
    DStar::Modulator modulator;
    DStar::Demodulator demodulator;
    DStar::SlowDataEncoder slowDataEnc;
    DStar::SlowDataDecoder slowDataDec;
};

#endif /* OPMODE_DSTAR_H */
