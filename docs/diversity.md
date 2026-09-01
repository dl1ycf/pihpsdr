# Diversity in piHPSDR

How two-antenna diversity works, and how the automatic phasing added on top
of it functions.

This is the reference document. Four companions cover narrower ground:
[`diversity-rade.md`](diversity-rade.md) for the FreeDV RADE V1 correlator,
[`diversity-digital-iq-proposal.md`](diversity-digital-iq-proposal.md)
for the FSK/DIgital reference,
[`diversity-auto-phasing.md`](diversity-auto-phasing.md) for the design
rationale and the ideas that were tried and discarded, and
[`diversity-measurements.md`](diversity-measurements.md) for what all of
it measurably does on recorded on-air signals.

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
filter and the same dither/random setting. The step attenuator is shared
too, unless the operator has asked for separate ones; see "Separate
attenuators" below.

The consequence is the one everything else rests on: **the two streams
have no relative sample delay and no relative drift**, so the channel
between them is a memoryless complex gain rather than a filter.

### Protocol 1

Equivalent: `how_many_receivers()` forces two HPSDR receivers, ADC0 is
wired to RX1 and ADC1 to RX2, the attenuators are tied together by
default, and the sample pairs arrive interleaved in the same frame.

### Front-end asymmetry

Worth knowing when interpreting results. On pre-Orion2 boards only ADC0's
path is under software control — ALEX high-pass, the TX low-pass when
using ANT1-3, and the ALEX attenuator. ADC1 is a bare rear-panel input.
So the relative gain and phase between the two antennas are stable within
a band and **jump** when the band, antenna or attenuator changes.

The analysis discards its statistics and starts again on a change of
frequency, sample rate, mode, filter edges, any window setting (§4) or
either ADC's step attenuator — but **not** on an antenna change, which it
does not watch. There the estimate simply re-converges over a few time
constants, which is slower than a restart but arrives at the same place.
**Restart averaging** is the button for it if the wait is unwelcome.

### Separate attenuators

The two step attenuators are tied together while diversity runs, both
protocols sending ADC0's value to ADC1 as well. That is the safe default,
because an attenuator change moves the relative gain between the arms and
so invalidates whatever weight is in force — including a manual one the
operator set by hand.

**ADC attenuators**, the tick box beside **Diversity** at the top of the
menu, unties them, and puts an **Attenuator (dB)** row with a value for
each ADC underneath — a row that is there only while they are split. The
reason to want it is headroom on one antenna alone: a local source strong
enough to overload ADC0 that the second antenna cannot hear at all
(measured at 10.5 dB above the floor on ADC0 only — Finding 5 of
[`diversity-measurements.md`](diversity-measurements.md)) can then be
attenuated where it is, instead of costing the quiet antenna the same
10 dB of sensitivity it did not need to lose.

Untied, the step is not simply allowed through. The weight is a ratio, so
a known change of *d* dB on one arm has a known effect on it: ADC1 moving
by *d* raises the correct weight by *d*, ADC0 moving by *d* lowers it by
the same. That correction is applied to `div_cos`/`div_sin` at the instant
the attenuator moves, so the combined audio does not step, and a manual
gain and phase stay valid across the change. The measurement itself
restarts, since both attenuations are part of the analysis context.

This applies to any path that moves an attenuator — the ATT slider, an
encoder, CAT, or the two spin buttons in the Diversity menu, which are
the only way to reach ADC1 while the loop is running and has made RX1 the
active receiver.

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

### Three objectives

Over the bins selected by the reference (§5), the loop accumulates the
cross spectrum and both auto spectra with exponential forgetting, then:

| Objective | Weight | Behaviour |
|---|---|---|
| **Null** | `w = −Sxy/Syy` | minimises `E\|z0 + w·z1\|²` — cancels whatever the two antennas have in common. Noise cancelling. |
| **Sum** | `w = +Sxy/Sxx` | equals `conj(h)` for `z1 = h·z0` — maximum ratio combining when both branches carry equal noise power. |
| **Best** | `w = 0` or `w` at the clamp, co-phased | gives the output to whichever antenna is measuring better, rather than combining them. |

Null and Sum use **different denominators**, so they are not simply
sign-flipped — though since `Sxx` and `Syy` are positive reals the two
answers are exactly 180° apart, differing only in magnitude by `Sxx/Syy`.
Both come from the same accumulators, so switching between them takes
effect on the next block and is applied without slewing.

**Best** is a selection rather than a third formula. It acts on the
per-antenna SNR estimate `div_auto_arm_db` — each arm's signal measured
against its *own* noise floor, so an antenna that is 12 dB down because it
is deaf is distinguished from one that is 12 dB down because it is quiet.
Every reference computes it. Whichever arm is ahead is used alone, with
1 dB of hysteresis so a marginal difference does not chatter.

Selecting arm 1 is not directly expressible: the combiner forms
`z0 + w·z1` with arm 0 pinned at unity gain, so "arm 1 only" exists only
as the limit `w → ∞`. The nearest reachable point is `w` at the loop's own
`DIV_MAX_WEIGHT` clamp with the co-phasing angle — +20 dB, inside the
sliders' ±27 dB, so arm 1 is dominant with arm 0 co-phased in underneath
it 20 dB down. That residue is not a compromise: measured
against a decoder it beat the full MVDR solve by 0.6 dB on the capture
where the two antennas disagreed about which was better, because arm 0 is
still doing useful combining. Selecting arm 0 needs no such trick —
`w = 0` is exact.

If the estimate is unavailable the loop **holds** rather than falling back
to arm 0, which would silently turn the mode into "diversity off" whenever
the floor could not be measured. Because Best selects rather than steers,
**Invert** does not apply to it and the button is greyed out.

Fit quality is the magnitude-squared coherence
`γ² = |Sxy|²/(Sxx·Syy)`. Below the **Min coherence** setting the loop
holds rather than chasing noise, and the status line says `HOLD`.

### Noticing that the signal has stopped

Coherence alone does not notice. The accumulators forget exponentially,
so when a transmission ends `Sxy`, `Sxx` and `Syy` all decay *together*
and `γ²` stays near 1 the whole way down — a 30 dB signal at the default
2 s averaging kept the loop reporting `track` for 5.8 time constants,
about **twelve seconds**, adjusting the weight on noise the entire time.
Once per gap, on every mode that has gaps.

So each block also compares this block's power, over the same bins and
with the same weights the estimate uses, against the smoothed power
accumulated over them. More than 10 dB apart and the statistics no longer
describe what is on the air: the loop holds, and the weight stays at the
last value measured on a real signal — which is what is wanted across a
gap. Measured: `track` to holding in **one block**, with the weight
unmoved.

The test scales itself. It fires on a signal well out of the noise, which
is exactly where stale statistics do the most harm, and stays quiet on a
weak one, where they hold little signal to be stale about. 10 dB is
comfortably past ordinary fading and far short of a signal stopping, and
holding through a deep fade is wanted anyway. It is one-sided, so a
signal *starting* never trips it.

It holds rather than flushing. The accumulators go on decaying at the
operator's averaging time either way, but flushing would put the loop one
block from the start, where the single-block cross spectrum is perfectly
coherent by construction and any bin at all looks like a signal.

**This matters most on CW**, where the signal is absent for most of a
transmission rather than only between them. The loop previously spent
every key-up period walking the weight around on noise; it now measures
only while there is something to measure, so the estimate is built from
key-down periods alone.

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
Dig 12Hz  track  occ  293Hz   -2.1 dB   +38°
Dig 12Hz  search no signal    +0.0 dB    +0°
```

Below it sits a second, single-field line reporting which antenna is
measuring better and by how much, on the same fixed width:

```
Antennas  measuring
Antennas  ADC1 better by  3.4 dB
Antennas  ADC0 better by 12.1 dB  using ADC0
```

It is shown whatever objective is running, because nothing else an
operator can see distinguishes a deaf antenna from a quiet one and the two
want opposite weights. `measuring` means the estimate is not yet available
— it needs a signal standing clear of the noise floor on *both* arms. The
trailing `using ADCn` appears only under **Best**, and is what the
selection has actually settled on. A remote client reads the same three
lines as the radio: `div_auto_arm_db`, `_arm_valid` and `_arm_pick` all
arrive in `INFO_DIVERSITY`, so `div_arm_status_set()` needs no
remote-aware case of its own.

Ie FSK/DIgital the third field is the width of what was found occupied,
which is checkable against the darker band on the panadapter, and
`search` means the region is in the right place but empty - as against
`wait`, which means something was found and then rejected as incoherent.

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
marks where the tracker has settled within it. Ie FSK/DIgital it is again
the search region, with the bins found occupied shaded more strongly over
the top, so the operator's setting and the measurement are both visible -
seeing them disagree is how a region placed on the wrong thing shows
itself. In RADE V1 it is the whole modem band, because the pilot
correlator taps the raw stream ahead of WDSP and is not affected by the
filter.

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

`div_context_changed()` watches the tuned and CTUN frequencies, the CTUN
offset, the CW sidetone, the sample rate, the mode, both filter edges, and
every window setting — the reference, the follow tick, centre, width and
weighting. Any change throws the accumulated statistics away, rather than
relying on call sites to notify it.

The three frequencies carry a tolerance. Below `DIV_RETUNE_HZ` (20 Hz,
cumulative since the last reset, not per block) nothing is discarded: what
the estimate describes is the pair of antennas and the path, and that does
not change because the dial moved a few hertz, while a retune small enough
to leave the same signal in the window is not a reason to start again. 20 Hz
is under a tenth of the narrowest CW filter and inside the ±60 Hz the RADE
correlator tracks, so a lock survives it; tuning across a band to another
station moves kilohertz and still resets. See Finding 15's neighbourhood in
[`diversity-measurements.md`](diversity-measurements.md).

**A transmit gap is not a retune, and is handled separately.** Both
protocols stop feeding `rx_add_div_iq_samples()` for the whole over — P2
only sets `RXACTION_DIV` when not transmitting, duplex included, and P1
guards the mixer the same way — so the analysis stream acquires a hole
that nothing in the context comparison can see. `rxtx()` calls
`diversity_auto_gap()`, the one funnel every TX/RX transition goes
through, so MOX, VOX and Tune are all covered. It discards the partly
filled block and marks the next complete one as following a gap, which is
the flag the worker already uses to call `rade_corr_reset()`.

That matters only to RADE V1, and it matters a lot: without it the first
block after an over spliced pre-TX and post-TX samples into one transform
while `lock_a` went on advancing by one modem frame against a ring that
had skipped an arbitrary number of samples. The correlator tracked
straight through into a dead lock, holding a frozen weight for the whole
**Hang** time — 10 s by default, up to 30 — before it started searching
again. It now re-acquires deliberately after every over, which costs the
searching load in §7 for a second or two.

**The weight and the transform accumulators are deliberately kept across
the gap.** `div_cos`/`div_sin` are written only by `div_apply_weight()`,
and every path with no answer to give sets `div_auto_holding` and returns
without calling it, so the gain and phase from before the over stay
applied until a new lock produces a better fit. `div_reset_stats()` is not
called either: a cross spectrum is a time average rather than something
locked to the sample clock, so once no single transform spans the hole it
is unharmed. Window, Carrier ane FSK/DIgital therefore lose nothing at all
across an over.

---

## 5. The four references

What part of the spectrum the decision is taken from. Selected by
**Measure on** in the Diversity menu, which lists them as

    Window (wideband)
    FSK/Digital (occupancy MVDR)
    Carrier (AM/SAM)
    RADE V1 pilot (MVDR)

— the two general-purpose references first and the two that need a
particular signal to be present after them. That order is a display order
only: the `DIV_REF_*` values are what land in the props file and go over
the wire, so they are fixed and new ones go on the end. `ref_rows[]` in
`src/diversity_menu.c` is the only place the two orders meet. The sections
below are in the order the references were built, which is neither.

### Window (wideband)

Every bin in the analysis window. The window either follows the RX filter
or is placed by hand with **Window centre** and **Window width**, in Hz
relative to the tuned signal — the same reference the filter edges use.

"The tuned signal", not the dial frequency: `rx_set_filter()` folds the CW
sidetone into `filter_low`/`filter_high`, so a CW passband sits at +pitch
in CWU and -pitch in CWL, and the shifted frame's own zero is one pitch
away from the only signal there is. `div_window_zero()` supplies that
offset to the hand-placed window, so a centre of 0 is the zero-beat note
in every mode and the hand-placed and filter-following windows agree.
Following the filter never had the problem, because it takes the folded
edges. The panadapter overlay places the drawn window with the same
function, so what is drawn stays what is measured.

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

**Window centre and width are modal twice over.** The Window, Carrier ane FSK/DIgital references each keep their own pair, so aiming the carrier
tracker at a station 5 kHz away does not destroy the window set up for
wideband work; switching back restores it. All three pairs persist, and
all three are part of the per-mode block described in §6, so the pairs
built up for AM are still there after an evening on SSB.

Measured at 384 kHz, where a bin is 11.7 Hz wide: **0.03 Hz of error and
0.002 Hz rms of jitter** at 11 s averaging, holding at −6 dB carrier SNR.

It does not use WDSP's SAM PLL, which is deliberately set for fast
acquisition — 39.8 Hz natural frequency, ~25 Hz loop noise bandwidth,
around 7 Hz of jitter on a weak carrier. That is right for demodulating
SAM and about a hundred times wider than suits measuring a carrier that is
not going anywhere. Working from the spectrum also means this mode works
in plain AM, where the SAM PLL does not run at all.

### RADE V1 pilot (MVDR)

Correlates against RADE V1's known pilot — on the side of the tuned
frequency the operator's passband names, and only that side — to separate
the wanted signal from everything else, which allows the interference covariance to be
estimated separately and a null steered onto QRM rather than onto the
signal. The covariance comes from the off-carrier bins of the pilot span,
on the modem's own side of the tuned frequency, so the rejected sideband
cannot get into it; the channel is accumulated as a cross-spectrum, so a
residual frequency error cannot decohere it. Both of those replaced
simpler versions that measurably did not work — see
[`diversity-measurements.md`](diversity-measurements.md).
Uses no transform at all. See [`diversity-rade.md`](diversity-rade.md).

Selecting the RADE V1 reference sets the objective to **Sum**, since the
signal the pilot correlator is pointing at is the wanted one. **Null**
turns that answer through 180 degrees to cancel the RADE station instead,
which is the quickest way to check the array is pointed at it. The
correlator has only one answer to compute - MVDR against the interference
covariance already maximises the pilot's SINR - so unlike the transform
references the two objectives here really are a sign flip.

Unlike every other reference, this one holds a *lock* - a timing, a
frequency and a pilot bank it keeps returning to - so it is the only one
with something to give up. The **Hang** control decides when: how long
the lock outlives the pilot before the correlator searches again. Long
rides out a fade on a single station, which is when the weight is worth
the most; short is what a frequency several stations take turns on wants,
where each arrives over its own path and until the lock is dropped the
new one is being combined with the old one's answer. See
[`diversity-rade.md`](diversity-rade.md).

The wideband **RADE passband** reference that used to sit alongside it has
been retired. It placed a window on the 750-2200 Hz modem band, on the
side of the tuned frequency the operator's passband implied, and clipped
it to the filtere FSK/DIgital does the same thing from the same passband
and does it better: it finds where the modem's energy actually is rather
than assuming the nominal band, and it measures the noise separately
instead of assuming both branches carry the same amount. Its slot in the
props file's `diversity_auto_ref` is migrated te FSK/DIgital on restore.

##e FSK/DIgital (occupancy MVDR)

For a narrow digital signal - FT8, RTTY, PSK31, VARA, JS8 - in a passband
that is mostly empty. It is the only reference that measures the *noise*
separately from the signal, and everything it does differently follows
from that.

The other references have no picture of the noise on its own, so **Sum**
has to assume the two branches carry equal, uncorrelated noise. That is
what makes `w = +Sxy/Sxx` maximum ratio combining. On a real station the
assumption is usually false in two ways at once: ADC1 is often a small
loop or an active whip on a bare rear-panel input, several dB noisier
than the main antenna (§1), and much of what both antennas hear is
common-mode hash picked up on the feedlines, which is *correlated*
between them. Sum is blind to both.

A digital signal is narrow, so the empty part of the passband can simply
be looked at:

1. **The region.** The analysis region is the RX filter with **Window
   follows RX filter** ticked, or a hand-placed centre and width without
   it - the same two controls as Window mode, kept as their own modal
   pair. Following the filter needs no sideband table and no ±1500 Hz
   constant: the passband is already on the correct side of the tuned
   frequency in USB, LSB, DIGU, DIGL and CW, and under CTUN.
2. **Occupancy.** The noise floor is the **median** of the bin powers
   over the region - a median, not a mean, so a signal filling part of
   the region cannot drag the floor up and hide itself. Bins more than
   6 dB above it *and* coherent between the two antennas are signal.

   This test has no false-alarm control that scales with the region: three
   bins clearing the threshold is the requirement whether the region holds
   thirty of them or two hundred. On a wide region full of nothing but
   noise, three will. Measured on a no-signal capture the mode produces a
   weight on 30 % of blocks, and it is why the reference is the wrong one
   for a narrow CW passband. See
   [`diversity-measurements.md`](diversity-measurements.md).
3. **The covariance.** Everything at least four bins clear of an occupied
   bin is noise, and `R` is accumulated over it. Correlation is not a
   disqualification here - correlated noise is precisely what `R` exists
   to describe.
4. **The solve.** `w = R⁻¹h`, with `h0 = ΣSxx` and `h1 = conj(ΣSxy)` over
   the signal bins. Diagonally loaded at 1 %, the same 2×2 solve the RADE
   V1 correlator uses (`div_mvdr2()`), which reaches `R` and `h` from
   pilot correlations instead.

With `R` diagonal and equal the solve reduces to `conj(h1/h0)`, which is
exactly `+Sxy/Sxx` - so **the mode degenerates to Sum when the noise
really is equal and uncorrelated**, which is both the right behaviour and
the property the tests pin down.

**The guard band is not optional.** A signal 40 dB above the noise puts
more into its neighbouring bins, through the analysis window's skirts,
than the noise floor holds - and those bins are correlated with the
signal's own channel. Feeding them to `R` tells MVDR that the direction
the signal arrives from is interference, and it steers the null straight
onto it. This is the standard failure of MVDR trained on data containing
the wanted signal, and it was observed here before the guard existed: on
a synthetic test the weight moved 8° off the correct answer. Four bins is
where the Blackman-Harris skirts have gone.

**A transmission ending is noticed by the shared staleness test** (§4),
which matters more here than anywhere else: the occupancy test is a ratio
against the median floor, so it is scale invariant and would not see the
level collapse at all. When it fires, the occupied span is withdrawn from
the status line and the panadapter as well, so a signal that has gone
stops being drawn as one.

**What it cannot do is separate a wanted signal from co-channel QRM.**
Both are occupied and both are correlated between the arms, so occupancy
has nothing to tell them apart by - that is what the RADE V1 pilot is
for. Here the operator separates them by placing the region, and **Null**
cancels what the region is sitting on, exactly as in Window mode. Both
objectives are meaningful, so unlike the RADE V1 reference this one does
not force Sum on selection.

**A full region is not an empty one.** If the signal covers the whole
region the median *is* the signal and occupancy finds nothing - which is
what a filter set snugly around the signal looks like, with the follow
tick on, which is what a careful operator does. Holding there would be a
trap: the better the filter, the more certainly the mode would do
nothing. Coherence tells the two apart. A full region is coherent, so it
is accumulated whole and falls through to plain maximum ratio combining;
an empty one is not, and holds.

Measured on synthetic 2-FSK against a two-path channel: identical to the
Window reference within 0.2 dB when both branches carry the same noise,
**13 dB better output SINR** when the aux branch is 20 dB noisier, and
one block from `track` to `search` when the signal stops.

---

## 6. Operator controls

| Control | Effect | Shown for |
|---|---|---|
| **Diversity** | The whole feature, including the DDC re-plumbing | always |
| **ADC attenuators** | Split ADC0's and ADC1's step attenuators (§1) | two ADCs with a step attenuator |
| **Attenuator (dB)** | ADC0 and ADC1, 0-31 dB each | only while split |
| **Gain / Phase** (coarse, fine) | Manual weight; live when Auto is not driving, and under **Hold** | always |
| **Auto** | Off / Null / Sum / Best — the objective | always |
| **Measure on** | Which reference (§5) | always |
| **Window follows RX filter** | — | Windowe FSK/DIgital |
| **Window centre / width** | The analysis window, the carrier search region in Carrier mode, or the occupancy search region ie FSK/DIgital. Measured from the tuned signal, which in CW is the zero-beat note. Kept separately per reference | Window (unticked), Carriere FSK/DIgital (unticked) |
| **Resolution** | 12 / 6 / 3 Hz bins. Finer lifts weak signals out of the noise but halves the update rate each step | all but RADE V1 |
| **Weighting** | Flat or Coherence (see above) | Window |
| **Averaging** | 0.2-30 s. Time constant for the estimate | always |
| **Hang** | 1-30 s. How long a lock outlives the pilot before the correlator searches again | RADE V1 |
| **Min coherence** | Below this the loop holds rather than adapts | all but RADE V1 |
| **Restart averaging** | Discards the accumulated statistics | always |
| **Hold** | Stops applying the loop's answer without stopping the loop | always |
| **Invert** | Swaps Null and Sum | always; inactive under Best |

Rows that the selected reference cannot use are **hidden, not greyed
out**. The RADE V1 reference places its own window, so four rows never
apply to it, and it uses no transform at all, so two more do not either.
Greying them left a tall dialog of mostly dead controls.e FSK/DIgital hides Weighting for the same kind of reason: the occupancy
split has already decided which bins carry signal, which is the job that
control was doing.

### One set of settings per group of modes

The right reference, window and objective are a property of what is being
received, and the mode is the operator's own statement of that: a carrier
to track in AM and SAM, an FSK occupancy to find in DIGU and DIGL, a
filter-wide window in SSB, and in CW a window narrow enough to sit on one
note. A single set carried across a mode change therefore hands the loop
settings chosen for a signal that is no longer there — the carrier tracker
hunting a carrier SSB does not have, or the 100 Hz window left over from
CW swallowing an SSB passband whole — and the operator has to notice and
undo it every time.

So the whole settings block is modal. The groups are

| Group | Modes |
|---|---|
| SSB | LSB, USB |
| CW | CWL, CWU |
| FM | FMN |
| AM | AM, SAM, DSB |
| Digital | DIGU, DIGL |
| Other | everything else, presently SPEC |

DSB sits with AM and SAM because its passband is symmetric about the
carrier, so a window and a carrier search mean the same thing there.
Anything unnamed shares one block, which costs nothing and means a mode
added later still lands somewhere sensible.

`rx_mode_changed()` announces the change to `diversity_auto_mode_changed()`
for RX0, which covers every route a mode can change by — the menu, CAT, a
bandstack recall, a VFO swap, and a client asking for one. That files what
is in force under the outgoing group, adopts the incoming one, and draws
the same restart and reset conclusions `diversity_auto_apply_settings()`
does. It deliberately does *not* invert the weight when the objective
crosses between Null and Sum: there the operator asked for the weight in
force to be turned over, here two unrelated blocks merely happen to
differ.

Hold is not modal. It is an operating state rather than a setting, and it
is not persisted either — see below.

The blocks live on the radio, with the analysis. A client is sent the
outcome of a switch the same way it is sent any other settings change, so
a panel running remotely follows a mode change on the radio with no
remote-aware code of its own.

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

It applies to Null and Sum only. **Best** is not one of that pair — it
selects an antenna rather than steering a null, so there is no opposite
answer to swap to — and the button is greyed out there. Left live, it
would have moved the combo to Null and performed no inversion at all.

The objective combo takes the same path, so the button and the combo
cannot behave differently.

For a while this control was inert in **RADE V1** and looked broken rather
than unimplemented. The correlator's answer was applied whatever the
objective said, so Invert turned `div_cos`/`div_sin` over immediately -
the audio changed - and then the next block wrote the un-inverted answer
back and slewed straight to it. Every reference now applies the sign the
objective asks for.

Settings persist in the props file as `diversity_auto_*`, with the
per-reference window pairs as `diversity_band_*`, `diversity_carrier_*`
and `diversity_digital_*`, and every group's block as
`diversity_group[n].*`. All of them are range checked on restore, by one
`div_settings_validate()` rather than a clamp per global. New reference
modes go on the end of the enum: the value is what is written to the
file, so inserting one in the middle silently changes what an existing
file means.

The flat `diversity_auto_*` keys stay, and are the current group's values.
They are also what seeds every group whose own keys are absent, so a file
written before the blocks existed gives each group what the radio was last
set to — the old single-block behaviour, until the operator moves a
control in one mode and not another. The mode is not restored until after
`diversity_auto_restore_state()` runs, so which group the flat keys belong
to is not knowable there; the first mode change announced adopts that
group's block, which for a file this version wrote is the same thing.

**The DSP runs on the radio; the UI runs wherever the operator is.** The
sample pair only exists on the radio side, so the analysis thread, the
correlator and the weight all live there and nothing about that changes
for a remote operator. What travels is the control surface and what it
displays, so a client drives every part of the feature — objective,
reference, window, resolution, weighting, averaging, hang, min coherence,
Hold, Invert, Restart and the manual weight — exactly as the radio's own
panel does.

Three messages carry it:

| Message | Direction | Contents |
|---|---|---|
| `CMD_DIVERSITY` | client → server | Enable, manual gain and phase (unchanged, predates this) |
| `CMD_DIV_SETTINGS` | both | The whole auto-loop control block, plus an action byte |
| `INFO_DIVERSITY` | server → client | What the loop is measuring, on the server's 150 ms timer |

`CMD_DIV_SETTINGS` sends the **whole** control block whenever any one
control moves, rather than one message per control. It is small, it is
idempotent, and it lets the radio work out what a change means by
comparing the block against what it has in force. That is why
`diversity_auto_apply_settings()` exists: the rules for what moving a
control implies — restart when the transform length changes or the
objective crosses Off, rebuild when a RADE reference is selected or left,
reset when the accumulated bins stop being the right bins, turn the weight
over on Null ↔ Sum — used to live in the menu callbacks, which was fine
while the only operator sat at the radio. With the UI able to run
elsewhere the server has to draw the same conclusions from a settings
block that the menu drew from a widget, and two copies of those rules
would drift. There is now one copy, and a client never has to reason about
restarts at all. **Restart averaging** is the single control that changes
no setting, so it cannot be seen as a difference between two blocks and
travels as the action byte instead.

The settings block also carries the three per-reference window pairs. They
are modal state the operator built up rather than derived values, so a
client that sent only the live pair would silently flatten the other two
on the radio.

**The radio owns the settings.** A connecting client receives
`CMD_DIV_SETTINGS` and adopts what it finds rather than imposing what it
saved, so the radio behaves the same however it is being driven and a
second client sees what the first one set. `diversity_auto_save_state()`
returns early on a client for the same reason — a client is a radio in its
own right when it is not connected, and must not carry the last radio's
settings into its own next session. The block travels the other way too,
so a control moved on the radio's own panel reaches a watching client, and
`diversity_menu_refresh()` repaints whichever dialog did not originate the
change.

`INFO_DIVERSITY` is written straight into the `div_auto_*` globals by
`diversity_auto_apply_status()`. Every consumer — the status line, the
antenna line, the panadapter overlay — already reads those, so none of
them needed remote-aware code, and `update_manual_sensitivity()` in
particular needs no remote special case: `div_auto_running`,
`div_auto_mode` and `div_auto_hold` are all current on both sides, so the
same three terms grey the manual sliders in the same places. That is the
check that the split is in the right place.

Because `div_auto_running` is now true on a client as well,
`diversity_auto_start()`, `_stop()`, `_reset()`, `_invert()` and
`_gap()` all return early when `radio_is_remote`. The flag no longer means
"there is an engine here" — there never is one on a client — and without
those guards a client would try to tear down an engine it never built.
`radio_is_remote` is only ever set true and never cleared, so the guard is
a property of the process rather than something that can go stale.

The one control that does not travel is the `DIVERSITY_CAPTURE`
development button: it writes a file from inside the analysis thread, so
it belongs where that thread is.

`CLIENT_SERVER_VERSION` is `0x01300008`. Client and server check it on
connect and refuse a mismatch, so both ends must be built from the same
tree.


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
e FSK/DIgital | 0.3 % | 0.7 % | 1.0 % | 2.4 % |
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

- The transform modes scale with `nfft` and are cheape FSK/DIgital adds a
  partial sort for the median noise floor, capped at 4096 samples however
  wide the region is, which is why it stays with the rest of them.
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
| RADE V1 lock drop | the **Hang** control, 1-30 s, plus the ~1 s the freeze gate takes |

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
| `src/diversity_auto.c`, `.h` | The engine: tap, queue, worker, transform modes, occupancy split, weight |
| `src/rade_correlator.c`, `.h` | RADE V1 pilot correlation; the MVDR solve itself is `div_mvdr2()`, shared wite FSK/DIgital |
| `src/diversity_menu.c` | Controls and status |
| `src/rx_panadapter.c` | The analysis-window overlay, and the RADE modem passband |
| `src/receiver.c` | The combiner, the tap into it, and `rx_mode_changed()`, where a mode change reaches the modal settings |
| `src/radio.c` | Start/stop, props, shutdown, and `rxtx()`, where a transmit gap is reported |
| `src/new_protocol.c` | P2 DDC pairing and ADC configuration |
| `src/client_server.c`, `.h` | `CMD_DIV_SETTINGS` and `INFO_DIVERSITY` on the wire |
| `src/client_thread.c`, `src/server_thread.c` | Where those are sent and received |
| `test/diversity/` | Mode coverage, window placement including the CW zero, weighting and keying, RADE acquisitione FSK/DIgital occupancy and MVDR, the modal per-mode blocks, props migration, CPU benchmark |

---

## 10. Related

- [`diversity-guide.md`](diversity-guide.md) — **start here**: what the
  feature does and how to use it, with worked examples
- [`diversity-rade.md`](diversity-rade.md) — the RADE V1 pilot correlator in detail
- [`diversity-measurements.md`](diversity-measurements.md) — what the
  combiners measurably do on recorded on-air captures, band by band.
  Ongoing; it is the record that decides what the constants should be
- [`diversity-digital-iq-proposal.md`](diversity-digital-iq-proposal.md) —
  the FSK/DIgital
 reference in detail, and what the proposal it grew out
  of got wrong
- [`diversity-dither-fix.md`](diversity-dither-fix.md) — a P2 bug found
  along the way, where ADC1 never received the dither/random setting
- [`diversity-auto-phasing.md`](diversity-auto-phasing.md) — design
  history, including the approaches that were tried and abandoned. Not a
  description of current behaviour
