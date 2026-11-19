<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# DMR in OpenRTX

Status: DMR direct mode (DMO) voice, implemented behind the `dmr` meson
option and the `C62_DMR` CMake option, not yet tested on air.

## Scope

Supported:

- Tier II direct mode voice, simplex: group and private calls, colour code,
  24 bit source and destination IDs;
- reception from the voice LC header or, joining late, from the embedded
  link control of voice bursts B to E;
- interoperability targets: radios in direct mode (talkaround) and MMDVM
  hotspots in simplex mode.

Not supported:

- repeater operation: TDMA slot synchronisation to a repeater downlink and
  burst transmission in a given slot. A direct mode transmission here is
  continuous: one timeslot carries the bursts, the other one the idle
  pattern (as MMDVM's DMO transmitter does), so the radio never has to key
  its transmitter on and off every 30 ms;
- data, SMS, CSBK, privacy, talker alias, GPS. The channel timeslot is not
  used in direct mode;
- the DMR chips of the MD-3x0, MD-UV3x0 and GD-77 families (HR_C5000,
  HR_C6000): DMR runs in software on the M17 baseband path instead.

## Implementation

| Part | Files |
|---|---|
| Vocoder (mbelib-neo, AMBE+2) | `core/dmr_vocoder.h`, codec thread (`audio_codec.c`, `CODEC_DMR_AMBE`) |
| Constants, bit order | `protocols/DMR/Constants.hpp` |
| FEC: BPTC (196,96), Hamming, Golay (20,8), QR (16,7), RS (12,9) | `protocols/DMR/Fec.*` |
| Burst fields: sync, slot type, EMB, voice frames | `protocols/DMR/Burst.*` |
| Full and embedded link control | `protocols/DMR/Lc.*` |
| Call level encoder and decoder | `protocols/DMR/Voice.*` |
| 4FSK modem | `protocols/DMR/Modulator.*`, `Demodulator.*` |
| Operating mode | `rtx/OpMode_DMR.*`, `OPMODE_DMR` |
| Radio drivers | DMR uses the M17 baseband configuration |
| UI | mode `DMR` (macro 5), caller and destination display, Settings > DMR (ID, destination, call type, colour code) |

Layer 2 is ported from MMDVMHost and the modem from MMDVM (G4KLX,
GPL-2.0-or-later). The DMR interleave of the AMBE+2 frames comes from DSD
(ISC), as used by mbelib-neo's test vector import. The bursts built match,
bit for bit, the ones MMDVMHost builds for the same link control (see
`tests/unit/dmr_protocol.cpp`), and the AMBE+2 silence frame of MMDVMHost
decodes without errors.

Settings are stored in `settings_t` only when DMR is enabled, so default
builds keep their settings layout.

### Radio parameters

- **Source ID:** Settings > DMR > DMR ID.
- **Destination and colour code:** from the channel when it is a DMR
  channel of a vendor codeplug with a DMR contact, otherwise from Settings >
  DMR (destination, call type, colour code). Channels switched to DMR with
  the macro menu use the settings.
- **Reception:** calls with the receive colour code only; the main screen
  shows the caller and the talkgroup or called ID.

### Modem

- 4FSK at 4800 symbols/s, root raised cosine shaping with roll-off 0.2.
  Transmission at 48 kHz, deviation +/-648 and +/-1944 Hz, scaled like the
  M17 modulator (+/-800 and +/-2400 Hz). The same per-radio phase
  inversions as M17 are used.
- Reception at 24 kHz, 5 samples per symbol, as in MMDVM: DC removal,
  matched filter, sync correlation and decision levels taken from each sync.
  Bursts come back at the same position of the 60 ms TDMA frame; the
  synchronisation is lost after 13 frames without sync.
- Unlike MMDVM, the receiver reads the symbols between the samples, at the
  correlation peak of the last sync (quadratic interpolation). At 5 samples
  per symbol the sampling instants can be a tenth of a symbol away from the
  symbol centres, which with a roll-off of 0.2 gave about 1% of bit errors
  on a clean signal; with the interpolation a clean signal decodes without
  errors at any timing in the unit tests.
- Transmission: 100 ms preamble, two voice LC headers, voice superframes,
  one terminator with LC.
- The decoder keeps a jitter buffer of about 40 ms (DMR voice arrives three
  frames at a time, every 60 ms).

Still to verify on air: deviation and phase on each radio, interoperability
with DMR radios in direct mode and with MMDVM simplex hotspots, CPU load of
the AMBE+2 vocoder on Cortex-M4 and on the C62, stack and heap use.

## The vocoder

DMR uses DVSI AMBE+2, mode 3600x2450 (2450 bit/s voice plus 1150 bit/s
FEC), a later generation than the AMBE of D-STAR (see `docs/dstar.md`).

- **Implementation:** mbelib-neo (GPL-2.0-or-later), already used for
  D-STAR, provides an AMBE+2 encoder and decoder. Only its AMBE/IMBE code is
  built.
- **Patents:** mbelib-neo's own patent record (`legal/patents.md`, reviewed
  2026-10-17) maps its AMBE+2 encoder and decoder against **US 8,359,197**,
  shown "Active" with a projected expiry of **2028-05-20** in the patent
  database (not verified against the official register), and keeps US
  8,595,002 (related half-rate family) open. It states that AMBE+2 remains
  available "with open questions" and gives no clearance. This is a weaker
  position than D-STAR's AMBE, whose patents are reported expired.
- **Project stance:** the maintainers have said that DMR's AMBE+2 needs a
  DVSI agreement and that OpenRTX focuses on free protocols such as M17
  (<https://github.com/OpenRTX/OpenRTX/issues/33>).

For these reasons DMR support is off by default: `meson setup -Ddmr=true`,
or `-DC62_DMR=ON` on the Retevis C62, leaves the decision to whoever builds
it. Whether firmware built with it may be distributed is not settled here.

## Memory

On the C62, DMR adds about 42 KB of flash and 24 KB of static RAM. Nearly
all the RAM is the cosine table of mbelib-neo's AMBE+2 decoder
(`ambe2450_cache`), static on embedded targets where thread-local storage is
replaced by plain static variables. The DMR receiver and modulator allocate
about 5 KB and 4 KB of heap while the mode is active.

## Building

```bash
meson setup -Ddmr=true build      # Linux, MD-3x0, MD-UV3x0, Module17, ...
```

On the Retevis C62 (see `scripts/c62/README.md`):

```bash
docker run --rm -it --user "$(id -u):$(id -g)" \
    -v "$PWD":/work openrtx-c62 c62-build -DC62_DMR=ON
```

D-STAR and DMR can be enabled together; they share mbelib-neo.

## History

A first DMR operating mode was proposed in
<https://github.com/OpenRTX/OpenRTX/pull/102> (2022, MD-3x0 only, using the
HR_C5000), and closed by its author without being merged. This
implementation does not use the DMR chips.
