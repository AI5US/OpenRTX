/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2015-2025 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DMR_LC_H
#define DMR_LC_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "protocols/DMR/Constants.hpp"

namespace DMR
{

/**
 * Voice link control: who calls whom. Ported from MMDVMHost DMRLC.cpp.
 */
struct Lc {
    uint8_t flco;    ///< Full link control opcode, see Flco
    uint8_t fid;     ///< Feature set ID, 0 for the standard feature set
    uint8_t options; ///< Service options
    uint32_t dst;    ///< Destination: talkgroup or radio ID, 24 bits
    uint32_t src;    ///< Source radio ID, 24 bits

    /**
     * \brief Check whether this is a group call.
     *
     * @return true for a group call, false for a private call.
     */
    bool isGroup() const
    {
        return flco == FLCO_GROUP;
    }

    /**
     * \brief Serialise the link control.
     *
     * @param bytes: LC_BYTES bytes.
     */
    void toBytes(uint8_t *bytes) const;

    /**
     * \brief Load the link control from its serialised form.
     *
     * @param bytes: LC_BYTES bytes.
     */
    void fromBytes(const uint8_t *bytes);
};

/**
 * \brief Write the full link control of a voice LC header or of a terminator
 * with LC in the information field of a data burst: Reed-Solomon (12,9)
 * checksum masked by the data type, BPTC (196,96) coded. The slot type and
 * sync fields are left untouched.
 *
 * @param lc: link control.
 * @param dataType: DT_VOICE_LC_HEADER or DT_TERMINATOR_WITH_LC.
 * @param burst: burst.
 */
void encodeFullLc(const Lc &lc, uint8_t dataType, uint8_t *burst);

/**
 * \brief Read the full link control of a voice LC header or of a terminator
 * with LC.
 *
 * @param burst: burst.
 * @param dataType: DT_VOICE_LC_HEADER or DT_TERMINATOR_WITH_LC.
 * @param lc: decoded link control, written only on success.
 * @return true if the checksum is valid.
 */
bool decodeFullLc(const uint8_t *burst, uint8_t dataType, Lc &lc);

/**
 * Embedded link control encoder: the link control sent, in four 32 bit
 * fragments, in voice bursts B to E (MMDVMHost DMREmbeddedData.cpp).
 */
class EmbeddedLcEncoder
{
public:
    /// Number of fragments
    static constexpr size_t FRAGMENTS = 4;

    EmbeddedLcEncoder();

    /**
     * \brief Set the link control to be sent, computing its fragments.
     *
     * @param lc: link control.
     */
    void setLc(const Lc &lc);

    /**
     * \brief Get a fragment.
     *
     * @param n: fragment number, 0 to FRAGMENTS - 1.
     * @param data: 4 bytes, the embedded signalling of the burst.
     * @return the link control start/stop value of the fragment.
     */
    uint8_t getFragment(size_t n, uint8_t *data) const;

private:
    uint8_t raw[16]; ///< The 128 coded bits, in transmission order
};

/**
 * Embedded link control decoder: collects the fragments of the link control
 * from voice bursts B to E.
 */
class EmbeddedLcDecoder
{
public:
    EmbeddedLcDecoder();

    /**
     * \brief Forget the fragments received so far.
     */
    void reset();

    /**
     * \brief Add a fragment.
     *
     * @param lcss: link control start/stop value from the EMB.
     * @param data: 4 bytes of embedded signalling.
     * @param lc: decoded link control, written when a complete and valid
     * group or private voice link control has been received.
     * @return true when lc has been written.
     */
    bool push(uint8_t lcss, const uint8_t *data, Lc &lc);

private:
    uint8_t raw[16]; ///< The 128 coded bits, in transmission order
    uint8_t count;   ///< Fragments received in sequence, 0 to 4
};

} // namespace DMR

#endif // DMR_LC_H
