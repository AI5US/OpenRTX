/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_SLOWDATA_H
#define DSTAR_SLOWDATA_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/DSTAR/Constants.hpp"
#include "protocols/DSTAR/Header.hpp"

namespace DStar
{

/// Slow data block: two consecutive frames, type/length byte + 5 bytes
static constexpr size_t SLOW_DATA_BLOCK_SIZE = 2 * SLOW_DATA_SIZE;

/// Slow data block type mask, high nibble of the first byte of a block
static const uint8_t SLOW_DATA_TYPE_MASK = 0xF0;
/// Slow data block length mask, low nibble of the first byte of a block
static const uint8_t SLOW_DATA_LENGTH_MASK = 0x0F;
/// Text message block, low nibble: block number 0 to 3
static const uint8_t SLOW_DATA_TYPE_TEXT = 0x40;
/// Header repeat block, low nibble: number of header bytes in the block
static const uint8_t SLOW_DATA_TYPE_HEADER = 0x50;
/// Filler byte, sent when there is no slow data
static const uint8_t SLOW_DATA_FILLER = 0x66;

/**
 * Slow data encoder.
 *
 * The caller sends DATA_SYNC in frame 0 of each superframe and calls next()
 * exactly once for each of the frames 1 to 20, i.e. 20 times per
 * superframe, starting after reset(). The encoder counts the calls to keep
 * track of the block boundaries (two frames) and of the superframes: the 20
 * calls of a superframe carry 10 blocks.
 *
 * Schedule, restarted by reset():
 * - message and header set: the text message (4 blocks) in even
 *   superframes (0, 2, ...), the header (9 blocks: 8 blocks of 5 bytes, type
 *   0x55, plus one of 1 byte, type 0x51) in odd superframes (1, 3, ...);
 * - message only: the text message in every superframe;
 * - header only: the header in every superframe;
 * - nothing: filler only.
 * The unused blocks of a superframe are filled with SLOW_DATA_FILLER.
 * setMessage() and setHeader() take effect at the next block boundary (the
 * next odd numbered frame); they do not restart the schedule.
 */
class SlowDataEncoder
{
public:
    /**
     * \brief Constructor, equivalent to reset().
     */
    SlowDataEncoder();

    /**
     * \brief Clear message and header, restart from superframe 0, frame 1.
     */
    void reset();

    /**
     * \brief Set the text message.
     *
     * @param text: NUL terminated message, truncated to MESSAGE_SIZE
     * characters and space padded; nullptr or an empty string remove the
     * message.
     */
    void setMessage(const char *text);

    /**
     * \brief Set the radio header to be repeated in the slow data.
     *
     * @param h: header, its CRC is computed by the encoder.
     */
    void setHeader(const Header &h);

    /**
     * \brief Get the slow data of the next frame among frames 1 to 20.
     *
     * @param out: slow data field, already scrambled.
     */
    void next(uint8_t out[SLOW_DATA_SIZE]);

private:
    void makeBlock(size_t block, uint8_t out[SLOW_DATA_BLOCK_SIZE]) const;

    uint8_t message[MESSAGE_SIZE];         ///< Space padded message
    uint8_t header[HEADER_SIZE];           ///< Header repeat, with CRC
    uint8_t current[SLOW_DATA_BLOCK_SIZE]; ///< Block being sent
    bool hasMsg;                           ///< Message set
    bool hasHdr;                           ///< Header set
    size_t frame;        ///< Next frame, 0 to 19 (= frames 1 to 20)
    uint32_t superframe; ///< Superframe counter
};

/**
 * Slow data decoder.
 *
 * Call sync() when the data sync (frame 0 of a superframe) is received, and
 * push() for each of the following frames 1 to 20. Text message and header
 * repeat stay available until reset(), and are replaced when new ones are
 * received.
 */
class SlowDataDecoder
{
public:
    /**
     * \brief Constructor, equivalent to reset().
     */
    SlowDataDecoder();

    /**
     * \brief Discard all received data.
     */
    void reset();

    /**
     * \brief Data sync received: the next frame starts a block.
     */
    void sync();

    /**
     * \brief Process the slow data field of a received frame.
     *
     * @param in: received slow data field, still scrambled.
     */
    void push(const uint8_t in[SLOW_DATA_SIZE]);

    /**
     * \brief Check if a complete text message has been received.
     *
     * @return true if a text message is available.
     */
    bool hasMessage() const;

    /**
     * \brief Get the last complete text message received.
     *
     * @return NUL terminated message, trailing spaces removed; an empty
     * string if no message has been received.
     */
    const char *getMessage() const;

    /**
     * \brief Check if a header repeat with a valid CRC has been received.
     *
     * @return true if a header is available.
     */
    bool hasHeader() const;

    /**
     * \brief Get the last valid header received in the slow data.
     *
     * @param h: header to be loaded, left untouched if none is available.
     * @return true if a header is available.
     */
    bool getHeader(Header &h) const;

private:
    void processBlock();
    void processText();
    void processHeader();

    uint8_t block[SLOW_DATA_BLOCK_SIZE]; ///< Block being received
    size_t blockPtr;                     ///< Bytes received in block

    char text[MESSAGE_SIZE];             ///< Message being collected
    uint8_t textParts;                   ///< Bit n set: part n received
    char message[MESSAGE_SIZE + 1];      ///< Last complete message
    bool msgValid;                       ///< Message available

    uint8_t hdrBuf[HEADER_SIZE];         ///< Header being collected
    size_t hdrPtr;                       ///< Header bytes collected
    uint8_t header[HEADER_SIZE];         ///< Last valid header
    bool hdrValid;                       ///< Header available
};

} // namespace DStar

#endif // DSTAR_SLOWDATA_H
