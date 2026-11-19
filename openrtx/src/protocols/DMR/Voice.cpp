/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DMR/Burst.hpp"
#include "protocols/DMR/Voice.hpp"

using namespace DMR;

VoiceEncoder::VoiceEncoder() : lc(), colorCode(0), voiceIndex(0)
{
}

void VoiceEncoder::start(const Lc &lc, uint8_t colorCode)
{
    this->lc = lc;
    this->colorCode = colorCode & 0x0F;
    embedded.setLc(lc);
    voiceIndex = 0;
}

void VoiceEncoder::header(uint8_t *burst) const
{
    memset(burst, 0x00, BURST_BYTES);
    encodeFullLc(lc, DT_VOICE_LC_HEADER, burst);
    encodeSlotType(burst, colorCode, DT_VOICE_LC_HEADER);
    setSync(burst, MS_DATA_SYNC);
}

void VoiceEncoder::voice(const uint8_t frames[VOICE_FRAMES][VOICE_FRAME_BYTES],
                         uint8_t *burst)
{
    memset(burst, 0x00, BURST_BYTES);
    setVoiceFrames(burst, frames);

    if (voiceIndex == 0) {
        setSync(burst, MS_VOICE_SYNC);
    } else if (voiceIndex <= EmbeddedLcEncoder::FRAGMENTS) {
        uint8_t data[4];
        uint8_t lcss = embedded.getFragment(voiceIndex - 1, data);

        encodeEmb(burst, colorCode, false, lcss);
        setEmbeddedData(burst, data);
    } else {
        // Burst F: null embedded signalling
        const uint8_t data[4] = { 0, 0, 0, 0 };

        encodeEmb(burst, colorCode, false, LCSS_SINGLE);
        setEmbeddedData(burst, data);
    }

    voiceIndex = (voiceIndex + 1) % SUPERFRAME_BURSTS;
}

void VoiceEncoder::terminator(uint8_t *burst) const
{
    memset(burst, 0x00, BURST_BYTES);
    encodeFullLc(lc, DT_TERMINATOR_WITH_LC, burst);
    encodeSlotType(burst, colorCode, DT_TERMINATOR_WITH_LC);
    setSync(burst, MS_DATA_SYNC);
}

VoiceDecoder::VoiceDecoder() : callLc(), colorCode(0)
{
    reset();
}

void VoiceDecoder::setColorCode(uint8_t colorCode)
{
    this->colorCode = colorCode & 0x0F;
}

void VoiceDecoder::reset()
{
    embedded.reset();
    voiceIndex = -1;
    active = false;
    lcKnown = false;
}

void VoiceDecoder::process(const uint8_t *burst, SyncType sync, VoiceEvent &ev)
{
    memset(&ev, 0x00, sizeof(ev));

    switch (sync) {
        case SyncType::DATA: {
            uint8_t cc, dataType;

            if (decodeSlotType(burst, cc, dataType) == false)
                return;

            if (cc != colorCode)
                return;

            if (dataType == DT_VOICE_LC_HEADER) {
                Lc lc;

                if (decodeFullLc(burst, DT_VOICE_LC_HEADER, lc) == false)
                    return;

                // Headers are repeated at the start of a call
                if (active && lcKnown && (lc.flco == callLc.flco)
                    && (lc.src == callLc.src) && (lc.dst == callLc.dst))
                    return;

                // Another header starts a new call, even within a call
                ev.callStart = true;
                ev.lcValid = true;
                ev.lc = lc;
                active = true;
                lcKnown = true;
                callLc = lc;
                voiceIndex = -1;
                embedded.reset();
            } else if ((dataType == DT_TERMINATOR_WITH_LC) && active) {
                ev.callEnd = true;
                reset();
            }

            return;
        }

        case SyncType::VOICE:
            voiceIndex = 0;
            break;

        default: {
            // A burst without sync is a voice burst only after burst A
            if (voiceIndex < 0)
                return;

            voiceIndex++;

            // Missed the sync of burst A: take this burst as burst A
            if (voiceIndex >= static_cast<int8_t>(SUPERFRAME_BURSTS)) {
                voiceIndex = 0;
                break;
            }

            uint8_t cc, lcss;
            bool pi;

            if (decodeEmb(burst, cc, pi, lcss) == false)
                break;

            if (cc != colorCode)
                return;

            // Join a call whose header has been missed
            if (active == false) {
                ev.callStart = true;
                active = true;
                lcKnown = false;
                embedded.reset();
            }

            uint8_t data[4];
            getEmbeddedData(burst, data);

            Lc lc;
            if (embedded.push(lcss, data, lc) && (lcKnown == false)) {
                ev.lcValid = true;
                ev.lc = lc;
                lcKnown = true;
                callLc = lc;
            }

            break;
        }
    }

    if (active == false)
        return;

    ev.voice = true;
    getVoiceFrames(burst, ev.frames);
}
