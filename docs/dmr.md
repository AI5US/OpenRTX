<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# DMR in OpenRTX

Status: DMR voice in direct mode (DMO) and through repeaters, implemented
behind the `dmr` meson option and the `C62_DMR` CMake option. Direct mode
is tested on air on the Retevis C62, both ways with a DMR radio (see below);
repeater mode is checked on the bench only, against off-air recordings, and
waits for a test with a repeater (see [Repeater mode](#repeater-mode)). Not
yet tested on the other radios.

## Scope

Supported:

- Tier II direct mode voice, simplex: group and private calls, colour code,
  24 bit source and destination IDs;
- reception from the voice LC header or, joining late, from the embedded
  link control of voice bursts B to E;
- Tier II repeater voice: reception of the selected timeslot of a repeater
  downlink, transmission in the same timeslot of the uplink, repeater
  wake-up;
- interoperability targets: radios in direct mode (talkaround), MMDVM
  hotspots in simplex and duplex mode, repeaters.

A direct mode transmission is continuous: one timeslot carries the bursts,
the other one the idle pattern (as MMDVM's DMO transmitter does), so the
radio never has to key its transmitter on and off every 30 ms. Through a
repeater the radio must leave the other timeslot free, see below.

Not supported:

- data, SMS, privacy, talker alias, GPS; CSBKs other than the repeater
  wake-up;
- channel access rules: a transmission does not wait for the timeslot to be
  free;
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
| Repeater signalling: CACH, BS activation CSBK | `protocols/DMR/Cach.*`, `Csbk.*` |
| 4FSK modem | `protocols/DMR/Modulator.*`, `Demodulator.*` |
| Operating mode | `rtx/OpMode_DMR.*`, `OPMODE_DMR` |
| Radio drivers | DMR uses the M17 baseband configuration; `radio_setPa()` keys the PA around repeater bursts (`CONFIG_RADIO_PA_KEYING`) |
| UI | mode `DMR` (macro 5), caller and destination display, Settings > DMR (ID, destination, call type, colour code, timeslot, TX timing) |

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
- **Direct or repeater mode:** repeater mode when the transmit and receive
  frequencies differ, direct mode otherwise.
- **Timeslot (repeater mode):** from the channel when it is a codeplug DMR
  channel with a DMR contact, otherwise Settings > DMR > Timeslot.
- **TX timing (repeater mode):** Settings > DMR > TX timing, in symbols
  (208 us), positive to transmit earlier. A trim on top of the target's
  calibration, 0 by default.

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

### On-air test on the Retevis C62

Tested with a DMR radio in direct mode (TG 9, colour code 1), receiving and
transmitting, and checked with off-air recordings (RTL-SDR, FM discriminator
demodulated offline, then decoded with this receiver and vocoder):

- deviation: symbol unit 678 Hz against 648 Hz nominal (5% high, as the
  D-STAR deviation, so a property of the shared M17 level calibration);
  outer/inner level ratio 3.0 to 3.1, the four levels equally spread;
- after the fixes below, voice syncs exactly 360 ms apart over the whole
  transmissions, 0.03 to 0.06 corrected bit errors per AMBE+2 frame and
  under 1% of frames with more than 3 errors.

What it took, worth knowing for other radios:

- a voice burst needs three speech frames: the transmitter waits for five
  queued frames (sending more headers meanwhile) before the first burst, so
  that the frames of a burst are always ready when it is built;
- the baseband output must never miss a block: a replayed 20 ms block shifts
  every following burst, and receivers then play the bursts up to the next
  voice sync as corrupted frames. M17 and D-STAR receivers ride over such a
  slip, DMR receivers do not. On the C62 the rtx thread had to run above the
  UI (screen refresh) and codec threads;
- the AMBE encoders need about 35 KB of stack and 42 KB of heap, and compute
  in single precision on Cortex-M (`mbe_float_math.h`): in double precision
  they were too slow for real time on the C62's single precision FPU.

Still to verify on air: the other radios (deviation, phase, RAM: the MD-3x0
may not have room for the 48 KB codec stack), MMDVM simplex hotspots.

## Repeater mode

### How it works

- **Reception.** The receiver looks for the base station syncs, follows a
  burst every 30 ms and reads the timeslot of each one from the TACT of the
  CACH before it (Hamming (7,4), one error corrected). As two errors go
  undetected, the timeslot must alternate: a single mismatch is overruled,
  two in a row are believed. Only the selected timeslot is decoded and
  played back. Each burst carries its receiver sample index, which the
  OpMode turns into system time with a running estimate of the offset
  between sample count and system tick (lowest offset seen, which has the
  shortest input latency, rising slowly to follow the drift of the clocks).
- **Uplink timing.** As in MMDVM (`DMRTX.cpp createCACH`), the inbound
  timeslot n starts when the downlink burst of timeslot n ends; the uplink
  burst starts 1.25 ms later, after the guard time. The radio is half
  duplex, so the timing comes from the last downlink burst heard before the
  transmission (at most 1 s old) and holds through it on the radio's clock:
  at 50 ppm a minute long transmission drifts by 3 ms, inside the 18 symbol
  window of MMDVM.
- **Wake-up.** With no downlink in the last second, the radio sends two BS
  activation CSBKs (opcode 0x38, MS data sync, CRC mask A5A5), listens
  500 ms for the downlink, and tries three times before giving up until the
  PTT is released.
- **Transmission.** Voice LC headers, superframes and terminator as in
  direct mode, with MS syncs, one burst every 60 ms in the selected
  timeslot (`Modulator::sendSlot()`).
- **PA keying.** On the targets defining `CONFIG_RADIO_PA_KEYING` (the C62)
  the PA is on only around the bursts, 5 symbols (about 1 ms) before and
  after each one; the others send carrier through the other timeslot, which
  blocks the users of that timeslot on a shared repeater. The switching is
  done by the transmitting thread itself, from a modulator callback that
  runs for each 20 ms block of baseband while it plays, once the next block
  is ready: it can sleep until a switching instant inside the block without
  starving the output stream, it follows the output samples rather than a
  separate clock, and it costs no thread (the C62 has all of its 5 POSIX
  threads in use). The same schedule would be the place to switch to
  reception between the bursts, see below.
- **PTT.** A transmission ends on two released PTT readings in a row, one
  per burst: on the C62, with the PA keyed, single false readings ended
  transmissions after a few seconds.

### Calibration

Two delays of the radio itself place the uplink bursts; the repeater's own
timing is followed from the downlink.

| Delay | Where | C62 |
|---|---|---|
| Baseband output stream to antenna (beyond the 20 ms block of silence the stream starts with) | `CONFIG_BASEBAND_TX_DELAY_US` in the target's `hwconfig.h`; places the PA keying and the bursts | 26.5 ms, the DSP buffering: measured off air, see below |
| Antenna to receiver sample count | TX timing setting, for now | not measured |

The receive delay can only be measured against a downlink: record a duplex
hotspot or repeater downlink and the radio's uplink in one RTL-SDR capture
(TX and RX frequencies less than about 0.9 MHz apart at 960 kS/s), and
compare where the uplink bursts start with the end of the downlink bursts of
the same timeslot (target: 6 symbols later). Once measured it should become
a target constant as well, with the setting left as a trim.

### Bench test on the Retevis C62

Without a repeater, checked with RTL-SDR recordings demodulated and decoded
offline, using a test build that transmits after unanswered wake-ups:

- the wake-up CSBKs decode with a valid CRC, opcode, source ID and colour
  code, three attempts about 0.75 s apart;
- voice header, 60 ms spaced voice bursts and terminator decode, 0.12
  corrected bit errors per AMBE+2 frame, under 0.5% of frames with more than
  3 errors, with the PA keyed;
- with carrier fill each 60 ms holds 28 ms of modulation; with the PA keyed
  the PA is on for 29 to 30 ms every 60 ms. With no output delay set the PA
  windows fell exactly between the bursts: the baseband reaches the antenna
  about 27 ms after the stream plays it. Measured from the position of the
  bursts inside the windows, the delay is now set to 26.5 ms; the last
  recording, at 27.5 ms, had the windows open about 0.9 ms late.

The PA switching follows the system tick, 1 ms, which gives the windows a
jitter of about +/-1 ms: hence the 1 ms margins, against 1.25 ms of guard
time.

### Open items

- **Test with a duplex MMDVM hotspot or a repeater:** acceptance of the
  uplink bursts and of the wake-up, playback of a timeslot, and the
  receive delay calibration above.
- **Microphone level:** listeners report DMR transmissions from the C62 as
  too loud. Off air the decoded speech measured -38 dBFS RMS and -17 dBFS
  peak with mbelib-neo, which is unity gain from encoder to decoder; but
  listeners decode with DVSI vocoders, and the C62's software microphone
  gain (`CONFIG_MIC_GAIN`, 13) was set for Codec2 and applies to the AMBE
  encoders as well. mbelib-neo scales its AMBE+2 encoder to DVSI's level
  (`AMBE2450_ENC_MAG_SCALE`); the fix is probably a separate encode gain
  for the AMBE codecs, measured against a DVSI decoder.
- **PTT readings:** whether the two reading confirmation is enough; the
  last keyed bench transmission still ended after 5.9 s, which may have
  been the release.
- **Reception between the bursts:** while transmitting, the downlink burst
  of the radio's own timeslot (the repeater relaying it) arrives while the
  radio is idle. Receiving it would keep the timing through long
  transmissions and show the repeater's state; it needs the BK4819 to
  retune between the TX and RX frequencies within the guard time (the
  driver waits 5 ms now) and the C62 to run baseband input and output at
  the same time.
- **Channel access:** wait for the timeslot to be free (CACH AT bit, or no
  call on the downlink) before transmitting.

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

On the Retevis C62 (see `platform/targets/c62/README.md`):

```bash
west build -b c62 -d build . -- -DC62_DMR=ON
```

D-STAR and DMR can be enabled together; they share mbelib-neo.

## History

A first DMR operating mode was proposed in
<https://github.com/OpenRTX/OpenRTX/pull/102> (2022, MD-3x0 only, using the
HR_C5000), and closed by its author without being merged. This
implementation does not use the DMR chips.
