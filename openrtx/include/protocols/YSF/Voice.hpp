/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_VOICE_H
#define YSF_VOICE_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include "protocols/YSF/Constants.hpp"
#include "protocols/YSF/Convolution.hpp"

namespace YSF
{

/**
 * Builds the frames of a V/D mode 2 (DN) voice transmission: a header, a
 * communications frame per 100 ms of speech, and a terminator.
 */
class VoiceEncoder
{
public:
    /**
     * Constructor.
     */
    VoiceEncoder();

    /**
     * Destructor.
     */
    ~VoiceEncoder();

    /**
     * Start a new transmission.
     *
     * @param source: source callsign, up to CALLSIGN_LEN characters.
     * @param destination: destination, up to CALLSIGN_LEN characters.
     * @param dgId: digital group ID, 0 to 99.
     */
    void start(const char *source, const char *destination, uint8_t dgId);

    /**
     * Build the header frame.
     *
     * @param frame: output, FRAME_BYTES bytes.
     */
    void header(uint8_t *frame);

    /**
     * Build the next communications frame.
     *
     * @param voice: VOICE_FRAMES AMBE+2 frames of VOICE_FRAME_BYTES bytes.
     * @param frame: output, FRAME_BYTES bytes.
     */
    void voice(const uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES],
               uint8_t *frame);

    /**
     * Build the terminator frame.
     *
     * @param frame: output, FRAME_BYTES bytes.
     */
    void terminator(uint8_t *frame);

private:
    /**
     * Write the sync and the FICH of a frame.
     */
    void writeFich(uint8_t fi, uint8_t *frame) const;

    /**
     * Write the callsign data of a header or terminator frame.
     */
    void writeCallsigns(uint8_t *frame) const;

    std::array<uint8_t, CALLSIGN_LEN> source;      ///< Source callsign
    std::array<uint8_t, CALLSIGN_LEN> destination; ///< Destination
    uint8_t dgId;                                  ///< Digital group ID
    uint8_t fn;                                    ///< Next frame number
};

/**
 * What a received frame brought.
 */
struct VoiceEvent {
    /// A transmission has started, from its header or, joining late, from
    /// a communications frame
    bool callStart;

    /// The source callsign is known: from the header, or from the DCH of the
    /// first frame of each cycle when joining late
    bool sourceValid;

    /// The frame carries voice, see frames
    bool voice;

    /// The transmission has ended, by its terminator
    bool callEnd;

    /// The transmission is in a mode not decoded: V/D mode 1 or voice full
    /// rate (VW)
    bool unsupported;

    char source[CALLSIGN_LEN + 1];      ///< Source, valid with sourceValid
    char destination[CALLSIGN_LEN + 1]; ///< Destination, with sourceValid
    uint8_t dgId;                       ///< Digital group ID of the frame

    /// AMBE+2 frames, valid when voice is set
    uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES];

    /// Repeated voice bits found with an error in this frame
    size_t voiceErrors;
};

/**
 * Follows a voice transmission from the received frames.
 */
class VoiceDecoder
{
public:
    /**
     * Constructor.
     */
    VoiceDecoder();

    /**
     * Destructor.
     */
    ~VoiceDecoder();

    /**
     * Forget the current transmission.
     */
    void reset();

    /**
     * @return true while a transmission is being received.
     */
    bool inCall() const;

    /**
     * Process a received frame.
     *
     * @param frame: FRAME_BYTES bytes.
     * @param ev: what the frame brought.
     */
    void process(const uint8_t *frame, VoiceEvent &ev);

private:
    /**
     * Read the voice channels of a frame into an event.
     */
    void readVoice(const uint8_t *frame, VoiceEvent &ev) const;

    /**
     * Copy a callsign field, without its trailing spaces.
     */
    static void copyCallsign(const uint8_t *field, char *out);

    Convolution conv;                   ///< FICH and DCH decoder
    char source[CALLSIGN_LEN + 1];      ///< Source callsign
    char destination[CALLSIGN_LEN + 1]; ///< Destination
    bool active;                        ///< Transmission in progress
    bool haveSource;                    ///< source is known
    bool haveDestination;               ///< destination is known
};

} // namespace YSF

#endif // YSF_VOICE_H
