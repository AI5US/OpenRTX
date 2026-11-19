<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Yaesu System Fusion (C4FM) in OpenRTX

Status: V/D mode 2 (DN) voice, behind the `ysf` meson option and the
`C62_YSF` CMake option. Tested in software only (unit tests, and frames
cross-checked against MMDVMHost); not yet tested on air.

## Scope

Supported:

- V/D mode 2 (DN) voice, transmission and reception, simplex and through
  repeaters (YSF transmissions are continuous, there are no timeslots);
- the source and destination callsigns, from the header or, joining late,
  from the data channel of the communications frames.

Not supported, for now:

- voice full rate (VW, IMBE) and V/D mode 1: the transmission is shown but
  not played;
- data (GPS, pictures, Wires-X commands), DG-ID filtering on reception;
- settings: transmissions go to `ALL` with DG-ID 0, from the radio callsign.

## Implementation

| Part | Files |
|---|---|
| Vocoder: AMBE+2 3600x2450, its 49 parameter bits (no DMR frame FEC) | `core/dmr_vocoder.h` (`dmrEncoder_encodeParams()`, `dmrDecoder_decodeParams()`), codec thread `CODEC_YSF_AMBE` |
| Constants | `protocols/YSF/Constants.hpp` |
| Convolutional code (K = 5, rate 1/2) | `protocols/YSF/Convolution.*` |
| Frame information channel (FICH): CRC, Golay (24,12) (M17's), convolutional code | `protocols/YSF/Fich.*` |
| Data channels (V/D mode 2, full rate) and voice channel | `protocols/YSF/Payload.*` |
| Call level encoder and decoder | `protocols/YSF/Voice.*` |
| 4FSK receiver | `protocols/YSF/Demodulator.*` |
| 4FSK modulator | DMR's (`protocols/DMR/Modulator.*`), with its deviation set |
| Operating mode | `rtx/OpMode_YSF.*`, `OPMODE_YSF` |
| Radio drivers | YSF uses the M17 baseband configuration |
| UI | mode `YSF` (macro 5), received callsigns on the main screen |

Layer 2 is ported from MMDVMHost and the receiver from MMDVM (G4KLX,
GPL-2.0-or-later). FICH and data channels are bit for bit those of
MMDVMHost: frames built here decode with MMDVMHost's `YSFFICH` and
`YSFPayload`, and MMDVMHost's correction of a damaged voice channel gives
back the frame built here.

### Frames

A frame lasts 100 ms: a 40 bit sync, the 25 byte FICH and five 18 byte
blocks. In V/D mode 2 each block holds 5 bytes of the data channel (DCH)
and a 104 bit voice channel (VCH): the first 27 of the 49 AMBE+2 parameter
bits three times, the other 22 once and a pad bit, whitened and bit
interleaved. The DCH cycles over eight frames: destination, source,
downlink, uplink, remarks and data. Header and terminator frames carry two
full rate DCHs instead: destination and source, downlink and uplink.

The order of the 49 parameter bits in the VCH follows MMDVM_CM's YSF to DMR
conversion (the first 12 bits are the AMBE+2 C0 information bits, then 12
of C1, then 25 of C2 and C3): it is the part to check first on air.

### Modem

- 4FSK at 4800 symbols/s, root raised cosine shaping with roll-off 0.2, as
  DMR: the DMR modulator and a receiver built like the DMR one (DC removal,
  matched filter, decision levels from each sync, symbols read between the
  samples at the sync correlation peak).
- Deviation: +/-700 and +/-2100 Hz, against a nominal +/-900 and +/-2700 Hz.
  With the baseband scaled like M17 the nominal deviation clips 3% of the
  samples on the filter overshoot (0.06% at 700 Hz); receivers take their
  decision levels from the sync, and MMDVM offers half the nominal
  deviation for YSF.
- Transmission: 100 ms preamble (alternating +3 and -3 symbols), a header,
  more headers until seven speech frames are queued, a communications frame
  per 100 ms, a terminator.

## Building

```bash
meson setup -Dysf=true build      # Linux, MD-3x0, MD-UV3x0, Module17, ...
```

On the Retevis C62:

```bash
west build -b c62 -d build . -- -DC62_YSF=ON
```

YSF can be enabled with D-STAR and DMR; they share mbelib-neo, and YSF
shares the AMBE+2 vocoder and the modulator with DMR.

The AMBE+2 patent question of DMR applies to YSF V/D mode 2 as well, see
`docs/dmr.md`.
