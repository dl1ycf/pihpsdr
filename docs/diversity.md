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
a band and **jump** when the band, antenna or attenuator changes.

The analysis discards its statistics and starts again on a change of
frequency, sample rate, mode, filter edges or any window setting (§4) —
but **not** on an antenna or attenuator change, which it does not watch.
There the estimate simply re-converges over a few time constants, which is
slower than a restart but arrives at the same place. **Restart averaging**
is the button for it if the wait is unwelcome.

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

`div_choose_nfft()` picks the transform length to land near the requested
bin width, so at the default **Resolution** of 12 Hz the block is 85.3 ms
at every sample rate:

| Sample rate | nfft | bin | block |
|---|---|---|---|
| 48 kHz | 4096 | 11.7 Hz | 85.3 ms |
| 96 kHz | 8192 | 11.7 Hz | 85.3 ms |
| 192 kHz | 16384 | 11.7 Hz | 85.3 ms |
| 384 kHz | 32768 | 11.7 Hz | 85.3 ms |

Asking for finer bins doubles nfft and therefore the block period. nfft is
capped at 65536, so 3 Hz bins are unavailable at 384 kHz; the status line
always shows the bin width actually achieved.

| Resolution | Block | 48 kHz | 96 kHz | 192 kHz | 384 kHz |
|---|---|---|---|---|---|
| 12 Hz | 85 ms | yes | yes | yes | yes |
| 6 Hz | 171 ms | yes | yes | yes | yes |
| 3 Hz | 341 ms | yes | yes | yes | capped at 6 Hz |

Finer bins lift a weak signal further out of the per-bin noise floor, which
is a different thing from turning Averaging up: averaging reduces the
variance of an estimate, resolution improves the SNR the estimate is made
from. The cost is responsiveness — at 3 Hz the weight settles in about two
seconds rather than half a second.

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

### Seeing where it is looking

The status line is held to a fixed 44 characters in four columns — what
is being measured, what the loop is doing, one detail belonging to the
mode, and the weight — in a monospace face, with a small margin at each
end. It is the widest thing in the dialog and so sets the minimum window
width, and building every line to the same length out of fields that
truncate as well as pad means nothing arriving at run time can widen it.
The predecessor was a printf per mode, the longest around a hundred
characters.

```
Win 12Hz  track  coh 100%     -2.1 dB   +32°
Car  3Hz* HOLD   +400000 Hz  -12.3 dB  +179°
RADE V1   LOCK   LSB 100%     -2.1 dB   +32°
```

A `*` on the first field means the window ran past the Nyquist limit for
this sample rate and was clamped. Under **Hold** the weight shown is the
one the loop has *tracked to*, not the one being applied — the sliders
show what is applied, and seeing the two apart is the point of the
control.

The analysis window is drawn on the RX panadapter as a translucent green
band, using the theme's "ok" accent at low alpha. It is drawn under the
spectrum trace, in the same place in the draw order as the notch shading.

This matters most for a window placed outside the passband, which is
otherwise completely invisible. In follow-filter mode the band lands
exactly on the filter shading, which is a convenient check that the
frequency reference is right.

In Carrier mode the band is the search region and a brighter vertical line
marks where the tracker has settled within it. In RADE passband mode it is
the modem band clipped to the filter, as measured; in RADE V1 it is the
whole modem band, because the pilot correlator taps the raw stream ahead
of WDSP and is not affected by the filter.

The frequency reference deserves a note, because both halves of it were
wrong here at different times. The window and the filter edges live in
WDSP's shifted frame, where the tuned signal sits at zero; the analysis
works on the tapped DDC buffer. The conversion is

    bin frequency = −(s + frame_off)

with `frame_off = vfo[0].offset`, less the CW sidetone in CWU and plus it
in CWL.

The **displacement** is stated by two independent places in the code: the
panadapter's own filter overlay, and WDSP's notch database, which compares
absolute RF notch frequencies against `flow + tunefreq + shift`. It is
also the only arrangement that puts the CW passband on the dial frequency.

The **inversion** is not derived, it is measured. The tapped buffer runs
backwards with respect to RF: a signal above the dial appears at a
negative complex frequency in it. Three on-air observations say so — the
wideband RADE mode finding an LSB modem's energy at positive bin
frequencies, the V1 correlator locking the un-mirrored pilot bank on an
LSB signal twice by a wide margin, and the plain operator's expectation
that LSB, having been inverted once by the transmitter, arrives here the
right way up. Reading the code does not give this answer; `wdsp/shift.c`,
`wdsp/analyzer.c` and the panadapter's pixel mapping cannot all three be
read consistently with one another, and the measurement does not care.

Neither error showed with a symmetric window, CTUN off and a phone mode,
which is most bench testing. What they broke was everything asymmetric: a
hand-placed window at +5 kHz measured −5 kHz, the RADE window sat on the
wrong sideband, and with CTUN on the analysis measured a window `2 ×
offset` away from the one drawn on the screen.

If this is ever revisited, revisit it with a signal: put a known carrier a
few kHz off the dial, run the Carrier reference, and see which way
`div_auto_carrier` moves.

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

**The window may be placed outside the passband.** That is often the better
way to cancel noise: measuring the noise on its own, clear of the wanted
signal, gives a cleaner estimate of the noise channel than measuring it
through the signal. It is also how you size a window to take in just the
mark and space tones of an FSK signal.

This works because the channel between the two antennas is flat over any
realistic frequency gap. What curves it is the differential delay between
the feedlines, and 20 dB of cancellation needs the phase right to about
5.7°:

| Δ delay | 1 kHz | 5 kHz | 10 kHz | 30 kHz |
|---|---|---|---|---|
| 10 m coax (50 ns) | 0.02° | 0.09° | 0.18° | 0.54° |
| 30 m coax (152 ns) | 0.05° | 0.27° | 0.55° | 1.64° |
| 100 m coax (505 ns) | 0.18° | 0.91° | 1.82° | 5.45° |

So measuring a few kHz away costs nothing. The limit is the Nyquist
frequency, ±half the sample rate: a window beyond it is pulled back to the
edge and the status line marks the first field with a `*`. Before that guard existed
a window at +30 kHz on a 48 kHz stream was silently measured at −18 kHz
instead.

#### Weighting

**Flat** sums the spectra over the window and divides, which makes the
answer a power-weighted average of `h(f)` — dominated by the loudest bins
whether or not the two antennas agree there, and diluted by noise-only
bins that add to the denominator but not the numerator.

**Coherence** weights each bin by its own magnitude-squared coherence, so
bins carrying something both antennas hear dominate and noise-only bins
fall out. This is what makes a wide window work on SSB voice, where the
energy moves about constantly and there is no carrier to sit on: set the
window to the whole passband and the estimator picks the bins worth using,
following the voice as it moves, instead of the operator hand-placing a
narrow window on the loudest point.

Measured on synthetic speech — one narrow formant wandering across the
passband, so only part of the window carries signal at any instant —
against the true channel:

| noise | flat gain err | flat phase | coherence gain err | coherence phase |
|---|---|---|---|---|
| 0.05 | 0.05 dB | 0.08° | 0.04 dB | 0.08° |
| 0.20 | 0.60 dB | 0.32° | 0.41 dB | 0.30° |
| 0.50 | 3.12 dB | 0.75° | **1.63 dB** | 0.78° |
| 1.00 | 8.78 dB | 1.37° | **4.30 dB** | 1.10° |

Coherence roughly halves the gain error wherever it matters, and the phase
is much the same either way — which is what the theory says, since the
noise-only bins bias the magnitude rather than the phase. Coherence is the
default; Flat is kept so the two can be compared on air.

### Carrier (AM/SAM)

The carrier bin only. The carrier is found from the spectrum: the peak bin
**inside the analysis window**, refined by parabolic interpolation on log
power across the three bins about the peak, then smoothed with the
Averaging control.

Because the search is confined to the window, a carrier other than the
primary can be tracked — and therefore nulled. Park a 1 kHz window on
+5 kHz and the primary carrier is outside the search entirely. The
panadapter shows the search region as a green band with a brighter line
where the tracker has settled.

**Window centre and width are modal.** The Window and Carrier references
each keep their own pair, so aiming the carrier tracker at a station 5 kHz
away does not destroy the window set up for wideband work; switching back
restores it. Both pairs persist.

Measured at 384 kHz, where a bin is 11.7 Hz wide: **0.03 Hz of error and
0.002 Hz rms of jitter** at 11 s averaging, holding at −6 dB carrier SNR.

It does not use WDSP's SAM PLL, which is deliberately set for fast
acquisition — 39.8 Hz natural frequency, ~25 Hz loop noise bandwidth,
around 7 Hz of jitter on a weak carrier. That is right for demodulating
SAM and about a hundred times wider than suits measuring a carrier that is
not going anywhere. Working from the spectrum also means this mode works
in plain AM, where the SAM PLL does not run at all.

### RADE passband

Places the window on the FreeDV RADE modem band, 750-2200 Hz, on the side
of the tuned frequency the **operator's passband** puts it — the midpoint
of the filter edges, which covers LSB, USB and the digital modes without a
mode table. The window is then clipped to the filter, so a narrower filter
narrows what is measured.

The passband decides it outright. Taking the stronger of the two sides by
energy, as an earlier version did, is a coin toss on a signal near the
noise floor — which is where RADE lives — so with no signal present the
window settled wherever noise put it and the green overlay could sit above
an LSB passband indefinitely. The energy comparison survives only for AM,
SAM and FM, where the passband straddles the carrier and says nothing.

Measuring the side the operator is not listening to is not a fallback
worth having: whatever is over there is a different signal, filtered out
downstream, and combining for it would optimise the array for something
that never reaches the audio.

Maximum ratio combining across the modem band; no QRM nulling.

### RADE V1 pilot (MVDR)

Correlates against RADE V1's known pilot — on the side of the tuned
frequency the operator's passband names, and only that side — to separate
the wanted signal from everything else, which allows the interference covariance to be
estimated separately and a null steered onto QRM rather than onto the
signal. Uses no transform at all. See [`diversity-rade.md`](diversity-rade.md).

Selecting either RADE reference sets the objective to **Sum**, since on
RADE the signal being pointed at is the wanted one.

---

## 6. Operator controls

| Control | Effect | Shown for |
|---|---|---|
| **Diversity Enable** | The whole feature, including the DDC re-plumbing | always |
| **Gain / Phase** (coarse, fine) | Manual weight; live when Auto is not driving, and under **Hold** | always |
| **Auto** | Off / Null / Sum — the objective | always |
| **Measure on** | Which reference (§5) | always |
| **Window follows RX filter** | — | Window |
| **Window centre / width** | The analysis window, or the carrier search region in Carrier mode. Kept separately per mode | Window (unticked), Carrier |
| **Resolution** | 12 / 6 / 3 Hz bins. Finer lifts weak signals out of the noise but halves the update rate each step | all but RADE V1 |
| **Weighting** | Flat or Coherence (see above) | Window, RADE passband |
| **Averaging** | 0.2-30 s. Time constant for the estimate | always |
| **Min coherence** | Below this the loop holds rather than adapts | all but RADE V1 |
| **Restart averaging** | Discards the accumulated statistics | always |
| **Hold** | Stops applying the loop's answer without stopping the loop | always |
| **Invert** | Swaps Null and Sum | always |

Rows that the selected reference cannot use are **hidden, not greyed
out**. The RADE references place their own window, so four rows never
applied to them; the pilot correlator uses no transform at all, so two
more do not either. Greying them left a tall dialog of mostly dead
controls.

### Hold

Stops the loop *applying* its answer. It keeps measuring, and the status
line keeps showing where it has got to, but the gain and phase controls
become the operator's meanwhile. Releasing puts the tracked answer in
place in one step rather than slewing to it.

That makes two things easy that were not: comparing the loop's answer
against a hand-set one on the same signal, and holding a good weight
through a period when the band is doing something the loop should not
follow, without losing the loop's progress.

It is an operating state rather than a setting, so it is not persisted,
and it is released when the dialog closes — there is no indicator for it
anywhere else, and a loop that had silently stopped applying anything
would be a mystery.

### Invert

Swaps Null and Sum, and **turns the weight in force through 180 degrees at
the same time**, whether or not the loop is currently applying anything
and whether or not Hold is set. It is the quickest way to tell whether the
array is pointed at the wanted signal or at the interference.

Both halves are needed. Changing the objective alone only takes effect
when the loop next produces a weight, and it may not be producing one: the
coherence gate can be holding, the RADE correlator can be frozen on a
fade, and under Hold nothing is applied at all. The control then changed
what was being computed while leaving the audio exactly as it was.

Under Hold it acts on the operator's own manual weight, which is the only
thing being applied then.

The objective combo takes the same path, so the button and the combo
cannot behave differently.

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
| Window | 0.2 % | 0.4 % | 0.9 % | 1.7 % |
| Carrier | 0.2 % | 0.4 % | 0.9 % | 1.3 % |
| RADE passband | 0.2 % | 0.4 % | 0.8 % | 1.4 % |
| RADE V1, **searching** | 4.7 % | 5.4 % | 4.9 % | 7.1 % |
| RADE V1, searching, AM passband | 6.2 % | 6.5 % | 7.8 % | 7.2 % |
| RADE V1, locked | 0.5 % | 1.0 % | 1.8 % | 3.6 % |

The two searching rows are the same work over one pilot bank and over
two. An SSB passband names the bank, so only one is searched; AM, SAM and
FM say nothing and cost both. The saving is less than half because the
decimator is a fixed cost that grows with the sample rate — by 384 kHz it
dominates and the difference nearly vanishes.

At Resolution settings finer than 12 Hz the per-block cost roughly doubles
with nfft, but so does the block period, so the cost per *second* is close
to unchanged.

Reading these:

- The transform modes scale with `nfft` and are cheap.
- **RADE V1 while searching is by far the peak load** and is nearly
  rate-independent, because acquisition works on the fixed 8 kHz decimated
  stream. It costs 3-4 points more than tracking does. On a Pi this is the
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
| **RADE V1 acquisition** | **1-5 s** of continuous signal |
| RADE V1 confirmation ("probation") | ~1 s of that |
| RADE V1 freeze when the pilot goes | ~1 s |
| RADE V1 lock drop | ~10 s |

RADE V1 acquisition is in two parts. The search scores its grid at 8, 16
and 32 passes of a 120 ms modem frame, with the threshold coming down as
the integration lengthens, so a strong signal is found in about a second
and a weak one in under four. What it finds is a *candidate*: the tracker
then follows that one timing and frequency for eight frames, applying its
ordinary per-frame test, and **produces no weight until it confirms**. A
false alarm therefore costs a second and never moves the combiner.

This replaced a scheme that re-ran the entire blind search three times
before declaring lock — `3 x 32 x 120 ms`, 11.5 s for every signal however
strong. Confirming the one cell we care about answers the same question
for a fraction of the cost. Measured: 2.2 s to lock on synthetic signals,
USB and LSB, with and without CTUN.

Once locked, the frequency is tracked from the pilot-to-pilot phase
advance with a ~2 s time constant, which removes the 5 Hz search-grid
quantisation and follows the few Hz per minute a station drifts.

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
| `test/diversity/` | Mode coverage, window placement and weighting, RADE acquisition, CPU benchmark |

---

## 10. Related

- [`diversity-guide.md`](diversity-guide.md) — **start here**: what the
  feature does and how to use it, with worked examples
- [`diversity-rade.md`](diversity-rade.md) — the RADE modes in detail
- [`diversity-dither-fix.md`](diversity-dither-fix.md) — a P2 bug found
  along the way, where ADC1 never received the dither/random setting
- [`diversity-auto-phasing.md`](diversity-auto-phasing.md) — design
  history, including the approaches that were tried and abandoned. Not a
  description of current behaviour
