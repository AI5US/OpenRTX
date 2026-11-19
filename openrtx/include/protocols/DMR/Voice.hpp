/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_VOICE_H
#define DMR_VOICE_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/DMR/Constants.hpp"
#include "protocols/DMR/Lc.hpp"

namespace DMR
{

/**
 * Type of sync found by the receiver in a burst.
 */
enum class SyncType : uint8_t {
    NONE,  ///< No sync: voice burst B to F, or unknown
    VOICE, ///< Voice sync: voice burst A
    DATA   ///< Data sync: data burst, slot type follows
};

/**
 * Builds the bursts of a direct mode voice transmission: voice LC headers,
 * superframes of voice bursts A to F and the terminator with LC.
 */
class VoiceEncoder
{
public:
    VoiceEncoder();

    /**
     * \brief Start a new transmission.
     *
     * @param lc: link control of the call.
     * @param colorCode: colour code, 0 to 15.
     * @param bsSourced: use the base station syncs, as a repeater does,
     * instead of the mobile station ones.
     */
    void start(const Lc &lc, uint8_t colorCode, bool bsSourced = false);

    /**
     * \brief Build a voice LC header burst.
     *
     * @param burst: output, BURST_BYTES bytes.
     */
    void header(uint8_t *burst) const;

    /**
     * \brief Build the next voice burst of the superframe: A carries the
     * voice sync, B to E the embedded link control, F a null embedded
     * signalling.
     *
     * @param frames: the three AMBE+2 frames of the burst.
     * @param burst: output, BURST_BYTES bytes.
     */
    void voice(const uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES],
               uint8_t *burst);

    /**
     * \brief Build the terminator with LC burst, ending the transmission.
     *
     * @param burst: output, BURST_BYTES bytes.
     */
    void terminator(uint8_t *burst) const;

    /**
     * \brief Position of the next voice burst in its superframe.
     *
     * @return 0 for A to 5 for F.
     */
    uint8_t nextVoiceIndex() const
    {
        return voiceIndex;
    }

private:
    Lc lc;                      ///< Link control of the call
    EmbeddedLcEncoder embedded; ///< Fragments of the embedded LC
    uint8_t colorCode;          ///< Colour code
    uint8_t voiceIndex;         ///< Next voice burst, 0 (A) to 5 (F)
    uint64_t voiceSync;         ///< Sync pattern of voice bursts A
    uint64_t dataSync;          ///< Sync pattern of data bursts
};

/**
 * Result of the processing of a received burst.
 */
struct VoiceEvent {
    /// A call has started: from a voice LC header (lc valid) or, joining
    /// late, from a voice burst with a matching colour code (lc not yet
    /// known)
    bool callStart;

    /// The link control of the call is known: set on the voice LC header and
    /// when the embedded LC has been received, for late entry
    bool lcValid;

    /// The burst carries voice, see frames
    bool voice;

    /// The call has ended, by a terminator with LC
    bool callEnd;

    /// Link control, valid when lcValid is set
    Lc lc;

    /// AMBE+2 frames, valid when voice is set
    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];
};

/**
 * Follows a direct mode voice transmission from the bursts found by the
 * receiver. Bursts with another colour code are ignored.
 */
class VoiceDecoder
{
public:
    VoiceDecoder();

    /**
     * \brief Set the colour code of the bursts to be accepted.
     *
     * @param colorCode: colour code, 0 to 15.
     */
    void setColorCode(uint8_t colorCode);

    /**
     * \brief Forget the current call, e.g. when the receiver lost the
     * synchronisation.
     */
    void reset();

    /**
     * \brief Process a received burst.
     *
     * @param burst: BURST_BYTES bytes.
     * @param sync: sync type found by the receiver.
     * @param ev: result, always written.
     */
    void process(const uint8_t *burst, SyncType sync, VoiceEvent &ev);

    /**
     * \brief Check whether a call is being received.
     *
     * @return true between the start and the end of a call.
     */
    bool inCall() const
    {
        return active;
    }

private:
    EmbeddedLcDecoder embedded; ///< Embedded LC fragments
    Lc callLc;                  ///< Link control of the call, if known
    uint8_t colorCode;          ///< Accepted colour code
    int8_t voiceIndex;          ///< Last voice burst, 0 (A) to 5 (F), or -1
    bool active;                ///< A call is being received
    bool lcKnown;               ///< The link control of the call is known
};

} // namespace DMR

#endif // DMR_VOICE_H
