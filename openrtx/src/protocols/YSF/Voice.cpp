/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/YSF/Fich.hpp"
#include "protocols/YSF/Payload.hpp"
#include "protocols/YSF/Voice.hpp"

using namespace YSF;

/// Frame numbers of the V/D mode 2 DCH cycle carrying the callsigns
static constexpr uint8_t FN_DESTINATION = 0;
static constexpr uint8_t FN_SOURCE = 1;

/// Sync bytes, at the start of every frame
static constexpr uint8_t SYNC_PATTERN[SYNC_BYTES] = {
    0xD4, 0x71, 0xC9, 0x63, 0x4D,
};

/// Fill of the callsign fields not used
static constexpr uint8_t BLANK[CALLSIGN_LEN] = {
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
};

/// Copy a string into a callsign field, padded with spaces
static void setField(const char *str, std::array<uint8_t, CALLSIGN_LEN> &field)
{
    size_t len = strnlen(str, CALLSIGN_LEN);

    memcpy(field.data(), BLANK, CALLSIGN_LEN);
    memcpy(field.data(), str, len);
}

VoiceEncoder::VoiceEncoder() : dgId(0), fn(0)
{
    source.fill(' ');
    destination.fill(' ');
}

VoiceEncoder::~VoiceEncoder()
{
}

void VoiceEncoder::start(const char *source, const char *destination,
                         uint8_t dgId)
{
    setField(source, this->source);
    setField(destination, this->destination);
    this->dgId = dgId;
    fn = 0;
}

void VoiceEncoder::header(uint8_t *frame)
{
    writeFich(FI_HEADER, frame);
    writeCallsigns(frame);
}

void VoiceEncoder::voice(const uint8_t voice[VOICE_FRAMES][VOICE_FRAME_BYTES],
                         uint8_t *frame)
{
    writeFich(FI_COMMUNICATIONS, frame);

    // The DCH cycles through the callsigns and remarks, blank but for the
    // destination and the source
    uint8_t dch[VD2_DCH_BYTES];
    if (fn == FN_DESTINATION)
        memcpy(dch, destination.data(), VD2_DCH_BYTES);
    else if (fn == FN_SOURCE)
        memcpy(dch, source.data(), VD2_DCH_BYTES);
    else
        memcpy(dch, BLANK, VD2_DCH_BYTES);

    writeVd2Dch(dch, frame);

    for (size_t i = 0; i < VOICE_FRAMES; i++)
        writeVch(i, voice[i], frame);

    fn = (fn + 1) % (VD_MODE2_FT + 1);
}

void VoiceEncoder::terminator(uint8_t *frame)
{
    writeFich(FI_TERMINATOR, frame);
    writeCallsigns(frame);
}

void VoiceEncoder::writeFich(uint8_t fi, uint8_t *frame) const
{
    memset(frame, 0x00, FRAME_BYTES);
    memcpy(frame, SYNC_PATTERN, SYNC_BYTES);

    Fich fich;
    fich.setFi(fi);
    fich.setCs(CS_RADIO);
    fich.setFn((fi == FI_COMMUNICATIONS) ? fn : 0);
    fich.setFt(VD_MODE2_FT);
    fich.setDt(DT_VD_MODE2);
    fich.setMr(MR_DIRECT);
    fich.setDgId(dgId);
    fich.encode(frame);
}

void VoiceEncoder::writeCallsigns(uint8_t *frame) const
{
    // CSD1: destination and source; CSD2: downlink and uplink, blank
    uint8_t csd[FR_DCH_BYTES];

    memcpy(csd, destination.data(), CALLSIGN_LEN);
    memcpy(csd + CALLSIGN_LEN, source.data(), CALLSIGN_LEN);
    writeFrDch(0, csd, frame);

    memcpy(csd, BLANK, CALLSIGN_LEN);
    memcpy(csd + CALLSIGN_LEN, BLANK, CALLSIGN_LEN);
    writeFrDch(1, csd, frame);
}

VoiceDecoder::VoiceDecoder()
{
    reset();
}

VoiceDecoder::~VoiceDecoder()
{
}

void VoiceDecoder::reset()
{
    memset(source, 0x00, sizeof(source));
    memset(destination, 0x00, sizeof(destination));
    active = false;
    haveSource = false;
    haveDestination = false;
}

bool VoiceDecoder::inCall() const
{
    return active;
}

void VoiceDecoder::process(const uint8_t *frame, VoiceEvent &ev)
{
    memset(&ev, 0x00, sizeof(ev));

    Fich fich;
    if (fich.decode(frame, conv) == false) {
        // Keep the speech going through a damaged FICH: the voice channels
        // have their own protection
        if (active)
            readVoice(frame, ev);

        return;
    }

    ev.dgId = fich.dgId();

    switch (fich.fi()) {
        case FI_HEADER: {
            ev.callStart = (active == false);
            active = true;

            uint8_t csd[FR_DCH_BYTES];
            if (readFrDch(0, frame, conv, csd)) {
                copyCallsign(csd, destination);
                copyCallsign(csd + CALLSIGN_LEN, source);
                haveSource = true;
                haveDestination = true;
            }
            break;
        }

        case FI_COMMUNICATIONS: {
            if (active == false) {
                ev.callStart = true;
                active = true;
            }

            if (fich.dt() != DT_VD_MODE2) {
                ev.unsupported = true;
                break;
            }

            uint8_t dch[VD2_DCH_BYTES];
            if (readVd2Dch(frame, conv, dch)) {
                if ((fich.fn() == FN_DESTINATION) && !haveDestination) {
                    copyCallsign(dch, destination);
                    haveDestination = true;
                }

                if ((fich.fn() == FN_SOURCE) && !haveSource) {
                    copyCallsign(dch, source);
                    haveSource = true;
                    ev.sourceValid = true;
                }
            }

            readVoice(frame, ev);
            break;
        }

        case FI_TERMINATOR:
            ev.callEnd = active;
            reset();
            break;

        default:
            break;
    }

    if (haveSource && (fich.fi() == FI_HEADER))
        ev.sourceValid = true;

    if (ev.sourceValid) {
        memcpy(ev.source, source, sizeof(ev.source));
        memcpy(ev.destination, destination, sizeof(ev.destination));
    }
}

void VoiceDecoder::readVoice(const uint8_t *frame, VoiceEvent &ev) const
{
    ev.voice = true;
    for (size_t i = 0; i < VOICE_FRAMES; i++)
        ev.voiceErrors += readVch(i, frame, ev.frames[i]);
}

void VoiceDecoder::copyCallsign(const uint8_t *field, char *out)
{
    size_t len = CALLSIGN_LEN;
    while ((len > 0) && (field[len - 1] == ' '))
        len--;

    memcpy(out, field, len);
    out[len] = '\0';
}
