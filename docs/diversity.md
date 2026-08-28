# Diversity in piHPSDR

How two-antenna diversity works, and how the automatic phasing added on top
of it functions.

This is the reference document. Two companions cover narrower ground:
[`diversity-rade.md`](diversity-rade.md) for the FreeDV RADE modes, and
[`diversity-auto-phasing.md`](diversity-auto-phasing.md) for the design
rationale and the ideas that were tried and discarded.

---

## 1. What the radio provides

Diversity needs two receive chains that are coherent — same clock, same
local oscillator, no relative drift — and configured identically. Both
HPSDR protocols provide that, and the phasing itself is done entirely in
the host, not in the FPGA.

### Protocol 2

`src/new_protocol.c` reconfigures the DDC map when diversity is enabled:

| | DDC0 | DDC1 | DDC2 | DDC3 |
|---|---|---|---|---|
| Normal RX | off | off | RX1 | RX2 |
| Diversity RX | ADC0, synced | ADC1, synced | off | off |

Byte 1363 of the receive-specific packet is DDC0's sync map, set to `0x02`,
which tells the firmware to lock DDC1 to DDC0 and merge them into a single
UDP stream: **119 interleaved sample pairs** per packet (I0 Q0 I1 Q1, 24
bits each) rather than 238 consecutive samples.

Both DDCs are given the same NCO frequency — the high-priority packet
copies bytes 9-12 into 13-16 — the same sample rate, the same band-pass
filter, the same step attenuator and the same dither/random setting.

The consequence is the one everything else rests on: **the two streams
have no relative sample delay and no relative drift**, so the channel
between them is a memoryless complex gain rather than a filter.

### Protocol 1

Equivalent: `how_many_receivers()` forces two HPSDR receivers, ADC0 is
wired to RX1 and ADC1 to RX2, the attenuators are tied together, and the
sample pairs arrive interleaved in the same frame.

### Front-end asymmetry

Worth knowing when interpreting results. On pre-Orion2 boards only ADC0's
path is under software control — ALEX high-pass, the TX low-pass when
using ANT1-3, and the ALEX attenuator. ADC1 is a bare rear-panel input.
So the relative gain and phase between the two antennas are stable within
a band and **jump** when the band, antenna or attenuator changes. The
analysis notices and starts again; see §4.

---

## 2. The combiner

Three lines, in `rx_add_div_iq_samples()` (`src/receiver.c`):

```c
double i_sample = i0 + (div_cos * i1 - div_sin * q1);
double q_sample = q0 + (div_sin * i1 + div_cos * q1);
```

That is `y = z0 + w·z1` with one complex weight
`w = 10^(div_gain/20)·e^{jφ}`, computed in `radio_calc_div_params()`
(`src/radio.c`).

Four properties follow, and they shape everything else:

- **The reference weight is fixed at 1.0.** Only the ratio is adjustable.
  SINR is invariant to a common scale so nothing is lost, but the output
  level moves as the weight adapts.
- **The weight is flat across the whole DDC passband.** It can align the
  two antennas exactly at one frequency only.
- **There is no output normalisation.** Two equal in-phase signals give
  +6 dB of signal and +3 dB of noise.
- **It is applied per raw IQ sample, ahead of WDSP** — before the noise
  blanker, before `fexchange0`.

RX1's panadapter shows the combined stream; RX2, if running, shows raw
ADC1.

---

## 3. Manual control

Unchanged from before this work. The Diversity menu has coarse and fine
gain (±25 dB / ±2 dB) and phase (±180° / ±5°) sliders, and there are
encoder actions (`DIV_GAIN`, `DIV_PHASE`) in `src/actions.c`. `div_gain`
is clamped to ±27 dB and `div_phase` wrapped to ±180°.

The sliders are live whenever the automatic loop is not driving the
weight, and grey out when it is.

---

## 4. The automatic loop

`src/diversity_auto.c`. The shape is:

```
protocol RX thread                    analysis thread
------------------                    ---------------
rx_add_div_iq_samples()
  |
  +-- diversity_auto_sample()  -->  queue (4 blocks)  -->  div_process_block()
  |     4 stores per sample                                  |
  |                                          window + 2 FFTs, or
  +-- z0 + w*z1  ------------------------->  RADE pilot correlation
        (applied immediately,                                |
         weight read from                              solve for w
         div_cos/div_sin)                                    |
                                              slew into div_cos/div_sin
```

The analysis never sits in the audio path. The weight is applied to every
sample exactly as it was before, and the loop just changes what that
weight is, roughly twelve times a second.

### Block cadence

`div_choose_nfft()` picks the transform length to land near a 12 Hz bin,
so the **block is 85.3 ms at every sample rate**:

| Sample rate | nfft | bin | block |
|---|---|---|---|
| 48 kHz | 4096 | 11.7 Hz | 85.3 ms |
| 96 kHz | 8192 | 11.7 Hz | 85.3 ms |
| 192 kHz | 16384 | 11.7 Hz | 85.3 ms |
| 384 kHz | 32768 | 11.7 Hz | 85.3 ms |

### The queue

The sample path fills one buffer and hands it to a four-deep queue. If the
worker falls far enough behind that the queue fills, the block is dropped
and the worker is told, because a gap in the stream invalidates RADE V1's
pilot timing and it has to re-acquire rather than track a pilot that has
silently moved. For the transform-based modes a dropped block costs
nothing but one block's contribution.

### Two objectives

Over the bins selected by the reference (§5), the loop accumulates the
cross spectrum and both auto spectra with exponential forgetting, then:

| Objective | Weight | Behaviour |
|---|---|---|
| **Null** | `w = −Sxy/Syy` | minimises `E\|z0 + w·z1\|²` — cancels whatever the two antennas have in common. Noise cancelling. |
| **Sum** | `w = +Sxy/Sxx` | equals `conj(h)` for `z1 = h·z0` — maximum ratio combining when both branches carry equal noise power. |

They use **different denominators**, so they are not simply sign-flipped —
though since `Sxx` and `Syy` are positive reals the two answers are
exactly 180° apart, differing only in magnitude by `Sxx/Syy`. Both come
from the same accumulators, so switching between them takes effect on the
next block and is applied without slewing.

Fit quality is the magnitude-squared coherence
`γ² = |Sxy|²/(Sxx·Syy)`. Below the **Min coherence** setting the loop
holds rather than chasing noise, and the status line says `HOLD`.

### Applying the weight

`div_apply_weight()` clamps `|w|` to +20 dB — inside the sliders' ±27 dB,
because a large weight means the aux antenna's own noise dominates the sum
and it costs headroom downstream. It then moves 15 % of the remaining
distance per block (about a 0.5 s time constant) and back-computes
`div_gain`/`div_phase` so the menu, the props file and remote clients stay
consistent with what is actually applied.

### Starting again

The analysis watches the tuned frequency, sample rate, mode, filter edges
and window settings, and throws away its accumulated statistics whenever
any of them change — rather than relying on call sites to notify it.

---

## 5. The four references

What part of the spectrum the decision is taken from. Selected by
**Measure on** in the Diversity menu.

### Window (wideband)

Every bin in the analysis window. The window either follows the RX filter
or is placed by hand with **Window centre** and **Window width**, in Hz
relative to the tuned frequency — the same reference the filter edges use.
Hand placement lets the window be parked on a known noise, or sized to
take in just the mark and space tones of an FSK signal.

### Carrier (AM/SAM)

The carrier bin only. The carrier is found from the spectrum: the peak bin
within ±500 Hz of the tuned frequency, refined by parabolic interpolation
on log power across the three bins about the peak, then smoothed with the
Averaging control.

Measured at 384 kHz, where a bin is 11.7 Hz wide: **0.03 Hz of error and
0.002 Hz rms of jitter** at 11 s averaging, holding at −6 dB carrier SNR.

It does not use WDSP's SAM PLL, which is deliberately set for fast
acquisition — 39.8 Hz natural frequency, ~25 Hz loop noise bandwidth,
around 7 Hz of jitter on a weak carrier. That is right for demodulating
SAM and about a hundred times wider than suits measuring a carrier that is
not going anywhere. Working from the spectrum also means this mode works
in plain AM, where the SAM PLL does not run at all.

### RADE passband

Places the window on the FreeDV RADE modem band, 750-2200 Hz, on whichever
side of the carrier actually carries it — measured each block by comparing
the energy either side, not derived from the mode. Maximum ratio combining
across the modem band; no QRM nulling.

### RADE V1 pilot (MVDR)

Correlates against RADE V1's known pilot to separate the wanted signal
from everything else, which allows the interference covariance to be
estimated separately and a null steered onto QRM rather than onto the
signal. Uses no transform at all. See [`diversity-rade.md`](diversity-rade.md).

Selecting either RADE reference sets the objective to **Sum**, since on
RADE the signal being pointed at is the wanted one.

---

## 6. Operator controls

| Control | Effect |
|---|---|
| **Diversity Enable** | The whole feature, including the DDC re-plumbing |
| **Gain / Phase** (coarse, fine) | Manual weight; live when Auto is not driving |
| **Auto** | Off / Null / Sum — the objective |
| **Measure on** | Which reference (§5) |
| **Window follows RX filter** | Window mode only |
| **Window centre / width** | Window mode with the above unticked |
| **Averaging** | 0.2-30 s. Time constant for the estimate, in every mode |
| **Min coherence** | Below this the loop holds rather than adapts |
| **Restart averaging** | Discards the accumulated statistics |

Settings persist in the props file as `diversity_auto_*` and are range
checked on restore.

The automatic loop runs on the radio side only. On a remote client the
combining happens on the server, so the auto controls are greyed out;
manual gain and phase still work and are sent over the wire.

---

## 7. CPU cost

Measured by `test/diversity/bench_cpu.c`, on a 12th Gen Intel i7-12700K.
The last column is the fraction of one core needed to keep up with the
85.3 ms block period. **A Raspberry Pi is several times slower at this
kind of scalar double-precision work, so scale accordingly.**

| Mode | 48 kHz | 96 kHz | 192 kHz | 384 kHz |
|---|---|---|---|---|
| Window | 0.2 % | 0.5 % | 1.1 % | 2.0 % |
| Carrier | 0.2 % | 0.5 % | 1.0 % | 1.8 % |
| RADE passband | 0.2 % | 0.5 % | 0.9 % | 1.8 % |
| RADE V1, **searching** | 7.7 % | 6.9 % | 7.5 % | 9.0 % |
| RADE V1, locked | 0.5 % | 1.1 % | 2.2 % | 3.7 % |

Reading these:

- The transform modes scale with `nfft` and are cheap.
- **RADE V1 while searching is by far the peak load** and is nearly
  rate-independent, because acquisition works on the fixed 8 kHz decimated
  stream. It costs 5-6 points more than tracking does. On a Pi this is the
  number to watch: it is the state the engine sits in whenever there is no
  RADE signal to lock to.
- RADE V1 once locked scales with the sample rate, because what remains is
  the decimator.

On the protocol receive thread the added cost is four float stores per
sample pair plus one mutex acquisition per block — well under 1 % of a
core at 384 kHz, and **no added audio latency**.

Run it yourself with `make -C test/diversity bench`.

---

## 8. Timings

| Event | Time |
|---|---|
| Weight slew | ~0.5 s |
| Estimate settling | the Averaging control, 0.2-30 s |
| **RADE V1 acquisition** | **~11.5 s** of continuous signal |
| RADE V1 freeze when the pilot goes | ~1 s |
| RADE V1 lock drop | ~10 s |

RADE V1 acquisition is the slow one and deserves explanation: declaring
lock needs `RADE_LOCK_FRAMES` (3) consecutive evaluations, each
integrating `RADE_ACQ_PASSES` (32) passes of one 120 ms modem frame. That
is deliberately conservative — a false lock steers the array at noise —
but 11.5 s of uninterrupted signal is a long time in a conversational
mode, and it is the first thing to revisit if acquisition proves too slow
in practice.

---

## 9. Files

| File | Role |
|---|---|
| `src/diversity_auto.c`, `.h` | The engine: tap, queue, worker, transform modes, weight |
| `src/rade_correlator.c`, `.h` | RADE V1 pilot correlation and MVDR |
| `src/diversity_menu.c` | Controls and status |
| `src/receiver.c` | The combiner, and the tap into it |
| `src/radio.c` | Start/stop, props, shutdown |
| `src/new_protocol.c` | P2 DDC pairing and ADC configuration |
| `test/diversity/` | Mode coverage test and the CPU benchmark |

---

## 10. Related

- [`diversity-rade.md`](diversity-rade.md) — the RADE modes in detail
- [`diversity-auto-phasing.md`](diversity-auto-phasing.md) — design
  rationale, and the approaches that were tried and abandoned
- [`diversity-dither-fix.md`](diversity-dither-fix.md) — a P2 bug found
  along the way, where ADC1 never received the dither/random setting
