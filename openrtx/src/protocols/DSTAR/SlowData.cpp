/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstring>
#include "protocols/DSTAR/Crc.hpp"
#include "protocols/DSTAR/SlowData.hpp"

using namespace DStar;

namespace
{

/// Data bytes in a slow data block, after the type/length byte
constexpr size_t BLOCK_DATA = SLOW_DATA_BLOCK_SIZE - 1;

/// Frames carrying slow data in a superframe (frames 1 to 20)
constexpr size_t DATA_FRAMES = SUPERFRAME_FRAMES - 1;

/// Text message blocks
constexpr size_t TEXT_BLOCKS = MESSAGE_SIZE / BLOCK_DATA;

/// Header repeat blocks
constexpr size_t HEADER_BLOCKS = (HEADER_SIZE + BLOCK_DATA - 1) / BLOCK_DATA;

static_assert(TEXT_BLOCKS * 2 <= DATA_FRAMES, "Text does not fit");
static_assert(HEADER_BLOCKS * 2 <= DATA_FRAMES, "Header does not fit");

bool headerCrcValid(const uint8_t hdr[HEADER_SIZE])
{
    uint16_t crc = crcCcitt(hdr, HEADER_SIZE - 2);

    return (hdr[HEADER_SIZE - 2] == (crc & 0xFF))
        && (hdr[HEADER_SIZE - 1] == (crc >> 8));
}

} // namespace

SlowDataEncoder::SlowDataEncoder()
{
    reset();
}

void SlowDataEncoder::reset()
{
    memset(message, ' ', sizeof(message));
    memset(header, 0x00, sizeof(header));
    memset(current, SLOW_DATA_FILLER, sizeof(current));
    hasMsg = false;
    hasHdr = false;
    frame = 0;
    superframe = 0;
}

void SlowDataEncoder::setMessage(const char *text)
{
    memset(message, ' ', sizeof(message));
    hasMsg = false;

    if ((text == nullptr) || (text[0] == '\0'))
        return;

    for (size_t i = 0; (i < MESSAGE_SIZE) && (text[i] != '\0'); i++)
        message[i] = static_cast<uint8_t>(text[i]);

    hasMsg = true;
}

void SlowDataEncoder::setHeader(const Header &h)
{
    h.toBytes(header);
    hasHdr = true;
}

void SlowDataEncoder::next(uint8_t out[SLOW_DATA_SIZE])
{
    // A new block starts on even frame indices (frames 1, 3, ..., 19)
    size_t half = frame % 2;
    if (half == 0)
        makeBlock(frame / 2, current);

    for (size_t i = 0; i < SLOW_DATA_SIZE; i++) {
        uint8_t byte = current[half * SLOW_DATA_SIZE + i];
        out[i] = byte ^ SLOW_DATA_SCRAMBLER[i];
    }

    frame++;
    if (frame >= DATA_FRAMES) {
        frame = 0;
        superframe++;
    }
}

void SlowDataEncoder::makeBlock(size_t block,
                                uint8_t out[SLOW_DATA_BLOCK_SIZE]) const
{
    bool sendHeader = hasHdr;
    if (hasMsg && hasHdr)
        sendHeader = (superframe % 2) == 1;

    bool sendMessage = hasMsg && !sendHeader;

    memset(out, SLOW_DATA_FILLER, SLOW_DATA_BLOCK_SIZE);

    if (sendMessage && (block < TEXT_BLOCKS)) {
        out[0] = static_cast<uint8_t>(SLOW_DATA_TYPE_TEXT | block);
        memcpy(&out[1], &message[block * BLOCK_DATA], BLOCK_DATA);
    }

    if (sendHeader && (block < HEADER_BLOCKS)) {
        size_t pos = block * BLOCK_DATA;
        size_t len = HEADER_SIZE - pos;
        if (len > BLOCK_DATA)
            len = BLOCK_DATA;

        out[0] = static_cast<uint8_t>(SLOW_DATA_TYPE_HEADER | len);
        memcpy(&out[1], &header[pos], len);
    }
}

SlowDataDecoder::SlowDataDecoder()
{
    reset();
}

void SlowDataDecoder::reset()
{
    memset(block, 0x00, sizeof(block));
    blockPtr = 0;

    memset(text, ' ', sizeof(text));
    textParts = 0;
    memset(message, 0x00, sizeof(message));
    msgValid = false;

    memset(hdrBuf, 0x00, sizeof(hdrBuf));
    hdrPtr = 0;
    memset(header, 0x00, sizeof(header));
    hdrValid = false;
}

void SlowDataDecoder::sync()
{
    blockPtr = 0;
}

void SlowDataDecoder::push(const uint8_t in[SLOW_DATA_SIZE])
{
    for (size_t i = 0; i < SLOW_DATA_SIZE; i++)
        block[blockPtr + i] = in[i] ^ SLOW_DATA_SCRAMBLER[i];

    blockPtr += SLOW_DATA_SIZE;
    if (blockPtr >= SLOW_DATA_BLOCK_SIZE) {
        blockPtr = 0;
        processBlock();
    }
}

bool SlowDataDecoder::hasMessage() const
{
    return msgValid;
}

const char *SlowDataDecoder::getMessage() const
{
    return message;
}

bool SlowDataDecoder::hasHeader() const
{
    return hdrValid;
}

bool SlowDataDecoder::getHeader(Header &h) const
{
    if (!hdrValid)
        return false;

    h.fromBytes(header);

    return true;
}

void SlowDataDecoder::processBlock()
{
    uint8_t type = block[0] & SLOW_DATA_TYPE_MASK;

    if (type == SLOW_DATA_TYPE_TEXT)
        processText();
    else if (type == SLOW_DATA_TYPE_HEADER)
        processHeader();
}

void SlowDataDecoder::processText()
{
    // Low nibble: block number, as in MMDVMHost CDStarSlowData::loadText()
    size_t part = block[0] & SLOW_DATA_LENGTH_MASK;
    if (part >= TEXT_BLOCKS)
        return;

    for (size_t i = 0; i < BLOCK_DATA; i++)
        text[part * BLOCK_DATA + i] = static_cast<char>(block[1 + i] & 0x7F);

    textParts |= static_cast<uint8_t>(1 << part);
    if (textParts != ((1 << TEXT_BLOCKS) - 1))
        return;

    textParts = 0;

    size_t len = MESSAGE_SIZE;
    while ((len > 0) && ((text[len - 1] == ' ') || (text[len - 1] == '\0')))
        len--;

    memcpy(message, text, len);
    message[len] = '\0';
    msgValid = true;
}

void SlowDataDecoder::processHeader()
{
    /*
     * The header is sent in blocks of 5 bytes (type 0x55) and a last block
     * with the remaining byte (type 0x51). A block shorter than 5 bytes or
     * the 41st byte end a header; MMDVMHost, which always takes 5 bytes per
     * block, works the same way when the last block is a 0x55 one.
     */
    size_t len = block[0] & SLOW_DATA_LENGTH_MASK;
    if (len > BLOCK_DATA)
        len = BLOCK_DATA;

    size_t space = HEADER_SIZE - hdrPtr;
    size_t copy = (len < space) ? len : space;
    memcpy(&hdrBuf[hdrPtr], &block[1], copy);
    hdrPtr += copy;

    if (hdrPtr < HEADER_SIZE) {
        // Short block before the end: incomplete header, start over
        if (len < BLOCK_DATA)
            hdrPtr = 0;

        return;
    }

    hdrPtr = 0;

    if (headerCrcValid(hdrBuf)) {
        memcpy(header, hdrBuf, HEADER_SIZE);
        hdrValid = true;
        return;
    }

    /*
     * Some radios alter the header in the slow data: retry with the clean
     * up done by MMDVMHost CDStarSlowData::getHeader().
     */
    hdrBuf[0] &= (FLAG_INTERRUPTED | FLAG_URGENT | FLAG_REPEATER);
    hdrBuf[1] = 0x00;
    hdrBuf[2] = 0x00;
    for (size_t i = HEADER_FLAGS; i < HEADER_CRC_OFFSET; i++)
        hdrBuf[i] &= 0x7F;

    if (headerCrcValid(hdrBuf)) {
        memcpy(header, hdrBuf, HEADER_SIZE);
        hdrValid = true;
    }
}
