/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DSTAR_HEADER_H
#define DSTAR_HEADER_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/DSTAR/Constants.hpp"

namespace DStar
{

/// Flag byte 0 (flag 1 in the D-STAR specification): data transmission
static const uint8_t FLAG_DATA = 0x80;
/// Flag byte 0 (flag 1 in the D-STAR specification): repeater
static const uint8_t FLAG_REPEATER = 0x40;
/// Flag byte 0 (flag 1 in the D-STAR specification): interrupted
static const uint8_t FLAG_INTERRUPTED = 0x20;
/// Flag byte 0 (flag 1 in the D-STAR specification): control signal
static const uint8_t FLAG_CONTROL = 0x10;
/// Flag byte 0 (flag 1 in the D-STAR specification): urgent
static const uint8_t FLAG_URGENT = 0x08;

/// Number of flag bytes at the beginning of the radio header
static constexpr size_t HEADER_FLAGS = 3;

/// Radio header field offsets, as in MMDVMHost CDStarHeader
static constexpr size_t HEADER_RPT2_OFFSET = 3;    ///< Destination repeater
static constexpr size_t HEADER_RPT1_OFFSET = 11;   ///< Departure repeater
static constexpr size_t HEADER_UR_OFFSET = 19;     ///< Companion callsign
static constexpr size_t HEADER_MY_OFFSET = 27;     ///< Own callsign
static constexpr size_t HEADER_SUFFIX_OFFSET = 35; ///< Own callsign suffix
static constexpr size_t HEADER_CRC_OFFSET = 39;    ///< CRC, low byte first

/**
 * D-STAR radio header: 3 flag bytes, RPT2 (8), RPT1 (8), UR (8), MY (8),
 * MY suffix (4) and CRC (2), 41 bytes in total.
 *
 * Callsign fields are stored space padded. Text setters convert lowercase
 * letters to uppercase and truncate to the field size; text getters return
 * NUL terminated strings with trailing spaces removed and need an output
 * buffer of at least field size + 1 bytes (CALLSIGN_SIZE + 1 or
 * SUFFIX_SIZE + 1).
 */
class Header
{
public:
    /**
     * \brief Constructor: flags set to zero, all text fields set to spaces.
     */
    Header();

    /**
     * \brief Reset to the state set by the constructor.
     */
    void clear();

    /**
     * \brief Get a flag byte.
     *
     * @param n: flag byte index, 0 to 2; other values return 0.
     * @return flag byte value.
     */
    uint8_t getFlag(size_t n) const;

    /**
     * \brief Set a flag byte.
     *
     * @param n: flag byte index, 0 to 2; other values are ignored.
     * @param v: flag byte value.
     */
    void setFlag(size_t n, uint8_t v);

    /**
     * \brief Set the own callsign (MY, 8 characters).
     *
     * @param cs: NUL terminated callsign, nullptr is treated as empty.
     */
    void setMy(const char *cs);

    /**
     * \brief Set the own callsign suffix (MY suffix, 4 characters).
     *
     * @param s: NUL terminated suffix, nullptr is treated as empty.
     */
    void setSuffix(const char *s);

    /**
     * \brief Set the companion callsign (UR, 8 characters).
     *
     * @param cs: NUL terminated callsign, nullptr is treated as empty.
     */
    void setUr(const char *cs);

    /**
     * \brief Set the departure repeater callsign (RPT1, 8 characters).
     *
     * @param cs: NUL terminated callsign, nullptr is treated as empty.
     */
    void setRpt1(const char *cs);

    /**
     * \brief Set the destination repeater callsign (RPT2, 8 characters).
     *
     * @param cs: NUL terminated callsign, nullptr is treated as empty.
     */
    void setRpt2(const char *cs);

    /**
     * \brief Get the own callsign.
     *
     * @param out: output buffer, at least CALLSIGN_SIZE + 1 bytes.
     */
    void getMy(char *out) const;

    /**
     * \brief Get the own callsign suffix.
     *
     * @param out: output buffer, at least SUFFIX_SIZE + 1 bytes.
     */
    void getSuffix(char *out) const;

    /**
     * \brief Get the companion callsign.
     *
     * @param out: output buffer, at least CALLSIGN_SIZE + 1 bytes.
     */
    void getUr(char *out) const;

    /**
     * \brief Get the departure repeater callsign.
     *
     * @param out: output buffer, at least CALLSIGN_SIZE + 1 bytes.
     */
    void getRpt1(char *out) const;

    /**
     * \brief Get the destination repeater callsign.
     *
     * @param out: output buffer, at least CALLSIGN_SIZE + 1 bytes.
     */
    void getRpt2(char *out) const;

    /**
     * \brief Serialise the header, computing and appending the CRC.
     *
     * @param out: output buffer, D-STAR byte order.
     */
    void toBytes(uint8_t out[HEADER_SIZE]) const;

    /**
     * \brief Load the header from its serialised form.
     *
     * All fields are loaded even when the CRC is not valid.
     *
     * @param in: serialised header, D-STAR byte order.
     * @return true if the CRC is valid.
     */
    bool fromBytes(const uint8_t in[HEADER_SIZE]);

    /**
     * \brief Check the repeater flag (FLAG_REPEATER in flag byte 0).
     *
     * @return true if the transmission is addressed to a repeater.
     */
    bool isRepeater() const;

private:
    void setField(size_t offset, size_t size, const char *text);
    void getField(size_t offset, size_t size, char *out) const;

    uint8_t data[HEADER_SIZE]; ///< Header bytes, CRC excluded
};

} // namespace DStar

#endif // DSTAR_HEADER_H
