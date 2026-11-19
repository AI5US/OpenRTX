/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2016-2021 Jonathan Naylor G4KLX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YSF_FICH_H
#define YSF_FICH_H

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstdint>
#include "protocols/YSF/Convolution.hpp"

namespace YSF
{

/**
 * Frame information channel: what a frame carries and where it sits in the
 * transmission (MMDVMHost YSFFICH.cpp). Coded with a CRC, four Golay (24,12)
 * words and the convolutional code, then interleaved.
 */
class Fich
{
public:
    /**
     * Constructor: all fields zero.
     */
    Fich();

    /**
     * Destructor.
     */
    ~Fich();

    /**
     * Decode the FICH of a frame.
     *
     * @param frame: FRAME_BYTES bytes, sync first.
     * @param conv: decoder to use.
     * @return true if the FICH is valid, otherwise its fields are undefined.
     */
    bool decode(const uint8_t *frame, Convolution &conv);

    /**
     * Encode the FICH into a frame.
     *
     * @param frame: FRAME_BYTES bytes, sync first; the FICH bytes are
     * written.
     */
    void encode(uint8_t *frame);

    uint8_t fi() const;   ///< Frame information, see Fi
    uint8_t cs() const;   ///< Callsign information
    uint8_t cm() const;   ///< Call mode
    uint8_t fn() const;   ///< Frame number
    uint8_t ft() const;   ///< Last frame number
    uint8_t dt() const;   ///< Data type, see Dt
    uint8_t mr() const;   ///< Message route, see Mr
    uint8_t dgId() const; ///< Digital group ID

    void setFi(uint8_t fi);
    void setCs(uint8_t cs);
    void setCm(uint8_t cm);
    void setFn(uint8_t fn);
    void setFt(uint8_t ft);
    void setDt(uint8_t dt);
    void setMr(uint8_t mr);
    void setDgId(uint8_t id);

private:
    /// Information bytes, then the CRC
    std::array<uint8_t, 6> fich;
};

} // namespace YSF

#endif // YSF_FICH_H
