# Diversity: measurements from recorded on-air captures

Running record of what the diversity combiners actually do on real
signals, measured against recorded two-antenna captures rather than
against the synthetic waveform in `test/diversity/test_rade.c`.

This page is the durable output of that work. The instrument that
produces the captures is a temporary development tool and will be deleted
(see `test/diversity/devtools/README.md`); the numbers here are meant to
outlive it, so every finding below is stated so that it can be checked
again from the raw `.divc` files alone.

**Status: open.** Findings 1, 2 and 9 have been acted on and the code has
changed; see "What was changed, and what it scored" at the end, which is
where the after figures live. Finding 8's *mechanism* did not survive the
attempt to fix it and has been corrected in place. The threshold policy is
settled for now. Still open: the USB pilot bank has never been correlated
on air, and the Digital I/Q occupancy test has no false-alarm control.

Read in order, the findings divide into two groups. The **Window**
reference gains 1.6 to 1.8 dB over the better antenna on every voice
capture, on two bands and both sidebands. Everything else - RADE V1 on
four captures, Digital I/Q on CW - lands *below* the better antenna, and
in each case for a reason that has been isolated and measured. RADE V1 has
since been repaired and now matches or beats the better antenna on all
three captures it was scored against; Digital I/Q on CW has not.

For how the modes work, see [`diversity.md`](diversity.md) and
[`diversity-rade.md`](diversity-rade.md).

## Capture set

All Angelia, 48 kHz DDC, averaging 10.5 s, hang 5.2 s, objective Sum. The
RADE captures are 703 blocks of nfft 4096 (85.3 ms each) = 60 s. The rest
vary, because the operator's Resolution control sets the transform size:
nfft 4096, 8192 and 16384 all appear, giving 85, 171 and **341 ms** per
analysis block. That matters more than it looks - at nfft 16384 the 10.5 s
averaging time is only 31 blocks, so the loop is coarse in time as well as
fine in frequency. Every capture recorded **0 dropped and 0 skipped
blocks**, so none of the analysis below is working around a lossy
recording.

| capture | freq | mode | reference running | RADE present |
|---|---|---|---|---|
| `213155` | 7.047 | DIGL | RADE V1 | 0-21 s and 26.6-60 s, **two stations** |
| `213018`, `213128` | 7.047 | DIGL | Digital I/Q | not analysed |
| `233133` | 7.047 | DIGL | RADE V1 | 0-36 s and 48-60 s |
| `233241` | 7.047 | DIGL | RADE V1 | throughout |
| `231724` | 3.588 | DIGL | Digital I/Q | 0-6 s and 30-60 s |
| `232052` | 3.588 | DIGL | RADE V1 | 0-5.8 s only, then dead air |
| `231532` | 3.588 | DIGL | Digital I/Q | **none** |
| `232750` | 3.588 | DIGL | RADE V1 | **none** |
| `233423` | 14.240 | DIGU | RADE V1 | **none** - band noise, occasional weak SSB |
| `233615` | 1.985 | LSB | RADE V1 | **none** - strong local interferer on ADC0 |
| `235853` | 3.663 | LSB | Window, coherence | analog voice, 33 s |
| `000012` | 3.663 | LSB | Window, coherence | analog voice, 60 s, 5 kHz filter |
| `235837` | 3.663 | LSB | Window, coherence | analog voice, **3.2 s - too short to use** |
| `000209` | 14.262 | USB | Window, coherence | analog voice, 43 s, nfft 8192 |
| `000328` | 14.262 | USB | Window, coherence | analog voice, 60 s, 5 filter changes |
| `001054` | 14.0522 | CWL | Digital I/Q | CW, 60 s, nfft 16384 |
| `001157` | 14.0522 | CWL | Digital I/Q | CW, 60 s, **operator tuning: 23 context changes** |

The four "none" captures are the most valuable ones in the set. A capture
of nothing is what says whether a detector threshold is safe, and it costs
nothing but a minute of a quiet band.

`231724` and `232052` are a matched pair: same band, same path, five
minutes apart, one running each reference. That comparison did more work
than any single capture.

## Method, and three traps worth knowing about

Three yardsticks are used, in increasing order of trust.

**Detector metrics** - time to lock, lock uptime, false locks. Cheap,
and the right measure for acquisition and threshold questions.

**The measured channel and the measured noise.** The pilot gives `h0` and
`h1` directly; a stretch of dead air in the same capture gives the true
noise covariance. Together they give the weight a two-branch combiner
*should* be using, independently of anything the code computes.

**Decode.** Three or more librade receivers run side by side over one
capture - each arm alone, and each candidate weight - counting frames in
sync and averaging `rade_snrdB_3k_est()`. This is the only measure that
answers the question the combiner exists to answer, and it is the one that
settles disagreements.

### Trap 1: a pilot-domain SNR flatters the wrong answer

The first metric tried here was the pilot correlation's own SNR, defined
as `|mean(c)|^2 / var(c)` over a window of frames. It agreed that the
shipping weight was losing, and then confidently picked a weight that was
**180 degrees from correct** - because `var(c)` on a fading path is
dominated by the channel's own movement, not by noise, so the metric
rewards a combination that is *steady* rather than one that is *strong*.
Cancelling the wanted signal is very steady.

Any metric built from the same pilot correlations the estimator uses will
do this. Decode is what caught it.

### Trap 2: synthetic AWGN is far too kind to a detector

Adding white Gaussian noise to a capture until it breaks gives a
consistent, repeatable, and **wrong** answer for where a detection
threshold can sit. On synthetic noise `RADE_USE_RATIO` looked safe down to
2.00 over five seeds. On recorded dead air it is not - see "False alarms".

Real band noise has QRN bursts, 30 dB of level swing over a minute, and
strong correlated signals in the rejected sideband. Thresholds have to be
set against recorded silence.

### Trap 3: mean SNR is selection-biased once anything loses sync

`rade_snrdB_3k_est()` is averaged only over frames that synced. A stream
that holds sync through the bad parts of a capture is *penalised* for it,
because it reports SNR on frames the others skipped. Where sync is not
essentially 100 %, compare **synced frame counts**, not mean SNR.

## Finding 1: the RADE V1 covariance is not noise

`rade_track()` builds its interference covariance from the pilot-span
residual `x - h*pw`. That residual is supposed to be everything that is
not the wanted signal. It is not.

Inter-arm coherence of the covariance the correlator actually uses,
against the true noise measured in dead air in the same capture:

| capture | true noise | shipping pilot-span R | R from passband guard bins |
|---|---|---|---|
| `213155` over A | 0.106 | **0.803** | 0.289 |
| `213155` over B | 0.106 | **0.774** | 0.394 |
| `233133` | 0.425 | **0.652** | 0.285 |
| `233241` | 0.443 | **0.802** | 0.415 |
| `231724` | 0.276 | **0.659** | 0.327 |
| `232052` | 0.492 | 0.610 | 0.610 |

The shipping covariance is 1.2 to 7.6 times more correlated between the
arms than the real noise is, on every capture, on both bands. Its *phase*
is wrong too - on `233133`, +114 degrees against a true -158.

The cause is that a single scalar `h` is fitted across the pilot symbol
and only that one component is removed. Everything else in the
correlator's +/-3 kHz view stays in the residual, and on these captures
that is dominated by whatever occupies the **rejected sideband** - a
station carrying comparable power to the wanted one, 0.80 to 0.84
coherent between the arms, which WDSP's own filter throws away and the
operator never hears.

MVDR then does exactly what it is told and steers a null onto it. That
null lands close to the wanted signal's own inter-arm phase, so the
combiner subtracts the signal it is supposed to be combining.

Restricting the covariance to guard bins **inside the operator's
passband** but off the modem's carriers brings it back in line with the
true noise in five of six cases.

## Finding 2: the channel accumulator decoheres

`acc_h0` and `acc_h1` are coherent EWMAs of a phasor that is still
turning, because nothing removes the pilot's frame-to-frame rotation. At
the operator's 10.5 s averaging time the coherent part of `acc_h0`
measures **16 dB (over A) to 28 dB (over B) below** the per-frame `|h0|`,
and the accumulated phase is dragged 36 to 51 degrees off.

Worth about 0.7 dB on its own - much less than Finding 1 - but it is a
real defect, and it gets *worse* as the operator lengthens Averaging,
which is the opposite of what that control promises.

Related, and visible in every capture: the tracked frequency never
settles. It walks monotonically (+20 to +8 Hz over 25 s in replay of
`213155`) because the discriminator measures the pilot's absolute
inter-frame rotation, which the reference's own rotation does not cancel.

The rotation-invariant form - accumulate `d1*conj(d0)` and `|d0|^2`
instead of `d0` and `d1` - removes the problem entirely and is the same
cross-spectrum the Digital I/Q reference already uses in `bin_xy`,
`bin_xx`.

## Finding 3: what the combiners deliver

Decode-scored, mean `rade_snrdB_3k_est()` in dB. "available" is the ideal
two-branch combiner built from the measured channel and the dead-air
noise.

| capture | arm 0 | arm 1 | RADE V1 | Digital I/Q | ideal |
|---|---|---|---|---|---|
| `213155` 40 m | 7.2 | **9.6** | 6.2 | 8.7 | 11.1 |
| `233133` 40 m | **9.4** | 1.5 | 8.9 | 8.3 | 10.2 |
| `233241` 40 m | **10.3** | 4.8 | 8.5 | 9.8 | 9.9 |
| `231724` 80 m | **4.4** | 1.7 | 4.0 | 5.1 | 5.1 |

Against the better arm:

| capture | RADE V1 | Digital I/Q | ideal |
|---|---|---|---|
| `213155` | **-3.4** | -0.9 | +1.5 |
| `233133` | **-0.5** | -1.1 | +0.8 |
| `233241` | **-1.8** | -0.4 | -0.3 |
| `231724` | **-0.3** | +0.7 | +0.8 |

**RADE V1 is below the better antenna on all four captures.** Digital I/Q
is better than RADE on three of four, and reaches the ideal on `231724`.
Neither reliably beats simply selecting the better antenna.

Two honest caveats. On `233133` Digital I/Q is 0.6 dB *worse* than RADE,
so "the simpler mode always wins" is not supported. And on `233241` the
"ideal" column is itself 0.3 dB below arm 0, which means the dead air used
for that capture's noise estimate was not representative of the over -
a limitation of deriving noise from a different part of the recording, not
a real result.

### The repair, scored

Fully causal, nothing an implementation could not compute on air:
cross-spectrum `h` (Finding 2) plus a covariance from passband guard bins
(Finding 1).

| capture | shipping | repaired | ideal |
|---|---|---|---|
| `213155` | -3.3 | **+0.9** | +1.5 |
| `233133` | -0.5 | **+0.5** | +0.8 |
| `233241` | -1.8 | **+0.1** | -0.3 |

A consistent 1.9 to 4.2 dB recovered, and on `233241` it beats the oracle,
which is the expected consequence of that capture's oracle being
mis-estimated rather than a sign the repair is doing something clever.

Available gain is modest and path-dependent - +0.8 to +1.5 dB. Where one
antenna is far worse than the other there is very little on the table, and
that was true of most of these captures.

## Finding 4: the noise is not always uncorrelated, so keep the cross term

On `213155` the inter-arm noise coherence was 0.106 - essentially
uncorrelated. Measured on that capture alone, simply dropping the
covariance's cross term (reducing MVDR to maximum ratio combining with
unequal branch noise) scored **better** than any principled repair, and
the obvious conclusion would have been to do that.

Every other capture contradicts it:

| capture | band | arm1/arm0 noise | in-band coherence |
|---|---|---|---|
| `213155` | 40 m | -3.7 dB | 0.106 |
| `233133` | 40 m | +4.2 dB | 0.425 |
| `233241` | 40 m | -0.1 dB | 0.443 |
| `231724` | 80 m | +4.8 dB | 0.276 |
| `231532` | 80 m | +4.0 dB | 0.324 |
| `232750` | 80 m | +4.8 dB | 0.241 |
| `233423` | 20 m | +6.2 dB | 0.256 |
| `233615` | 160 m | -2.5 dB | 0.437 |
| `235853` | 80 m voice | +2.5 dB | 0.263 |
| `000012` | 80 m voice | +2.4 dB | 0.066 |
| `001054` | 20 m CW | - | 0.266 |
| `001157` | 20 m CW | - | 0.578 |

On `231724` the ideal MVDR weight and the ideal MRC weight are 51 degrees
apart, so the cross term is doing real work there.

**Confine the covariance to the passband; do not diagonalise it.** One
capture would have led the other way.

Note also that which antenna is better *flips between bands* - arm 1 is
3.7 dB quieter on 40 m and 4.0 to 6.2 dB noisier on 80/20 m. There is a
case for showing per-arm SNR in the menu regardless of what the combiner
does.

## Finding 5: two kinds of interference, and only one is nullable

`233615` (160 m, strong local interferer on the primary antenna, no RADE)
separates them cleanly. Per 100 Hz, averaged over the minute, in the
tapped frame:

| band | arm0 | arm1 | coherence | deepest single-weight null |
|---|---|---|---|---|
| 150-470 Hz | -40 dB | -39 dB | 0.66-0.70 | -2.5 to -2.9 dB |
| **574-996 Hz** | **-28 to -35 dB** | -38.7 dB | **0.20-0.38** | **-0.2 to -0.7 dB** |
| 1100-3000 Hz | -41 dB | -38.5 dB | 0.60-0.83 | -2.0 to -5.0 dB |

The local source is the 574-996 Hz hump. It is up to **10.5 dB above the
floor on ADC0 only**, and it is **incoherent between the arms**, so a
two-branch array cannot null it - the best a single complex weight can do
there is 0.7 dB. What the array *can* do is de-weight the contaminated
arm, which is what MVDR with a correct noise covariance would do by
itself.

Everywhere else in that capture the coherence is 0.6 to 0.83: a
common-mode component that a two-branch array can genuinely null by 2 to
5 dB.

Both classes are present in one recording. This is the case the cross
term exists for, and it is the strongest argument against diagonalising
the covariance.

## Finding 6: on analog voice the wideband references work

Three captures of analog SSB on 80 m (3.663 MHz LSB, **Window** reference
with **coherence** weighting, window following the filter) are the first
real-signal test of anything other than RADE. `235837` is 3.2 seconds long
and is not used.

There is no decoder to appeal to here, so the yardstick is the passband
signal-to-noise ratio measured directly: total power in the operator's
passband over the blocks that contain voice, against the same over the
blocks that do not, with the weight applied per block as the loop produced
it. Voice and quiet blocks are separated by passband power, which is
strongly bimodal in both captures.

| | `235853` (2.7 kHz filter) | `000012` (5 kHz filter) |
|---|---|---|
| arm 0 alone | 19.58 dB | 19.84 dB |
| arm 1 alone | 20.11 dB | 20.55 dB |
| **as it ran on air** | **20.52 (+0.41)** | **21.66 (+1.11)** |
| replayed Window / coherence | 21.92 (+1.81) | 22.11 (+1.56) |
| replayed Window / flat | 21.34 (+1.23) | 22.11 (+1.56) |
| replayed Digital I/Q | 21.34 (+1.23) | 20.63 (+0.08) |
| best single fixed weight | 20.73 | 21.95 |

Bracketed figures are against the *better* arm, which is arm 1 in both.

**The Window reference gains where RADE V1 loses.** Replayed from a cold
start it is 1.6 to 1.8 dB better than the better antenna; as it actually
ran on air it is 0.4 to 1.1 dB better. Either way positive, on the same
radio, the same engine, the same operator settings and an adjacent band to
the captures where RADE V1 came out 0.3 to 3.4 dB *behind* the better
antenna.

The gap between the on-air and replayed rows is real and unexplained. The
replay starts with empty accumulators; the on-air run was already carrying
whatever the previous minute had put in them, and both captures are only
two to six averaging times long. Treat the on-air row as the honest
account of what the operator got and the replayed row as what the mode
does from cold.

### Why this corroborates Finding 1

Window and Digital I/Q both build their channel *and* their covariance
from FFT bins **inside the operator's window**. Neither can see the
rejected sideband. RADE V1 is the only reference that works on the whole
+/-3 kHz decimator output, and it is the only one that loses. Two modes
sharing the same `div_mvdr2()` solve and differing only in where they look
come out on opposite sides of zero.

### Coherence weighting earns its place, mostly by passing the gate

Its clearest effect is not on the weight but on how often there is one at
all - the fraction of blocks that produced a weight rather than holding:

| | `235853` | `000012` |
|---|---|---|
| Window / coherence | 43 % | 30 % |
| Window / flat | 17 % | 18 % |
| Digital I/Q | 41 % | 37 % |

Flat weighting spends most of the voice *holding*, because the coherence
it reports is diluted by the noise-only bins in the window and falls below
`div_auto_coherence_min`. Weighting each bin by its own coherence lifts
the figure past the gate. That is the mechanism `diversity.md` describes,
now measured. On SNR the two are 0.58 dB apart on the narrow passband and
identical on the wide one - so on these two captures coherence weighting
never hurt and sometimes helped.

### Digital I/Q is the wrong tool for voice

0.6 dB behind Window on the narrow capture and 1.5 dB behind on the wide
one, converging to `|w|` of 0.32 and 0.16 where the useful weights are
around 0.8. Its occupancy split takes the median bin power as the noise
floor, which assumes a narrow signal sitting in a mostly empty passband.
SSB voice intermittently fills the passband instead, so the median rides
up with the speech and the split stops meaning anything. Nothing is broken
- the mode is being used outside what it was designed for - but the
numbers are worth having next to the advice.

### A moving weight beats any fixed one

On `235853` the per-block weight is 1.2 dB better than the best single
constant weight found by exhaustive search over the same capture. On
`000012` it is 0.2 dB better. Whatever else is wrong elsewhere, tracking
the channel is worth something on a real fading path.

### What this does *not* show

The per-bin channel `h1/h0` appears to vary across a voice passband by
about 3 dB and 25 degrees, which would undermine the flat-scalar model.
It does not: splitting the voice blocks into two halves and measuring each
independently gives per-bin differences of 3.4 dB and 25 degrees - as
large as the spread being measured. **The apparent structure is
estimation noise.** Voice does not fill every bin all the time and the
inter-arm coherence is only 0.47 to 0.58, so the per-bin channel simply
cannot be resolved from a minute of speech.

The flat model therefore stands, but it is the RADE captures - continuous,
band-filling, coherence near 0.9 - that establish it. For the same reason
the "per-bin optimum" bound computed on these captures (0.35 dB above the
scalar weight on `000012`) is mostly fitting noise, and should not be read
as available headroom.

## Finding 7: USB voice, and the frame inversion confirmed on the other sideband

Two captures of analog SSB on 20 m (14.262 MHz **USB**, Window reference,
coherence weighting, 5 kHz filter). Measured the same way as Finding 6.

First, a bookkeeping result that had never been checked. The tapped buffer
is inverted with respect to RF, which was established on LSB. These are
the first USB captures, and they confirm it from the other side: with a
WDSP filter of **+150 to +5150 Hz**, the energy sits at **-5150 to -150 Hz**
in the tapped frame, 4 to 7 dB above the mirror band. The inversion holds
on both sidebands, so the sideband note in `rade_correlator.c` and
`diversity_auto.c` is right.

`div_rade_side_expected()` also recorded `expect_bank = 1` for these DIGU
/ USB contexts, so the *derivation* of the USB bank is confirmed. The
correlation in bank 1 is still untested - neither capture contains RADE.

| | `000209` | `000328` |
|---|---|---|
| arm 0 alone | 9.36 dB | 11.80 dB |
| arm 1 alone | 13.03 (+3.67) | 15.86 (+4.06) |
| **as it ran on air** | **14.63 (+5.28)** | **17.43 (+5.62)** |
| best single fixed weight | 14.22 (+4.86) | 16.85 (+5.04) |

**+1.60 and +1.57 dB over the better antenna**, and the running loop beat
the best fixed weight by 0.4 to 0.6 dB - tracking is worth something
again. Together with Finding 6 the Window reference now gains about
1.6 dB over the better antenna on two bands and both sidebands, on four
voice captures. That is the one part of this feature that is measurably
doing its job everywhere it has been looked at.

`000328` also contains five context changes in two seconds at t = 5.9 to
7.9 s, where the operator narrowed the filter from 5150 down to 2850 Hz
in five steps. With the window following the filter each one is a
legitimate reset. See Finding 9 for when they are not.

## Finding 8: CW - the occupancy split has no room in a narrow filter

Two captures on 14.0522 MHz CWL, Digital I/Q reference, 600 Hz filter
(-850 to -250, so +250 to +850 in the tapped frame), sidetone 550 Hz -
which is also the first exercise of the CW branch of `div_frame_off()`
on recorded data.

The band segment is busy but weak: no single dominant carrier, and the
strongest bin averages only 3.6 to 4.5 dB above the window median over
the minute. Scoring the ten strongest bins against the weakest 120:

| | `001054` | `001157` |
|---|---|---|
| arm 0 alone | 3.02 dB | 2.73 dB |
| arm 1 alone | 0.01 (-3.01) | -0.05 (-2.78) |
| **as it ran on air** | **0.13 (-2.89)** | **0.43 (-2.29)** |
| best single fixed weight | 3.32 (+0.30), `w` = 0.10 | 2.89 (+0.16), `w` = 0.10 |

Arm 1 is 3 dB worse here, so there was only 0.2 to 0.3 dB on the table.
The loop converged to `|w|` of about 2.4 - roughly **24 times too large** -
weighting the worse antenna heavily and giving away 2.3 to 2.9 dB.

The mechanism is visible in the constants. `DIV_OCC_DB` requires a bin to
be **6 dB** above the region's median to count as signal; these carriers
are 3.6 to 4.5 dB above it. Fewer than `DIV_OCC_MIN_BINS` clear the
threshold, so `div_digital_solve()` takes its "the region is full"
fallback - accumulate every bin in the window, coherence-weighted, and
solve plain maximum ratio combining. That fallback is correct for a filter
set snugly around a strong digital signal, which is what its comment
describes. On a 600 Hz CW filter holding weak signals the region is not
full of signal, it is full of *noise*, and the resulting weight is aimed
at whatever the two antennas hear in common.

**Digital I/Q is the wrong reference for a narrow CW passband.** That much
stands. The mechanism above does not - see the correction below.

### Correction: the fallback is not the path responsible

The occupancy-fallback explanation was written from the recorded on-air
run and was checked properly only when it came to be fixed. It is wrong,
and the record is more useful with the correction in it than with the
tidy story.

Replayed from a cold start through `run_ref`, `001054` produces a weight
on **5 %** of blocks and `001157` on **45 %**, and in both the occupancy
split does find its `DIV_OCC_MIN_BINS` - so the "region is full" branch is
barely reached. A guard was written for that branch (hold unless the
region median stands `DIV_OCC_DB` above the median of the band either side
of it) and measured on eight captures. It moved the passband score by
**0.01 to 0.02 dB** and was not kept.

Re-scored with the metric stated explicitly - the ten strongest bins of
the filter against the weakest 120, on the spectrum averaged over the
whole capture, Blackman-Harris window - the picture is flatter than
Finding 8 reports:

| | `001054` | `001157` |
|---|---|---|
| arm 0 | 2.54 dB | 2.37 dB |
| arm 1 | 0.53 (-2.01) | 0.68 (-1.68) |
| as it ran on air | 0.55 (-1.99), `\|w\|` 2.36 | 1.06 (-1.30), `\|w\|` 0.54 |
| replayed from cold | 0.84 (-1.70), `\|w\|` 1.12 | 0.72 (-1.64), `\|w\|` 2.38 |

The `|w|` of about 2.4 that Finding 8 attributes to both captures belongs
to `001054` on air only; on `001157` the operator's actual weight was
0.54. Replayed from cold the two swap over. What is common to all of them
is that no combination gets near arm 0 alone: the loss is 1.3 to 2.0 dB
whatever weight is in force, because arm 1 is 1.7 to 2.0 dB worse and
anything that keeps it at unity gain lands there.

**What is actually wrong is upstream of the split.** On `231532` - 80 m,
Digital I/Q, and *no signal at all* - the mode produces a weight on 30 %
of blocks. It reaches that through the normal path, not the fallback:
enough noise bins clear a threshold set 6 dB above the region's own median
to satisfy `DIV_OCC_MIN_BINS` = 3, and the coherence gate passes some of
them for the reason in Finding 10. The occupancy test has no false-alarm
control that scales with how many bins the region holds, and three bins
out of two hundred is not evidence of anything. Replacing the region
median with the band floor either side of it was also tried, and changed
nothing on any capture, because on these paths the two are the same
number.

That is the open question, and it wants its own measurements rather than a
guess. Until then the advice stands on the numbers above: use **Window**
on a narrow CW passband, not Digital I/Q.

## Finding 9: a 1 Hz VFO step throws the whole estimate away

`001157` is the operator tuning around during the capture, and it is the
most informative minute in the whole set.

`div_context_changed()` compares `frequency` **exactly**, so every step of
the tuning knob is a full context change: `div_reset_stats()` plus
`rade_corr_reset()`. Several of the steps in this capture are 1 Hz -
block 11 to 12 is 14052214 to 14052215 Hz, and the accumulators are
discarded.

The rate is what does the damage:

| | `001157` |
|---|---|
| context changes | 23, over blocks 9 to 38 (t = 3.1 to 13.0 s) |
| blocks between resets | median **1**, minimum 1 |
| blocks per averaging time (nfft 16384, tau 10.5 s) | **31** |

So while the operator tunes, the loop is permanently in the first block or
two of an estimate that needs thirty-one. Measured, against the settled
part of the same capture:

| | while tuning | settled |
|---|---|---|
| block-to-block step in the applied weight | **0.127** | 0.008 |
| holding | 83 % | 49 % |
| reported coherence | 0.171 | 0.687 |

The weight moves sixteen times faster per block, on estimates built from
one or two blocks.

To be fair to the design, the coherence gate absorbs most of this: the
loop holds 83 % of the time while tuning rather than acting on the
rubbish. The damage is bounded. But the weight actually in force during
tuning is a partly-slewed remnant, and the mode is effectively inoperative
for as long as the operator is turning the knob plus one averaging time
after they stop - here, thirteen seconds of tuning followed by ten more.

The antenna-to-antenna transfer `h1/h0` is a property of the two antennas
and the path; it does not change because the dial moved 1 Hz. What a
retune changes is *which signal is in the window*, and for a 1 Hz step
that is the same signal. The counter-argument is that tuning across a band
moves between stations and a stale weight aimed at the previous one is
worse than none; that argument applies to a kilohertz, not to a hertz.

The same mechanism, more benignly, is in `000328`: five filter changes in
two seconds, five resets, then fifty-two seconds to recover.

### Acted on: a 20 Hz tolerance, measured from the last reset

`div_context_changed()` now allows the three frequency fields to move by
`DIV_RETUNE_HZ` = 20 Hz before it resets. The comparison is against the
context as it stood at the **last reset**, not the previous block, so
twenty single-hertz steps count as twenty hertz; comparing against the
previous block would never fire at all, which is worse than resetting too
often. Everything else keeps its exact comparison, so a filter change is
still a full reset.

20 Hz because it is inside the +/-60 Hz the RADE correlator tracks, so a
lock survives it, and an order of magnitude below the narrowest CW filter.

Across the whole capture set only one capture changes at all:

| capture | resets, exact | resets, 20 Hz |
|---|---|---|
| `001157` (operator tuning) | 23 | **7** |
| `000328` (five filter steps) | 5 | 5 |
| `233423` | 1 | 1 |
| every other capture | 0 | 0 |

Measured on `001157`, block-to-block movement of the applied weight over
the blocks the operator was tuning (9 to 38): **0.368 before, 0.205
after** — 44 % less. Settled movement is unchanged at 0.020, and the four
voice captures replay bit-identically. So the change reaches the case it
was written for and nothing else.

One instrument note, because it invalidated the first attempt at this
measurement: `run_ref` used to take the operator's context from block 0
and keep it for the whole replay, so the recorded retuning was invisible
to the engine and both builds behaved identically. It now follows the
context block by block.

## Finding 10: the coherence gate sits inside the range of measured noise coherence

`div_auto_coherence_min` defaults to **0.30**. The inter-arm coherence of
the *noise*, measured in dead air across every capture in this document,
runs from **0.066 to 0.578**, with a median around 0.28.

On a path where the noise is 0.10 correlated the gate does what it is for.
On one where the noise is 0.44 or 0.58 correlated, "the two antennas agree
here" is true of the noise as well, and the gate cannot separate a signal
both antennas hear from noise both antennas hear. That is the second half
of the CW failure in Finding 8, and it is why the holding fraction varies
so widely between captures that otherwise look alike.

This does not have an obvious fix - a fixed threshold on coherence alone
cannot distinguish the two - but it is a limit worth stating, and it
argues for the gate being a per-path operator control rather than a
constant.

## False alarms

Locks produced on captures with no RADE signal anywhere. Cells are
`acquisitions / percent of the capture locked`.

| `use_ratio` | `231532` 80 m quiet | `232750` 80 m quiet | `233423` 20 m noise+SSB | `233615` 160 m QRM |
|---|---|---|---|---|
| 1.00 | 0 | 0 | 1 / 27 % | 1 / 90 % |
| 1.25 | 0 | 0 | 1 / 27 % | 2 / 66 % |
| 1.50 | 0 | 0 | 0 | 1 / 33 % |
| 1.75 | 0 | 0 | 0 | 0 |
| 2.00 and above | 0 | 0 | 0 | 0 |

Separately, `232052` - dead air *following* a real over - produces a false
lock at `use_ratio` 2.00 and below: 53.2 to 59.9 s, frequency pinned at
-50 Hz at the edge of the search range, quality 0.033 against 0.173 for
the genuine lock earlier in the same capture. Blind search on an empty
band is not the binding constraint; **re-acquisition shortly after a real
signal drops is**, with the accumulators still primed.

Against that, lowering the threshold buys nothing measurable on real
signals. Lock uptime over `use_ratio` 1.75 to 3.0:

| capture | 1.75 | 2.00 | 2.25 | 2.50 | 2.75 | 3.00 |
|---|---|---|---|---|---|---|
| `233133` | 55 % | 58 % | 54 % | 51 % | 55 % | 52 % |
| `233241` | 65 % | 63 % | 56 % | 56 % | 60 % | 60 % |
| `213155` | 93 % | 93 % | 93 % | 93 % | 93 % | 93 % |

Non-monotonic and within a few points - that is scatter, not a trend. The
only consistent effect is `233241`'s first lock moving from 19.8 s to
13.7 s at 2.00 and below.

**Conclusion: leave `RADE_USE_RATIO` at 2.50.** There is no measured
benefit to lowering it on real signals, and a measured false-alarm cost
below 2.00. This supersedes an earlier suggestion of 2.00 that was based
on synthetic AWGN - see Trap 2.

It also qualifies the claim in
[`diversity-rade.md`](diversity-rade.md) that `RADE_USE_RATIO` sets the
weak-signal floor. On these captures it does not set the floor of
anything: varying it over 1.75-3.0 leaves lock uptime unchanged. What it
holds is the false-alarm line, and that part stands.

## What holds on every capture

- **Bank 0 is the LSB bank.** 7.5 to 15.4 sigma in bank 0 against 1.7 to
  4.6 in bank 1, on every over on 40 m and 80 m. The mapping in
  `rade_correlator.c` is right.
- **The tapped buffer is inverted with respect to RF, on both sidebands.**
  An LSB filter of -2850..-150 puts the signal at +150..+2850 in the
  tapped frame; a USB filter of +150..+5150 puts it at -5150..-150
  (Finding 7). This was previously only checked on LSB.
- **The flat scalar channel model is right.** `h1/h0` measured per
  subcarrier varies by +/-0.3 to +/-0.63 dB in magnitude and +/-3 to
  +/-12 degrees in phase across 750-2200 Hz. Differential delay between
  the arms is under 6 us. A single complex weight is the correct model.
  This rests on the RADE captures; a voice passband cannot resolve it
  either way (Finding 6).
- **Every reference holds correctly when there is no signal.** On all four
  no-signal captures RADE V1 reported locked 0.00, holding 1.00,
  coherence 0.003-0.008, and Digital I/Q never produced a weight. On the
  voice captures all three references hold through the gaps between overs.
  None of them invents an answer from noise, including with the 160 m
  interferer at full strength on ADC0.
- **Reception is often close to anti-phase, but not always.**
  `arg(h1/h0)` measured -177, -161, -162, -105, -7 and -4 degrees across
  the overs. It is just the path; nothing structural.

## What is still open

- **The USB pilot bank has still never been tested on air.** Three USB
  captures now exist (`233423`, `000209`, `000328`) and none contains a
  RADE signal. `div_rade_side_expected()` is confirmed to *derive* bank 1
  for USB, and the frame inversion is confirmed on both sidebands
  (Finding 7), but nothing has ever correlated a real pilot in bank 1.
  A USB RADE capture with a real station remains the single most valuable
  thing missing.
- **Threshold policy needs more dead air.** Four quiet captures is enough
  to say 2.50 is safe and 1.50 is not; it is not enough to place the
  boundary. Dead-air captures are cheap and need no station.
- **No capture yet has a wanted signal *and* strong common-mode noise.**
  `233615` has the interference but no signal. `235853` comes closest -
  voice present with noise coherence 0.263 - but that is a wideband-mode
  capture, so it says nothing about the RADE covariance. What is wanted is
  a RADE station on a path with obvious common-mode noise: that is what
  would prove the passband-confined covariance keeps the nulling the mode
  is sold on.

- **Analog voice has been measured on one band, one path, two usable
  captures.** The +1.6 to +1.8 dB is worth confirming elsewhere, and the
  gap between the on-air and replayed rows in Finding 6 is not understood.
  A voice capture several averaging times long, armed before the loop has
  converged, would settle it.
- **No capture yet is marginal.** Every over here decoded at 99 %+ on
  either antenna alone. The case where diversity actually matters is the
  one that is hard to catch and easy not to bother recording.

- **The 20 Hz retune tolerance is a first number, not a measured one.**
  It is justified by what it must not break (the correlator's tracking
  range, the narrowest filter) rather than by how far the dial can move
  before `h1/h0` really has changed. That still wants a capture of a
  deliberate slow QSY across a band with a steady signal in view, so the
  weight can be watched as the frequency walks away from where it was
  measured.

- **Digital I/Q occupancy has no false-alarm control.** On `231532`, with
  no signal anywhere, the mode produces a weight on 30 % of blocks,
  through the normal path. Three bins clearing a 6 dB-over-median
  threshold is not evidence when the region holds two hundred of them, and
  `DIV_OCC_MIN_BINS` does not scale with region width. What is wanted is
  a threshold whose false-alarm rate is known: dead-air captures with
  Digital I/Q selected at several filter widths would give it directly.
  See the correction under Finding 8.

- **CW has been measured once, on weak signals.** `001054` and `001157`
  had only 0.2 to 0.3 dB available. A CW capture with one strong steady
  signal in the filter would say whether Finding 8 is about the occupancy
  threshold or about the signals being weak.
- **`--verify` has never passed on an on-air capture**, because every one
  was armed while the correlator was already locked. Arming before the
  lock would let the replay be checked against the radio.

## What was changed, and what it scored

Three changes to shipping code came out of the findings above. A fourth
was written, measured, and thrown away.

### `src/rade_correlator.c` — the covariance is no longer the residual

Findings 1 and 2, together, plus a frequency-discriminator bug the
investigation turned up. The three are one repair because they share a
root: the pilot reference is rebuilt from sample zero every frame while
the received pilot advances with the sample index.

- The interference covariance is measured in the **off-carrier bins** of
  the pilot span — 300 to 2850 Hz on the pilot bank's own side of the
  tuned frequency, excluding the modem's 750-2200 Hz carriers. The span is
  160 samples at 8 kHz, so its DFT bins are exactly the modem's 50 Hz
  carrier grid. The rejected sideband is excluded by construction.
- The channel is accumulated as the **cross-spectrum** `d1*conj(d0)` and
  `|d0|^2`, both invariant to a rotation the two arms share.
- The **frequency discriminator** subtracts the advance `lock_f` already
  accounts for, so it measures the residual it always claimed to.
- `rade_corr_reset()` now clears `rade_corr_freq_off` and
  `rade_corr_mirrored`, which used to survive it.

Decode-scored against librade, mean `rade_snrdB_3k_est()` against the
better antenna alone:

| capture | before | after | "repair" bound in Finding 3 |
|---|---|---|---|
| `213155` 40 m | -3.4 | **+0.5** | +0.9 |
| `233133` 40 m | -0.5 | **+0.5** | +0.5 |
| `233241` 40 m | -1.8 | **-0.0** | +0.1 |

3.9, 1.0 and 1.8 dB recovered. The mode went from below the better antenna
on all three to matching or beating it on all three, and lands within
0.4 dB of the offline bound everywhere.

Detection is untouched, which is the thing to check when a tracking loop
changes. Lock uptime, acquisitions and time to first lock are the same as
before to within their own scatter, on the real-signal captures and across
`use_ratio` 1.75 to 3.0; the false-alarm table below is unchanged, so
`RADE_USE_RATIO` stays at 2.50. `232052` at `use_ratio` 2.00 produces one
*fewer* false lock than before. Tracked frequency settles instead of
walking (Finding 2). CPU is unchanged in `bench_cpu`: the twenty-two
DFT bins per modem frame are about 30 kMAC/s against an acquisition search
orders of magnitude larger.

The displayed `rade_corr_snr` and `rade_corr_quality` read higher than
they used to, because a station in the rejected sideband is no longer
counted as interference. Numbers noted from older builds are not
comparable.

**One refinement was tried and rejected.** Clipping the covariance bins to
the operator's passband is the obvious extension of Finding 1, and it
scores *worse*: the 40 m captures were taken with a 500-2500 Hz filter, so
clipping drops the set back to the eleven bins immediately beside the
carriers and gives up 0.9 dB on `213155`, because halving the bin count
doubles the variance of `R`. The 350 Hz beyond a tight filter that the
unclipped set keeps is the same band noise the modem is sitting in, not a
second station. Finding 1 is about the rejected *sideband*, and choosing
the bins by pilot bank is what deals with that.

### `src/diversity_auto.c` — a 20 Hz retune tolerance

Finding 9. Details and measurements under "Acted on" there.

### What was thrown away

The Digital I/Q occupancy guard written for Finding 8. It moved the score
by 0.01 to 0.02 dB across eight captures because it guards a branch that
is barely reached, and the diagnosis it was built on turned out to be
wrong. Both the guard and the corrected diagnosis are described under
Finding 8; the corrected version is the useful part.

### One thing to expect

`replay_rade --verify` will now fail against any capture recorded before
these changes, and should: the file holds the state the *old* correlator
reached, and the correlator is not that one any more. The synthetic
round-trip (`make -C test/diversity/devtools run`) still passes, because
it records and replays with the same build.

## Reproducing any of this

```
make DIVCAP=1                       # radio with the capture button
make -C test/diversity/devtools     # replay_rade, run_ref, test_capture
```

`replay_rade` drives the correlator directly and sweeps its constants;
`run_ref` drives the whole engine so the Digital I/Q solve can be run over
a recording; `score_rade` decodes. See
[`test/diversity/devtools/README.md`](../test/diversity/devtools/README.md).

The capture files themselves are not in the repository - they are 46 MB a
minute. Keep them alongside this page for as long as the numbers here
matter.
