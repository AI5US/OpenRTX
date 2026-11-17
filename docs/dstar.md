<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# D-STAR in OpenRTX: feasibility study

Status: investigation only, no code yet. Facts marked **[unverified]** come
from secondary sources or from implementations of the spec, not from the
JARL text itself, and must be checked against it before any code is written.

## Summary

| Part | Feasible | Notes |
|---|---|---|
| Air interface (GMSK, header FEC, frames, slow data) | Yes | Well understood, GPL-2.0-or-later reference code exists (G4KLX MMDVM), runs in software on an STM32F4 |
| Hardware | Yes, same radios as M17 | Needs the MCU baseband path M17 already uses |
| Vocoder, Codec2 variant | Yes | Legally clean, but only interoperates with Codec2-aware stations |
| Vocoder, AMBE (interop with stock D-STAR radios) | **Blocker** | No good open encoder, legal status unclear, maintainers favour free codecs |

The vocoder decides what "D-STAR support" means. Everything below the
vocoder can be built and tested independently of that decision.

## The protocol

Reference: JARL "Digitalization Technology Standard for Amateur Radio"
(English), <https://www.jarl.org/English/7_dstar/STD6_0_E.pdf>. The spec is
known to contain errors; MMDVM and dsdcc are the practical references.

- **Modulation:** GMSK, 4800 bit/s, 1 bit per symbol, fits a 6.25 kHz
  channel. BT is quoted as 0.5, while MMDVM's TX filter uses
  `gaussfir(0.35, 1, 5)` **[unverified]**. Deviation about ±1.2 kHz
  (h = 0.5) **[unverified]**.
- **Transmission:** bit sync (0xAA...), frame sync 0xEA 0xA6, radio header,
  then voice frames until the end pattern 0x55 0x55 0xC8 0x7A.
- **Radio header:** 41 bytes:
  3 flag bytes, RPT2, RPT1, UR, MY (8 characters each, offsets 3, 11, 19,
  27), MY suffix (4 characters, offset 35), CRC-CCITT (init 0xFFFF, result
  inverted). The field order is **[unverified]**.
  The 328 bits plus 2 tail bits are coded with a rate 1/2, K = 3
  convolutional code (generators octal 7 and 5) to 660 bits, then block
  interleaved and XOR scrambled (table starting 0x70 0x4F 0x93 0x40,
  believed to be the x^7 + x^4 + 1 sequence **[unverified]**).
- **Voice frames:** every 20 ms, 72 vocoder bits (9 bytes) plus 24 slow data
  bits (3 bytes). 21 frames make a superframe; frame 0 carries the sync
  pattern 0x55 0x2D 0x16 instead of slow data.
- **Slow data:** 3-byte blocks XORed with 0x70 0x4F 0x93. The first byte of
  each 6-byte block is a type/length header. Type 0x4 carries the 20
  character message, 0x6 is filler; others carry a repeat of the header
  and GPS/D-PRS NMEA (type mapping **[unverified]**).
- **Codec2 extension:** SV9OAN, "Extending D-STAR with Codec 2", DCC 2019,
  <https://web.tapr.org/meetings/DCC_2019/2019-2-SV9OAN.pdf>. Bit 0 of
  flag 3 selects the vocoder (0 AMBE, 1 Codec2), bit 1 the Codec2 mode
  (3200, or 2400 plus FEC). Repeaters and hotspots pass it through, xlxd
  reflectors can transcode to AMBE.

Reference implementations:

- G4KLX MMDVM / MMDVMHost / DStarRepeater (GPL-2.0-or-later, compatible
  with OpenRTX): <https://github.com/g4klx/MMDVM> `DStarTX.cpp`,
  `DStarRX.cpp`, <https://github.com/g4klx/MMDVMHost> `DStarDefines.h`.
- dsdcc (GPL-3.0): D-STAR RX, header Viterbi, slow data.
- pydv: Codec2-in-D-STAR implementation.

## The vocoder problem

D-STAR uses DVSI AMBE, mode 3600x2400 (2400 bit/s voice plus 1200 bit/s
FEC). It is the older AMBE-2020 family, not the AMBE+2 used by DMR, so the
MD-380 ROM vocoder (md380-emu) cannot be used for it.

- **Patents:** the patents covering this AMBE generation are reported
  expired by 2017-2018 (Wikipedia citing Bruce Perens; community list at
  <https://www.qsl.net/kb9mwr/projects/dv/codec/patents.txt>). No statement
  from DVSI or a lawyer was found. Which patents cover D-STAR AMBE exactly
  is **[unverified]**. The algorithm and tables are not published.
- **Open implementations:** mbelib (ISC) decodes only, with poorer quality
  than the chip, and its README warns about patents. OP25's "max" branch
  has a software encoder whose D-STAR quality its authors call "only fair";
  it targets PCs.
- **Project stance:** maintainers have said, about DMR's AMBE+2, that it
  needs a DVSI agreement and that OpenRTX focuses on free protocols such as
  M17 (<https://github.com/OpenRTX/OpenRTX/issues/33>, #435). Nothing was
  found about D-STAR specifically.
- **Prior attempt:** kd0oss's PRs #311/#312 (closed by the author, not
  reviewed) ran the AMBE side on a PC with an AMBE dongle over USB, because
  the Module17 MCU was not fast enough for encoding
  (<https://kd0oss.blogspot.com/2025/01/module17-and-openrtx-modifications-part.html>).

Options:

1. **Codec2 only (SV9OAN extension).** Clean, fits the existing codec:
   Codec2 3200 is 64 bits per 20 ms, which fits the 72-bit slot, and
   `audio_codec.c` already handles 8-byte Codec2 3200 frames. Talks to other
   OpenRTX/pydv stations, and to AMBE users through a transcoding
   reflector. Cannot talk directly to a stock D-STAR radio.
2. **External AMBE-3000 (ThumbDV or similar)** over UART/USB, Module17
   first. Interoperable and legally clean for the project, but needs extra
   hardware and a new codec backend for 9-byte frames.
3. **Software AMBE.** Interoperable without extra hardware, but no
   decent-quality open encoder exists, CPU cost on Cortex-M4 is unknown,
   and the legal position is unclear. Not recommended without a project
   decision.

Receive-only AMBE decoding (option 3 for RX only) is the same legal
question as option 3 and is not a shortcut.

### Chosen: software AMBE with mbelib-neo, behind a build option

D-STAR support is built only with `meson setup -Ddstar=true`, which defines
`CONFIG_DSTAR` on the targets with a baseband path (MD-3x0, MD-UV3x0,
DM-1701, Module17, CS7000, CS7000-PLUS, linux). Default builds are unchanged.

The vocoder is [mbelib-neo](https://github.com/arancormonk/mbelib-neo)
(GPL-2.0-or-later, with ISC and PFFFT notices retained), pulled as a meson
subproject pinned to a commit, together with its PFFFT dependency. Only the
AMBE/IMBE code is built, not its TETRA decoder (unresolved licensing). It
provides both directions for D-STAR: a 3600x2400 encoder (D-STAR bit
layout cross-checked by its authors against MMDVM and DVSI test vectors,
on-air interoperability not yet verified) and the decoder, plus the
packing to and from the 9 bytes of a DV frame. `core/dstar_vocoder.h` wraps
it with separate encoder and decoder objects.

mbelib-neo's own patent review (`legal/patents.md`) lists records it has not
cleared, among them US 8,595,002 (related half-rate family) which its
encoder cites. Keeping the feature off by default leaves that decision to
whoever builds it.

To be measured on hardware: CPU load of encoding and decoding on a
Cortex-M4 (mbelib-neo is float based, scalar FFT), stack use in the codec
thread (16 KB) and heap use (each parameter set is about 2.6 KB, plus the
encoder's analysis state and FFT plans).

## Fit with the OpenRTX code base

### Hardware

D-STAR needs the same MCU baseband path as M17: modulator samples out to the
RF chip's modulation input, discriminator samples in. That limits it to the
`CONFIG_M17` targets: MD-3x0, MD-UV3x0, DM-1701, Module17, CS7000,
CS7000-PLUS, C62 (on its branch) and linux. GDx, MD-9600 and RT-4D have no
baseband path. On ttwrplus no MCU baseband path was found
(`audio_ttwrplus.c` has no devices), so it is excluded until checked.

Each target's radio driver configures the RF chip per opmode (deviation,
filters, bandwidth). A D-STAR case is needed in every one of them
(`radio_MD3x0.cpp`, `radio_UV3x0.cpp`, `radio_CS7000.cpp`,
`radio_C62.cpp`, `radio_linux.cpp`, ...), with deviation calibrated for
GMSK at ±1.2 kHz rather than M17's 4FSK.

### Reuse from M17 (`openrtx/src/protocols/M17`)

Reusable almost as is:

- `Correlator`, `Synchronizer`, `ClockRecovery`: templated on sync size and
  samples per symbol. 4800 symbols/s gives the same 10 samples/symbol at
  48 kHz TX and 5 samples/symbol at 24 kHz RX as M17.
- `DevEstimator`, the DC blocker and decimator in `openrtx/include/core/dsp.h`,
  `Fir<N>`, `Iir<N>`, the double-buffered audio stream handling of
  `Modulator`/`Demodulator`.

Needs new code or generalisation:

- Gaussian pulse shaping instead of RRC, 2-level symbol mapping and slicing
  (M17 uses ±1/±3).
- `Viterbi.hpp` is hard-wired to K = 5, G = 0x19. It should be generalised
  over K and generators (or a K = 3 instance added) for the header.
- Header interleaver, scrambler, CRC-CCITT, slow data mux/demux, header
  and superframe assembly.

Proposed layout, following the OSI split used for M17:

```
openrtx/include/protocols/DSTAR/   openrtx/src/protocols/DSTAR/
    Constants.hpp       sync patterns, frame sizes
    Header.hpp/.cpp     41-byte header, CRC, field access
    HeaderCoder.hpp     K=3 conv. code, interleaver, scrambler (encode/decode)
    SlowData.hpp/.cpp   message, header repeat, GPS mux/demux
    FrameEncoder/Decoder  superframe assembly
    Modulator/Demodulator GMSK at 48k/24k
openrtx/src/rtx/OpMode_DSTAR.cpp
```

### Opmode, codec, UI and codeplug

- `enum opmode` (`openrtx/include/rtx/rtx.h`) gains `OPMODE_DSTAR`;
  `rtxStatus_t` gains MY/UR/RPT1/RPT2/suffix and the received message.
- `OpMode_DSTAR` follows `OpMode_M17` (OFF/RX/TX state machine, audio
  paths, `codec_*` calls). Squelch would open on a valid header CRC.
- `audio_codec.c` is fixed to Codec2 3200 with 8-byte frames. Option 1
  works with it as is; options 2 and 3 need a codec backend abstraction and
  9-byte frames.
- Every `OPMODE_*` switch needs a D-STAR case: `rtx.cpp`, `ui.c` (mode
  cycling, destination edit), `ui_main.c`, `ui_menu.c`, module17 UI,
  `voicePromptUtils.c`, `cps.c`, `cps_io_*` and the radio drivers.
- `channel_t` (`openrtx/include/core/cps.h`) needs a `dstarInfo_t` with
  RPT1/RPT2 (16 bytes); this grows the union beyond the current 59-byte
  channel, or repeaters are stored by index. `settings_t` needs MYCALL,
  suffix, URCALL and the 20 character message; it is packed and persisted,
  so this changes the stored layout.
- New `SETTINGS_DSTAR` menu; new UI strings appended at the end of
  `stringsTable_t` only.

## Suggested plan

Each step can be merged and tested on its own.

0. **Decide the vocoder strategy with the maintainers** before any opmode
   or UI work. The protocol work below is useful whatever the decision.
1. **Protocol layer, host only.** Header, CRC, header coder, slow data,
   superframes, as pure C++ with Catch2 tests. Test vectors generated from
   MMDVM/dsdcc and, ideally, captures from a real D-STAR radio. Generalise
   `Viterbi.hpp` first, as its own commit, with the existing M17 tests
   guarding it.
2. **GMSK modem.** Modulator and demodulator, tested on linux with recorded
   baseband (like the M17 replay test), then on Module17, against an
   MMDVM hotspot or a stock radio receiving headers and slow data (no
   vocoder needed to check this).
3. **`OpMode_DSTAR` with Codec2 (option 1)**, UI and codeplug changes.
   Test against pydv or a Codec2-aware hotspot/xlxd reflector.
4. **AMBE**, only if the maintainers agree: external AMBE-3000 on Module17
   first (option 2).

Open questions to settle against the JARL spec: BT and deviation, header
field order, scrambler polynomial, slow data types, and the exact
interleaver table.
