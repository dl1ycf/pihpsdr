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
settled for now.

**Finding 11** - the MVDR solve returning a weight of exactly zero, the
second antenna muted, the menu showing -27 dB, on between half and all of
the frames of every RADE capture in this document bar one - has been
found and fixed. RADE V1 now beats the better antenna on five of the six
captures it can be scored on. **Finding 14** adds an antenna-selection
objective and measures it; it is a floor, not a ceiling, and Sum remains
the right default. Still open: the Digital I/Q occupancy test has no
false-alarm control, and its per-arm SNR estimate is the weakest of the
four (Finding 14).

**Finding 15** is new, and open. The frequency loop has stable lock
points spaced one modem frame rate apart - 8.3 Hz - and acquisition
cannot resolve which of them is right. The radio and a cold replay of the
same samples settled on two different ones on the same capture. It costs
about a decibel of pilot SNR, a fifth of the quality reading, and
**nothing measurable in decode**, because the diversity weight is
indifferent to it. It is, though, the reason a lock can look poor on a
signal that is not.

**Finding 16** is the first look below the HF bands: two mediumwave
captures, one a 693 kHz broadcast with inter-arm coherence 0.982, the
other band noise at 0.52. Null reaches the ceiling on the first and the
Best objective picks correctly on both.

The USB pilot bank, previously the most valuable missing measurement, is
now confirmed on air - see Finding 12, and confirmed again on a second
band in Finding 15's capture set.

Read in order, the findings divide into two groups. The **Window**
reference gains 1.6 to 1.8 dB over the better antenna on every voice
capture, on two bands and both sidebands. Everything else - RADE V1 on
four captures, Digital I/Q on CW - lands *below* the better antenna, and
in each case for a reason that has been isolated and measured. RADE V1 has
since been repaired and now matches or beats the better antenna on all
three captures it was scored against; Digital I/Q on CW has not.

Read Finding 3 and the repair scored under it with Finding 11 beside
them. Those numbers were honest about what the shipping code delivered at
the time, but they were obtained with the solve returning zero on two
thirds of the frames of two of the three captures, which is not what the
surrounding text assumes was happening. The figures under "What was
changed, and what it scored" supersede them.

For how the modes work, see [`diversity.md`](diversity.md) and
[`diversity-rade.md`](diversity-rade.md).

## Capture set

All Angelia. Everything up to and including the 60 m set is averaging
10.5 s, hang 5.2 s, objective Sum; the four captures added on August 30
and 31 are not - averaging runs 1.9, 5.6 and 4.8 s, and `111852` has the
operator changing both the reference and the objective while it records.
The August 29 captures are at a **48 kHz** DDC; the 60 m set of August 30
is the first at **192 kHz**, 351 blocks of nfft 32768 (170.7 ms each) =
60 s, and every capture after it is at that rate too. The
40 m RADE captures are 703 blocks of nfft 4096 (85.3 ms each) = 60 s. The
rest vary, because the operator's Resolution control sets the transform
size: nfft 4096, 8192, 16384 and 32768 all appear, giving 85, 171, 341
and **171 ms** per analysis block (the last at four times the sample
rate). That matters more than it looks - at nfft 16384 the 10.5 s
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
| `110923` | 5.3685 | USB | RADE V1 | **first bank-1 capture**, locked 65 % |
| `111051` | 5.3685 | USB | RADE V1 | two acquisitions, arm 1 the *better* antenna |
| `111328` | 5.3685 | USB | RADE V1 | **none** - band noise, 192 kHz |
| `111734` | 5.3715 | USB | RADE V1 | locked 70 % |
| `202743` | 7.09203 | DIGL | RADE V1 | marginal - quality 0.15, averaging **1.9 s** |
| `232842` | 1.987 | DIGU | RADE V1 | **bank 1 on a second band**, locked 94 %, averaging 5.6 s |
| `111852` | 0.6929 | SAM | Window, then Carrier, then Digital I/Q | **mediumwave** - 693 kHz broadcast, objective changed mid-capture |
| `112151` | 0.7244 | SAM | Digital I/Q | **mediumwave** band noise, partly coherent |

`202743` begins on 7.177 MHz and retunes to 7.09203 MHz at block 9. The
recorder did **not** set the context-changed bit for it: `rec_flags` is
zero on all 351 blocks. That is a devtool defect, not a radio one, but it
means the flag cannot be used to find retunes in this file - the
`frequency` field has to be read directly.

`111852` and `112151` are the first captures below 1.8 MHz, the first in
`SAM`, and the first where the *second* antenna is the loud one by a wide
margin: ADC1 runs 14.5 to 15.2 dB above ADC0 across the whole passband on
both. See Finding 16.

The five "none" captures are the most valuable ones in the set. A capture
of nothing is what says whether a detector threshold is safe, and it costs
nothing but a minute of a quiet band.

The four 60 m captures were taken with **no note recorded**, which the
devtools README asks for and which nothing enforces. What is known of
them comes from the operator afterwards: ADC0 is the main antenna,
sometimes tuned and sometimes not, ADC1 an untuned doublet. That
asymmetry is the subject of Finding 13, and the missing note is the
reason it had to be established by measurement rather than read off the
file.

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
| `110923` | 60 m | -2.1 dB | 0.75 |
| `111051` | 60 m | -13.1 dB | 0.72 |
| `111328` | 60 m | -10.2 dB | **0.14** |
| `111734` | 60 m | -11.6 dB | 0.86 |
| `202743` | 40 m | -0.6 dB | 0.43 |
| `232842` | 160 m | -7.7 dB | 0.29 |
| `111852` | 693 kHz | +13.1 dB | **0.78** |
| `112151` | 724 kHz | +14.5 dB | 0.52 |

The two mediumwave rows are the extreme of the set in both columns: the
second antenna 13 to 15 dB *hotter* rather than colder, and the noise
between the arms 0.52 to 0.78 correlated rather than 0.1 to 0.4. Nothing
about the covariance handling had to change to cope with them, and
Finding 16 shows the nuller reaching its ceiling on `111852`.

On `231724` the ideal MVDR weight and the ideal MRC weight are 51 degrees
apart, so the cross term is doing real work there.

**Confine the covariance to the passband; do not diagonalise it.** One
capture would have led the other way.

The four 60 m rows are measured differently from the rest and the
difference matters: there is no dead air in three of them, so the noise
figure is taken in the correlator's own guard bins - 300 to 2850 Hz on
the modem's side of the tuned frequency, off the 50 Hz carrier grid -
rather than in a silent stretch. `111328`, which has no signal at all, is
measured both ways and agrees to 0.2 dB, which is what licenses the
other three.

They also stretch the coherence range at both ends. `111734` at 0.86 is
the highest noise coherence in the set and `111328` at 0.14 nearly the
lowest, on the same two antennas four minutes apart. What separates them
is how far the band noise stands above the receivers': `111328` is 10 dB
quieter than its neighbours and both arms are close to their own noise
floors, which are independent. When external noise dominates it is
shared, and the inter-arm coherence goes with it. See Finding 10.

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
runs from **0.066 to 0.86**, with a median around 0.28. The 60 m captures
added the top of that range: 0.72, 0.75 and 0.86 on three of them, and
0.14 on the fourth (Finding 4).

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

The 60 m captures sharpen it. At 0.86 the gate is not merely unable to
separate signal from noise, it is a formality: every block passes it
whatever is or is not there. And the thing that decides where in the
0.14-0.86 range a path sits is not the antennas or the band but how far
the band noise is above the receiver's own, which changes with the hour
and with the weather.

## Finding 11: the MVDR solve returns exactly zero, and mutes the second antenna

The operator's report was that the RADE V1 lock sat at about -25 dB gain,
which did not feel right. It is not a gain. It is `div_mvdr2()` returning
`(0, 0)`, which `div_apply_weight()` renders as its floor:

```c
div_track_gain = (mag > 1.0e-9) ? 20.0 * log10(mag) : -27.0;
```

so the menu shows **-27.0 dB with phase exactly 0**, which is
indistinguishable from a tracked answer and is not one. The weight
actually applied then slews towards zero and stays there: on the three
60 m captures with a signal the recorded `div_cos, div_sin` has a median
magnitude of **-175, -119 and -86 dB**. Arm 1 was muted for the whole
minute. The operator was listening to ADC0 alone with the menu reporting
a lock, a quality of 0.8 and a pilot SNR of 8 dB - all of which were
true, and none of which reached the audio.

`div_mvdr2()` has exactly one exact-zero exit:

```c
const double d2 = denre * denre + denim * denim;
if (!(d2 > 1e-30)) { *wr = 0.0; *wi = 0.0; return; }
```

`den = r11*h0 - r01*h1` is a product of two *energies*. On the RADE path
`h` comes from pilot correlations (`|d0|^2`, `d1 conj(d0)`) and `R` from
160-sample DFT bins, both built from samples of order 1e-4, so `d2` is
that scale to the eighth power. It has no fixed magnitude, and the
threshold does.

Measured per capture, over every locked modem frame, `d2` against the
1e-30 the guard tests it against:

| capture | band / DDC | frames | zero | `d2` median | decades vs 1e-30 |
|---|---|---|---|---|---|
| `213155` | 40 m, 48 k | 419 | **0 %** | 8.4e-29 | +1.9 |
| `232052` | 80 m, 48 k | 41 | 49 % | 1.0e-30 | +0.0 |
| `233241` | 40 m, 48 k | 194 | 71 % | 5.1e-31 | -0.3 |
| `233133` | 40 m, 48 k | 186 | 65 % | 2.1e-31 | -0.7 |
| `111051` | 60 m, 192 k | 295 | 66 % | 1.7e-31 | -0.8 |
| `231724` | 80 m, 48 k | 172 | 91 % | 1.5e-31 | -0.8 |
| `111734` | 60 m, 192 k | 306 | **100 %** | 5.1e-32 | -1.3 |
| `110923` | 60 m, 192 k | 280 | **100 %** | 2.0e-34 | -3.7 |

The threshold sits *inside* the operating range - `232052` straddles it
exactly - and every capture in the set except the loudest one spends most
of its frames below it. This is not a 60 m problem and not a 192 kHz
problem; those captures are simply the quiet end of a distribution the
guard was always cutting through.

### It is scale, not a singular matrix

The guard is presumably meant to catch a singular `R`, and the covariance
here really is highly correlated between the arms (0.57 to 0.80), so that
had to be ruled out rather than assumed. Replaying each capture with the
input samples multiplied by a constant settles it: a factor of ten raises
`d2` by **eight decades** and changes nothing else, because both `R` and
`h` scale together and the solve normalises arm 0 to unity.

| capture | scale 1 | 10 | 100 | 1000 |
|---|---|---|---|---|
| `110923` | 100 % zero, jitter 0.00000 | 0 %, 0.15349 | 0 %, 0.15349 | 0 %, 0.15349 |
| `111051` | 66 % zero, jitter 3.61813 | 0 %, 8.18489 | 0 %, 8.18489 | 0 %, 8.18489 |
| `111734` | 100 % zero, jitter 0.00000 | 0 %, 0.53162 | 0 %, 0.53162 | 0 %, 0.53162 |
| `233133` | 65 % zero, jitter 0.16862 | 0 %, 0.31140 | 0 %, 0.31140 | 0 %, 0.31140 |
| `233241` | 71 % zero, jitter 0.08265 | 0 %, 0.21365 | 0 %, 0.21365 | 0 %, 0.21365 |
| `213155` | 0 % zero, jitter 0.43416 | 0 %, 0.43416 | 0 %, 0.43416 | 0 %, 0.43416 |

Bit-identical from ×10 upward: once the guard stops firing the answer is
scale-invariant, which is what says the matrix was never singular.
`213155`, where the guard never fires at all, is identical at ×1 too -
the control that says the ×10 replay is not doing anything else.

Acquisition, lock uptime, time to first lock and the solve count are
unchanged at every scale. The detector never saw this; only the weight
did.

### What it costs, decode-scored

Three librade receivers per capture plus a fourth driven by the weight
sequence the same code produces with the guard out of the way. Mean
`rade_snrdB_3k_est()`; sync was 100 % on every stream except where noted,
so the SNR column is the one that separates them (Trap 3).

| capture | arm 0 | arm 1 | as it ran | unguarded | shipped vs better arm | unguarded vs better arm |
|---|---|---|---|---|---|---|
| `110923` 60 m | 9.8 | 8.2 | 9.8 | 11.5 | +0.0 | **+1.7** |
| `111051` 60 m | 9.7 | **12.2** | 10.2 | 13.4 | **-2.0** | **+1.2** |
| `111734` 60 m | **7.3** | 6.8 | 7.3 | 9.2 | +0.0 | **+1.8** |
| `213155` 40 m | 7.2 | 9.6 | 10.0 | 10.0 | +0.5 | +0.5 |
| `233133` 40 m | 9.4 | 1.5 | 9.9 | 10.0 | +0.5 | +0.6 |
| `233241` 40 m | 10.3 | 4.8 | 10.3 | 9.9 | -0.0 | -0.4 |

Two things to take from this.

**On the 60 m captures the defect costs 1.7 to 1.8 dB**, and on `111051`
it costs 2.0 dB against the better antenna outright, because there arm 1
*was* the better antenna by 2.5 dB and the zero weight is precisely the
instruction to throw it away. "As it ran" equals arm 0 alone to a tenth
of a decibel on all three, which is what a muted second branch looks like
from the decoder.

**On the 40 m captures it costs almost nothing**, even where the guard
fires on two thirds of frames. Arm 1 is 5 to 8 dB worse there, so a
weight near zero is close to right anyway, and the frames that do solve
plus the 0.15 slew keep the applied weight somewhere sane between them.
That is why this survived the work in "What was changed, and what it
scored": those captures cannot see it. It also means the +0.5 / +0.5 /
-0.0 dB recorded there is not evidence that the repaired estimator is
working - the solve behind it was returning zero most of the time.

`233241` scores 0.4 dB *worse* unguarded, which is the same
mis-estimated-oracle capture Finding 3 already flags; it is the one place
where doing nothing happened to be better than the answer.

### Fixed

`div_mvdr2()` now tests whether `den` is small *compared with the two
terms it is the difference of*, which is the catastrophic-cancellation
condition and is scale-free, instead of comparing it with a constant. See
"What was changed, and what it scored" for the after figures: the zero
disappears on all eight captures, the weight becomes bit-identical to the
x10 control above, and detection is untouched.

## Finding 12: the USB pilot bank, confirmed on air at last

Four captures on 60 m (5.3685 and 5.3715 MHz **USB**, RADE V1, 192 kHz
DDC, filter +150 to +2850). `div_rade_side_expected()` derived
`expect_bank = 1` for all four, as Finding 7 said it would, and this time
there was a station there.

| | `110923` | `111051` | `111328` | `111734` |
|---|---|---|---|---|
| acquisitions | 1 | 2 | **0** | 2 |
| time to first lock | 2.05 s | 2.05 s | never | 3.41 s |
| lock uptime, replayed cold | 65 % | 68 % | 0 % | 70 % |
| mean quality | 0.81 | 0.60 | - | 0.82 |
| mean pilot SNR | 6.4 dB | 1.7 dB | - | 6.8 dB |

**Bank 1 acquires, confirms, tracks and holds on a real signal.** That
was the single most valuable missing measurement in this document and it
is now made. The mapping in `rade_correlator.c` is right on both
sidebands, not just the one it was measured on.

The frame inversion is confirmed a third time, and for the first time
with a RADE signal rather than voice. With the modem on USB its carriers
must land at -2200 to -800 Hz in the tapped buffer. Energy there against
the mirror band at +800 to +2200:

| `110923` | `111051` | `111328` | `111734` |
|---|---|---|---|
| +18.1 dB | +11.9 dB | +1.8 dB | +22.7 dB |

`111328`'s 1.8 dB is the control: no signal, no asymmetry.

### Bank 1 again, on 160 m

`232842` is a second, independent bank-1 confirmation, on a different
band, at a different averaging time, three months of propagation away
from anything the mapping was derived on. 1.987 MHz `DIGU`, filter +500
to +2500, `expect_bank` 1, averaging 5.6 s.

| | `232842` |
|---|---|
| acquisitions, replayed cold | 1 |
| time to first lock | 3.58 s |
| lock uptime, replayed cold | 94 % |
| median quality | 0.51 |
| median pilot SNR | +0.1 dB |
| modem band against its mirror, ADC0 | **+11.3 dB** |
| modem band against its mirror, ADC1 | +1.7 dB |

94 % uptime from a single acquisition is the best in the set after
`213155`. The frame inversion holds for a fourth time: with the modem on
USB the carriers land at -2200 to -750 Hz in the tapped buffer, 11.3 dB
above the mirror band on the antenna that can hear them, and 1.7 dB on
the one that cannot - which doubles as a control inside a single capture.

`202743` is the matching bank-0 case at 192 kHz - 7.09203 MHz `DIGL`,
averaging 1.9 s - and is included for completeness rather than for
weight. It is the most marginal RADE capture in the document: quality
0.15, pilot SNR -7.5 dB, eight acquisition attempts in the minute, and
5 dB *more* energy in the rejected sideband than in the modem's own. It
is used below only where a second, weaker data point is worth having.

### 192 kHz changes nothing that was measured here

The first captures in the set at a DDC rate other than 48 kHz. `decim`
goes from 6 to 24, `ntaps` from 97 to 385, and the analysis block becomes
170.7 ms - **longer than the 120 ms modem frame** for the first time, so
a block now carries one or two frames rather than always less than one.
Measured: 1.23 solved frames per locked block on all three, 0 dropped and
0 skipped blocks in all four, acquisition timing indistinguishable from
the 48 kHz captures.

Frequency tracking settles rather than walking, which is the failure
Finding 2 fixed and the thing most likely to be rate-sensitive. On
`110923` `lock_f` stays inside 1.2 Hz for the whole minute (sd 0.27 Hz
over the first half, 0.20 over the second); on `111734` it converges and
then holds to sd 0.01 Hz. `111051`'s second half is noisier at 4.1 Hz
because it re-acquires onto a different station mid-capture. Nothing
approaches the +/-60 Hz `RADE_FREQ_LIMIT`.

Threshold behaviour is unchanged too. Lock uptime over `use_ratio` 1.75
to 3.00 varies by 3.1 points on `110923`, 4.6 on `111051` and 0.3 on
`111734`, with no monotone trend in any of them - the same
scatter-not-trend the 40 m captures gave.

## Finding 13: the estimator measured the antenna difference correctly

The 60 m pair is badly asymmetric - a main antenna on ADC0 against an
untuned doublet on ADC1 - so it is a direct test of whether the
correlator's `h` and `R` describe the two arms honestly, or whether the
weight it produced was wrong because the measurement behind it was.

The measurement is honest. `acc_x01/acc_x00` and `acc_r11/acc_r00` from
the correlator, against the same two quantities measured independently
from the raw blocks by FFT - the channel over the modem band, the noise
over the correlator's own guard bins:

| | correlator `h1/h0` | independent | correlator `r11/r00` | independent | corr `R` coh | indep |
|---|---|---|---|---|---|---|
| `110923` | -2.1 dB, +82 deg | -2.8 dB, +83 deg | -1.7 dB | -2.1 dB | 0.80 | 0.75 |
| `111051` | -11.4 dB, -36 deg | -12.9 dB, -73 deg | -14.8 dB | -13.1 dB | 0.57 | 0.72 |
| `111734` | -12.3 dB, +24 deg | -13.1 dB, +31 deg | -12.0 dB | -11.6 dB | 0.79 | 0.86 |

Channel magnitude agrees to 1.5 dB, noise ratio to 1.7 dB, phase to 7
degrees on two of three. The exception is `111051`, 37 degrees out, and
it is the capture with a mean pilot SNR of 1.7 dB and two stations in it
- the independent figure averages the whole minute over the whole modem
band and cannot separate them either. Nothing here suggests the estimator
is fooled by a weak second antenna.

### Where the guard bins are not honest: `232842`

The same comparison on 160 m, where the two arms are much further apart
than on 60 m, finds one column that does not hold up.

| | correlator | independent, from the raw blocks |
|---|---|---|
| `h1/h0` | -18.9 dB, +22.8 deg | -17.3 dB (noise-subtracted band), -17.8 dB (carrier comb) |
| `r11/r00` | **-7.7 dB** | **-3.9 dB** |
| `R` coherence | 0.285 | 0.273 |
| arm 1 advantage | **-11.2 dB** | **-15.0 dB** |

The channel and the noise *coherence* agree, as they did on 60 m. The
noise **ratio** does not: the correlator reads arm 1's noise 3.8 dB lower
than the same guard region measured directly, and that error passes
straight into the per-arm figure the Best objective acts on, making arm 1
look 3.8 dB better than it is.

The cause is visible in the guard bins themselves. They are 50 Hz-wide
rectangular DFT bins taken inside one 20 ms pilot symbol, at
`lock_f + k*50 Hz` for k = 6..14 and 45..57, skipping the modem's own
carriers at k = 15..44. Measured from the raw blocks, the two bins that
sit immediately beside the modem span read hot on ADC0 and flat on ADC1:

| guard bin | k=6 | k=10 | **k=14** | **k=45** | k=48 | k=57 |
|---|---|---|---|---|---|---|
| ADC0 | -23.4 dB | -23.1 | **-19.4** | **-20.2** | -22.9 | -23.4 |
| ADC1 | -26.7 dB | -26.6 | -26.4 | -26.6 | -26.9 | -26.9 |

That is modem leakage, and it can only bias the arm that can hear the
modem. On `232842` ADC0's modem stands 11 dB above its own floor and
ADC1's stands 1.9 dB above, so the leakage lands almost entirely on ADC0,
inflates `acc_r00`, and pushes `r11/r00` down. On `202743`, where the
modem is 5.8 dB above the floor and the two arms are within a decibel,
the correlator's -0.59 dB and the independent -0.08 dB agree to half a
decibel. Two captures is a direction, not a law: what would settle it is
one capture with a strong modem and one deaf arm, and one with a strong
modem on both.

The pick was still right on `232842` - -11.2 dB and -15.0 dB both say
ADC0, decisively - so this is an accuracy problem in a displayed number
and a margin problem for Best, not a wrong answer here.

**What it means for the antennas.** The doublet is 11 to 13 dB down on
signal, which reads like the worse antenna and is not: its noise is 12 to
15 dB down as well. On `111051` it decoded **2.5 dB better than the main
antenna** (12.2 against 9.7). A branch can be much quieter and much less
sensitive at the same time, and only the ratio decides which to use -
which is exactly what MVDR computes and what Finding 11 threw away. The
weight the same numbers give with the guard out of the way is -1.1 dB at
-48 degrees, +20.1 dB at +18, and +4.9 dB at +19: on two of three
captures the correct answer is to weight the *quiet* antenna up, not
down.

This is also the clearest case yet for showing per-arm SNR in the menu.
Nothing an operator can see distinguishes "ADC1 is 12 dB down because it
is deaf" from "ADC1 is 12 dB down because it is quiet", and the two want
opposite weights.

### And on the no-signal capture

`111328` was taken as band noise with a weak coherent source audible in
it. Averaged over the minute in 50 Hz bins across the correlator's whole
+/-3 kHz view, **no bin exceeds 0.35 inter-arm coherence** and the mean
over 300 to 2850 Hz is 0.10 - the lowest in the capture set. Whatever the
source is, it is not a steady common-mode signal the array could null,
and it is not what a two-branch combiner is for. The most coherent
features in the capture (0.2 to 0.35) sit at +2.6 kHz and +3.6 to
+3.9 kHz, outside the passband and mostly outside the decimator.

## Finding 14: the weight clamp, and an antenna-selection objective

This one started as a question about `DIV_MAX_WEIGHT` and ended as a
fourth objective in the menu.

### Why a weight clamp is an awkward control

`src/receiver.c` forms

```c
i_sample = i0 + (div_cos * i1 - div_sin * q1);
```

so arm 0 is hard-wired at unity and `w` is a **ratio**, not a pair of
gains. The control is therefore asymmetric in a way the clamp inherits:
"ignore arm 1" is `w = 0`, exact and always reachable, while "ignore
arm 0" is `w -> infinity` and the clamp decides how close one may get.

And how large `w` has to be before arm 0 stops contributing depends on
arm 1's *level*, not on which antenna is better. On `111051` the doublet's
noise is 14.8 dB below the main antenna's, so even at the +20 dB clamp
arm 0 still supplies 23 % of the output noise power. Swap the two antennas
over and the same physical preference is expressed as `w = -20 dB`, well
inside the clamp with room to spare.

### The clamp value is not the problem

Only one capture goes near it:

| capture | median \|w\| | p90 | frames at or over +20 dB |
|---|---|---|---|
| `110923` | -1.1 dB | 0.0 dB | 0 % |
| `111051` | **+20.1 dB** | +21.2 dB | **52 %** |
| `111734` | +4.9 dB | +8.7 dB | 0 % |
| `213155` | -0.5 dB | +2.3 dB | 0 % |
| `233133` | -9.0 dB | -8.1 dB | 0 % |
| `233241` | -14.7 dB | -11.9 dB | 0 % |

And clamping costs almost nothing. Output SINR computed from the
correlator's own `h` and `R` - a pilot-domain metric, so Trap 1 applies
and it is used here only to compare weights that differ in magnitude
under one model - puts the cost of the +20 dB clamp at **0.02 dB** on
`111051` and 0.00 dB everywhere else. Tightening it to +9.5 dB costs
1.03 dB; to 0 dB, 2.31 dB.

So the dilemma is not the number. It is that **a weight on the rail is
unreadable**: `den = r11*h0 - r01*h1` collapses when the guard-bin
covariance carries the same inter-arm signature as the signal, which is
what "the dominant interference is the band noise both antennas hear"
means, and the noise coherence on these captures runs 0.57 to 0.86. Large
`|w|` therefore has two causes that look identical from outside - arm 1
genuinely deserves the weight, or the denominator nearly cancelled - and
no clamp value separates them, because what separates them is not in
`|w|`.

One intuition that had to be abandoned: that fixing Finding 11's guard
would push frames onto the rail, both being about a small `den`. It does
not. The frames the guard zeroed want the *same* weight as the frames it
passed - median `|w|` -9.0 against -9.1 dB on `233133`, -14.7 against
-14.7 on `233241`. The guard fired on absolute level and nothing else.
The two meet only on `111051`, where the honest answer is large
everywhere.

### DIV_AUTO_BEST

Since the combiner cannot express selection, the missing endpoint has been
given a name instead: a fourth objective beside Off, Null and Sum that
hands the output to whichever antenna is measuring better.

It needs one number no reference previously published, the per-arm SNR,
and in three of the four cases that number was already sitting in the
accumulators:

| reference | signal | noise | new measurement needed |
|---|---|---|---|
| RADE V1 | `acc_x00`, `acc_x01` | `acc_r00`, `acc_r11` (guard bins) | none |
| Digital I/Q | `sig_xx`, `sig_xy` | `r00`, `r11` (occupancy split) | none |
| Window | window power per arm | tracked floor | a minimum-statistics floor |
| Carrier | as Window | as Window | as Window |

In each case the advantage of arm 1 is `|h1/h0|^2 * (N0/N1)` - the
channel ratio the Sum weight already computes, divided by the noise
ratio. Selecting arm 0 is `w = 0`; selecting arm 1 is `w` at the clamp
with the co-phasing angle, which is not a switch but is the nearest
reachable point to one, and leaves arm 0 combining in 20 dB down.

### Two traps in the floor tracker, one of them a real result

The Window and Carrier references have no noise measurement at all, and
per-arm SNR is **not identifiable** from a single window's second-order
statistics: `Sxx`, `Syy` and `|Sxy|` give three equations in four
unknowns, and coherence pins down only the *product* of the two arms'
signal fractions. The information has to come from bins with no signal in
them, or from times with no signal in them. Window and Carrier have
neither to hand, so they track a floor over time.

Doing that naively fails, and fails *confidently*. A minimum taken over
the power smoothed at the operator's averaging time never sees a gap -
10.5 s is longer than the pause between two overs and far longer than the
one between two syllables - so the minimum still holds signal, on both
arms, in the same ratio as the signal itself. Everything cancels and the
answer is exactly 0.0 dB, which reads as "the arms are equal" and means
"this method has told you nothing". It did precisely that on all four
60 m captures, against a truth of +2.5 dB on one of them. The floor is
now tracked on a separate 0.5 s smoothing, and an estimate is published
only where both arms stand 6 dB clear of their own floor.

The second trap was ordinary and is recorded because it wasted a
measurement: `div_arm_publish(div_arm_from_floor(..., &db), db)` reads
`db` before the call that fills it, argument evaluation order being
unspecified, and produced a bit-exact 0.0 dB that looked exactly like the
degeneracy above. Two different faults with the same signature, found one
after the other.

### What the four references actually pick

Selection against the arm that decodes better (Findings 3 and 11) or
measures better in the passband (Findings 6 and 7):

| capture | better arm | Window | Carrier | RADE V1 | Digital I/Q |
|---|---|---|---|---|---|
| `110923` | ADC0 | ADC0 | ADC0 | ADC0 | ADC0 |
| `111051` | ADC1 | ADC1 | ADC1 | ADC1 | **ADC0** |
| `111734` | ADC0 | ADC0 | ADC0 | ADC0 | ADC0 |
| `213155` | ADC1 | ADC1 | ADC1 | **ADC0** | **ADC0** |
| `233133` | ADC0 | ADC0 | ADC0 | ADC0 | ADC0 |
| `233241` | ADC0 | ADC0 | ADC0 | ADC0 | ADC0 |
| `235853` | ADC1 | **ADC0** | **ADC0** | no lock | **ADC0** |
| `000012` | ADC1 | ADC1 | ADC1 | no lock | **ADC0** |
| `000209` | ADC1 | ADC1 | ADC1 | no lock | ADC1 |
| `000328` | ADC1 | ADC1 | ADC1 | no lock | **ADC0** |
| `232842` | ADC0 | ADC0 | **ADC1** | ADC0 | ADC0 |
| `111852` | ADC1 | ADC1 | ADC1 | no lock | ADC1 |
| `112151` | ADC0 | ADC0 | ADC0 | no lock | ADC0 |
| **correct** | | **12/13** | **11/13** | **6/7** | **7/13** |

`202743` is deliberately absent: decode makes ADC0 the better arm on
synced frames (305 against 257) and ADC1 the better arm on mean SNR (+2.7
against -0.3 dB), which is Trap 3 pointing both ways at once. Window and
Carrier pick ADC1 there, RADE V1 and Digital I/Q pick ADC0, and there is
no honest way to mark any of them.

The two mediumwave captures are the easiest rows in the table and all
three references that can run get them right, including the case that
matters most for a selection mode: on `112151` the second antenna is
14.5 dB **louder** and 1.6 dB **worse**, and every reference picks the
quiet one. Loudness is not the statistic and the estimator knows it.

The wideband floor tracker is the best of the four despite being the
crudest, and it is right on `213155` where the RADE guard-bin statistic is
wrong - Finding 13 now has a mechanism for that, and `232842` shows the
Carrier reference failing the same way in the other direction, reading
+5.5 dB for an arm that is 11 to 15 dB worse. Its one miss, `235853`, has the two antennas 0.53 dB apart - inside
the selection hysteresis, so the "wrong" pick costs half a decibel.
Digital I/Q is the weakest by a distance, which is consistent with the
correction under Finding 8: its noise bins come from an occupancy split
with no false-alarm control.

RADE V1 reports nothing on the four voice captures, correctly - there is
no pilot to lock to and therefore no per-arm measurement, and the mode
holds rather than guessing.

### Decode-scored, Best is a floor and Sum is a ceiling

Against the better arm, with the Finding 11 fix in place:

| capture | Sum | Best, RADE V1 statistic | Best, Window statistic |
|---|---|---|---|
| `110923` | **+1.7** | +0.1 | +0.0 |
| `111051` | +1.2 | **+1.9** | -1.0 |
| `111734` | **+1.8** | +0.0 | +0.0 |
| `213155` | +0.5 | **-2.5** | +0.9 |
| `233133` | **+0.6** | -0.1 | +0.0 |
| `233241` | -0.4 | -0.0 | -0.1 |
| `232842` | **+0.7** | +0.0 | +0.0 |
| `202743` | **-2.5** | -3.1 | - |
| mean | **+0.90** | -0.43 | -0.03 |

The mean row is over the original six and is left alone so the earlier
comparison still reads. `232842` behaves like the rest: Sum +0.7 dB over
the better arm, Best exactly level with it because it picked that arm.
`202743` is the outlier and is the marginal capture - Sum is 2.5 dB below
arm 1's mean SNR while being **16 synced frames ahead of it**, which is
Trap 3 again and the reason that row is not counted anywhere.

This is what selection is: it cannot beat the better antenna, and where
the two antennas are close - `110923` and `111734`, half a decibel to a
decibel and a half apart, which is where diversity is supposed to earn
its keep - real combining is worth 1.7 to 1.8 dB and selection collects
none of it. It wins on exactly one capture, `111051`, where arm 1 is the
better antenna and the MVDR solve is partly degenerate; there it beats Sum
by 0.7 dB.

A wrong pick is expensive: -2.5 dB on `213155` from the RADE statistic.
Selection has no coherence gate to hide behind - it acts on every block
where it has an estimate at all.

**Sum stays the default.** Best is worth having for the case the 60 m
captures found - one antenna much better than the other, where Sum's
answer is a large weight on a rail and hard to trust - and for
establishing what the antennas are actually doing, which is why the
per-arm figure is now on the menu whatever objective is running. It is
not a general improvement and the numbers above say so.

## Finding 15: the frequency loop has stable lock points 8.3 Hz apart

`232842` was recorded to answer one question - how well does RADE V1
track on 160 m - and the first thing it says is that the radio and a cold
replay of the *same samples* do not agree about where the station is.

| medians over t > 10 s | recorded by the radio | replayed cold from the same file |
|---|---|---|
| settled `lock_f` | **+16.11 Hz** | **+7.78 Hz** |
| quality | 0.440 | 0.507 |
| pilot SNR | -1.05 dB | +0.12 dB |

The difference is 8.34 Hz. One modem frame is `RADE_CORR_NMF`/`RADE_CORR_FS`
= 960/8000 = 120 ms, so the frame rate is **8.333 Hz**.

### Why it is stable, not a transient

The discriminator at `RADE_FREQ_ALPHA` measures the phase the pilot
correlation turns through from one frame to the next, minus the turn
`lock_f` already predicts. A residual of exactly one frame rate turns the
correlation through exactly 2*pi and reads as **zero error**. The
comment in `rade_correlator.c` says as much - "unambiguous over +/-4.17 Hz"
- and 4.17 Hz is half of 8.33.

So every offset `f_true + n*8.333 Hz` is an equilibrium, and the loop
sits at whichever one acquisition handed it. Forcing `lock_f` at
acquisition and letting the loop run confirms it directly. On `232842`,
medians over t > 10 s:

| forced start | settles at | quality | pilot SNR | frames tracked |
|---|---|---|---|---|
| +2 Hz | **-0.55** | 0.446 | -0.93 dB | 449 |
| 0, +4, +6, +8, +10 Hz | **+7.78** | **0.507** | **+0.12 dB** | **456** |
| +12, +14, +16, +18, +20 Hz | **+16.12** | 0.455 | -0.78 dB | 451 |
| +24 Hz | **+24.46** | 0.350 | -2.70 dB | 381 |

Four equilibria at -0.55, +7.78, +16.12 and +24.46 - spacings of 8.33,
8.34 and 8.34 Hz - each with its own basin, and the discriminator reading
-0.03 to -0.05 Hz of residual at all of them. The radio was sitting in
the +16.11 basin; the replay acquired into the +7.77 one.

The recorded series says the same thing more slowly: over the minute
`live_freq_off` walks from +17.92 to +16.06, about 1.9 Hz a minute,
converging on **its own** equilibrium rather than on the right one. At
that rate it would need four and a half minutes to cross one alias step,
and it never would, because there is no error signal pointing that way.

### Why acquisition cannot tell them apart

Acquisition correlates against one pilot symbol - `RADE_CORR_M` = 160
samples, 20 ms - and accumulates the **magnitude** over
`RADE_ACQ_PASSES` passes, so integrating longer sharpens the timing peak
and does nothing for the frequency one. A 20 ms observation resolves
frequency to about 50 Hz. The 5 Hz search grid is an order of magnitude
finer than the thing it is measuring.

Dumping the acquisition statistic against frequency at the moment of lock
on `232842` shows exactly that - a peak 60 Hz wide on a 100 Hz search:

| `acq_freq` | -25 | -15 | -5 | **+5** | +15 | +25 | +35 | +45 |
|---|---|---|---|---|---|---|---|---|
| sigma | 2.84 | 6.52 | 9.08 | **9.89** | 9.60 | 8.31 | 5.57 | 3.26 |

+15 Hz scores 97 % of the peak and +25 Hz scores 84 %. On a noisy minute
either can win, and both are more than one alias step from the truth.
`202743`, which is far weaker, is worse: its top five grid points sit
within 5 % of each other and span 30 Hz.

**The gap is structural.** Acquisition places the frequency to about
+/-25 Hz; tracking pulls in +/-4.17 Hz; the range between them is filled
with stable wrong answers 8.33 Hz apart.

### What it costs

Less than it looks, and not where an operator would guess.

| | at +7.78 Hz | at +16.12 Hz | difference |
|---|---|---|---|
| quality | 0.507 | 0.455 | -0.052 |
| pilot SNR | +0.12 dB | -0.78 dB | **-0.90 dB** |
| frames tracked | 456 | 451 | -5 |
| `h1/h0` | -18.87 dB, +22.8 deg | -18.92 dB, +23.5 deg | 0.05 dB, 0.7 deg |
| MVDR weight | reference | +0.05 dB, 2.3 deg | negligible |

Every median in this finding is taken over t > 10 s, so the +7.78 row
above and the "replayed cold" column at the top of the finding are the
same measurement. The radio's own column sits 0.27 dB below the
+16.12 row, which is not explained here: the recorded state is the state
*entering* each block and the radio had been locked for an unknown time
before the capture was armed, so its accumulators started somewhere the
replay's did not.

Decode-scored, with the weight from each fed to a separate librade
receiver over the same capture:

| stream | rx frames | in sync | mean SNR |
|---|---|---|---|
| arm 0 | 492 | 492 | 5.8 dB |
| arm 1 | 417 | 415 | 4.9 dB |
| weight from the +7.77 Hz lock | 487 | 487 | **6.6 dB** |
| weight from the +16.11 Hz lock | 487 | 487 | **6.5 dB** |

**0.1 dB.** The diversity weight is a *ratio* of two arms carried through
the same NCO and the same decimator, so a common frequency error cancels
out of it almost exactly - which is why the channel estimate is unmoved
and the audio is unaffected. What the alias actually costs is the
displayed pilot SNR (0.9 dB), the quality reading (0.37 to 0.32), and
margin against `RADE_USE_RATIO`: five frames out of 456 here, but on a
weaker signal
that margin is what a lock is made of. On `202743` the equilibria give
pilot SNR from -3.8 to -7.9 dB and lock uptime from 28 % to 73 %,
depending purely on which one acquisition happened to choose - though
that capture is marginal enough (quality 0.15, eight acquisitions in the
minute) that some of that spread is the signal and not the alias.

### The obvious remedy, not implemented

After a lock is confirmed, correlate at `lock_f`, `lock_f + 8.333` and
`lock_f - 8.333` and keep the strongest. One pilot symbol resolves 50 Hz,
which is six times the step, so the comparison is unambiguous even though
the *tracking* discriminator is not. It is three extra correlations at
lock, not per frame. Nothing here has been changed: this finding is
measurement only, and the decode column above is the argument for taking
the time to do it properly rather than quickly.

## Finding 16: mediumwave, where the noise is the coherent thing

Two captures below 1 MHz, `SAM` with a +/-4 kHz filter, both with ADC1 -
the untuned doublet - running 14.5 to 15.2 dB above ADC0 across the whole
passband. They are the first captures in the set where the inter-arm
noise is *more* correlated than not.

| | `111852`, 692.9 kHz | `112151`, 724.4 kHz |
|---|---|---|
| what is there | 693 kHz broadcast carrier, 43 dB over the in-band median | band noise, strongest features 19.5 dB over median |
| ADC1 - ADC0, passband | +15.2 dB | +14.5 dB |
| inter-arm coherence, passband | **0.982** | 0.524 |
| inter-arm coherence, off-carrier | 0.782 | - |
| noise `N1/N0` | +13.1 dB | +14.5 dB |

### `111852`: a strong carrier, and no diversity gain to be had

With a discrete carrier present, per-arm SNR is directly measurable -
signal in the carrier bins, noise over the rest of the passband - with no
model in the way:

| | ADC0 | ADC1 |
|---|---|---|
| carrier SNR | +34.0 dB | **+36.2 dB** |

ADC1 is 2.2 dB better, which is `h1/h0` = +15.3 dB against `N1/N0` =
+13.1 dB. The array, though, has almost nothing to add: the best fixed
weight anywhere in the plane scores **+36.4 dB**, 0.17 dB above simply
using ADC1. The reason is in the phases - the channel is at -55.7 degrees
and the noise at -64.8, nine degrees apart, with the noise 78 %
correlated. A two-element array cannot point at one and away from the
other when they arrive from the same direction.

Every objective finds that ceiling, which is the result worth having:

| stream | carrier SNR | vs the better arm | weight |
|---|---|---|---|
| Sum / Window | +36.23 dB | -0.00 | +14.95 dB, +55.9 deg |
| Sum / Carrier | +36.21 dB | -0.02 | +10.93 dB, +65.7 deg |
| Sum / Digital I/Q | **+36.38 dB** | **+0.15** | -0.20 dB, -33.4 deg |
| Best (all three) | +36.22 dB | -0.01 | +20.00 dB (the rail) |
| best fixed weight | +36.40 dB | +0.17 | -2.75 dB, -48.0 deg |

Digital I/Q's answer looks wrong and is not. It applies a weight of
essentially unity where MRC would want +15 dB, because with the noise 78 %
correlated MVDR is trading array gain for cancellation - and it comes out
0.15 dB ahead of everything else. This is the first capture in the set
where the passband-confined covariance is doing the job it exists for on
a signal rather than on an argument.

### Null reaches its ceiling on `111852`

Measured as output power in the +/-4 kHz passband against ADC0 alone,
over the settled part of the capture (t > 20 s, 234 blocks):

| | depth |
|---|---|
| loop, Null / Window | **-14.38 dB** |
| loop, Null / Digital I/Q | **-14.37 dB** |
| loop, Null / Carrier | -10.77 dB |
| best single weight over the whole minute | -14.36 dB |
| best weight recomputed every block | -14.96 dB |

Two of the three references are **at the ceiling** - indistinguishable
from the best constant weight, and within 0.6 dB of a weight recomputed
every 171 ms. The ideal weight barely moves (|w| sd 0.50 dB, phase sd
3.4 degrees over the minute), so there is nothing for a faster loop to
chase. Carrier gives up 3.6 dB because it co-phases on the carrier bin
alone, and the carrier's spatial signature is 9 degrees off the band's.

That is the answer to a question this document has been carrying since
Finding 5: on a genuinely common-mode source the nuller works, and works
as well as the geometry allows.

### `112151`: partial coherence, and a much smaller prize

| | depth |
|---|---|
| loop, Null / Window, Carrier, Digital I/Q | -0.76 to -0.79 dB |
| best single weight over the whole minute | -1.00 dB |
| best weight recomputed every block | -2.96 dB |

Only 2.4 dB of the passband is coherent between the arms here, so 3 dB is
all a nuller can ever take out. The loop gets to within 0.2 dB of the best
constant weight and leaves the remaining 2 dB, which a per-block weight
does collect: the ideal weight wanders far more than on `111852` (|w| sd
1.21 dB, phase sd 22.8 degrees), so the 4.8 s averaging the operator had
set is the limit, not the estimator. That is a real trade - shorter
averaging would collect it and would also make every false-alarm number
in this document worse.

### Both mediumwave captures also test the arm statistic

| | true arm 1 advantage | Window | Carrier | Digital I/Q |
|---|---|---|---|---|
| `111852` | +2.2 dB (carrier), +1.6 dB (coherent split) | +1.0 dB | +3.1 dB | +2.2 dB |
| `112151` | -1.6 dB (coherent split) | -2.2 dB | -1.2 dB | -0.1 dB |

Every reference is within 1.5 dB and every sign is right, on a pair where
one antenna is 15 dB louder than the other. That is the strongest
evidence so far that the per-arm figure Finding 14 added is measuring
what it claims to. It comes with a caveat: `arm_valid` is asserted on
only 4 to 32 % of blocks on the wideband references here, because a
continuous carrier raises the minimum-statistics floor along with itself
and the 6 dB clearance test rarely passes. The estimate is right when it
is offered and it is not offered often.

### What Best does with the +20 dB rail

On `111852` Best correctly chooses ADC1 and, because "use arm 1 only" is
only reachable as `w -> infinity`, applies the `DIV_MAX_WEIGHT` clamp:
`w` = +20.00 dB. The output is then ADC1 scaled by ten - **20 dB louder
than either antenna alone** - with ADC0 20 dB down inside it. The SNR is
right, the AGC step is not. Finding 14 predicted this from the algebra;
this is the first capture where an operator would actually hear it.

## False alarms

Locks produced on captures with no RADE signal anywhere. Cells are
`acquisitions / percent of the capture locked`.

| `use_ratio` | `231532` 80 m quiet | `232750` 80 m quiet | `111328` 60 m quiet | `233423` 20 m noise+SSB | `233615` 160 m QRM | `111852` 693 kHz | `112151` 724 kHz |
|---|---|---|---|---|---|---|---|
| 1.00 | 0 | 0 | 0 | 1 / 27 % | 1 / 90 % | 0 | **1 / 89 %** |
| 1.25 | 0 | 0 | 0 | 1 / 27 % | 2 / 66 % | 0 | **2 / 51 %** |
| 1.50 | 0 | 0 | 0 | 0 | 1 / 33 % | 0 | **3 / 42 %** |
| 1.75 | 0 | 0 | 0 | 0 | 0 | 0 | **1 / 15 %** |
| 2.00 | 0 | 0 | 0 | 0 | 0 | 0 | **1 / 15 %** |
| 2.25 | 0 | 0 | 0 | 0 | 0 | 0 | **1 / 9 %** |
| 2.50 and above | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

`111328` is the first dead-air capture at 192 kHz and on 60 m, and it
produces **no acquisition at any threshold from 1.00 upward** - the
cleanest column in the table. The blind-search false-alarm rate does not
change with the sample rate.

`112151` is the worst column in the table and the reason the threshold
policy below is now a *measured* margin rather than a comfortable one. It
is mediumwave band noise with no RADE anywhere near it, `expect_bank` is
-1 so both banks are searched, and it produces a lock at every threshold
up to and including 2.25 - 89 % of the minute at 1.00, still 9 % at 2.25.
It clears at 2.50 exactly. Quality on those false locks is 0.024 to
0.166, against 0.51 for the genuine lock on `232842`, so the *lock* is
false but the quality reading is honest about it.

`111852`, ten metres of coax and 31 kHz away, produces nothing at any
threshold. The difference is what the noise looks like, not where it is:
a single dominant carrier gives the timing-domain floor one large,
consistent peak to be measured against, and band noise does not.

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
below 2.00 - now below **2.50**, since `112151` locks at 2.25 and clears
only at the shipping value. The margin is one grid step wide. This supersedes an earlier suggestion of 2.00 that was based
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
- **Bank 1 is the USB bank, and it correlates on air.** Three 60 m USB
  captures acquire, confirm, track and hold in bank 1, 65 to 70 % uptime,
  quality 0.60 to 0.82, and `232842` does the same on 160 m from a single
  acquisition at 94 % uptime (Finding 12). Both banks are now measured on
  real signals rather than one measured and one derived, and bank 1 on
  two bands.
- **The tapped buffer is inverted with respect to RF, on both sidebands.**
  An LSB filter of -2850..-150 puts the signal at +150..+2850 in the
  tapped frame; a USB filter of +150..+5150 puts it at -5150..-150
  (Finding 7), and a USB RADE signal puts its carriers at -2200..-800,
  11.3 to 22.7 dB above the mirror band on four captures across 60 m and
  160 m (Finding 12). Checked on voice and on the modem, on both
  sidebands.
- **The flat scalar channel model is right.** `h1/h0` measured per
  subcarrier varies by +/-0.3 to +/-0.63 dB in magnitude and +/-3 to
  +/-12 degrees in phase across 750-2200 Hz. Differential delay between
  the arms is under 6 us. A single complex weight is the correct model.
  This rests on the RADE captures; a voice passband cannot resolve it
  either way (Finding 6).
- **The per-arm statistic gets the sign right.** Across thirteen captures
  it picks the antenna that decodes or measures better 11 to 12 times out
  of 13 on the wideband references, including the mediumwave pair where
  the better antenna is 14.5 dB *quieter* than the other (Findings 14 and
  16). Its accuracy is another matter - see Finding 13 on the guard bins.
- **Every reference holds correctly when there is no signal.** On all five
  no-signal captures RADE V1 reported locked 0.00, holding 1.00,
  coherence 0.003-0.008 (0.00 recorded and 0 acquisitions replayed on
  `111328`), and Digital I/Q never produced a weight. On the
  voice captures all three references hold through the gaps between overs.
  None of them invents an answer from noise, including with the 160 m
  interferer at full strength on ADC0.
- **Reception is often close to anti-phase, but not always.**
  `arg(h1/h0)` measured -177, -161, -162, -105, -7 and -4 degrees across
  the 40/80 m overs, and +82, -36 and +24 on 60 m. It is just the path;
  nothing structural, and the 60 m set shows the whole circle is in use.

## What is still open

- **The zero-weight guard is measured but not fixed.** Finding 11. What
  is missing before it can be is the corrected weight scored across the
  whole capture set rather than the six it was scored on here, and an
  answer to what `111051` does when the corrected weight sits on the
  `DIV_MAX_WEIGHT` rail. Everything else in this document that scores the
  RADE combiner needs re-reading once it is: the "after" figures under
  "What was changed, and what it scored" were taken with the solve
  returning zero on most frames.
- **The frequency alias is measured and not fixed.** Finding 15. The
  remedy is three extra correlations at lock and is described there; what
  is missing before writing it is a capture where the alias actually
  breaks a lock, so the fix has something to be scored against. Decode
  says it costs 0.1 dB on a strong signal, which is not an argument for
  rushing it.
- **Threshold policy needs more dead air, and `112151` narrowed the
  margin.** Six quiet captures now say 2.50 is safe and 2.25 is not - the
  boundary is one grid step below the shipping value rather than two, and
  the capture that moved it is mediumwave band noise, a kind of spectrum
  the set had never held before. Dead-air captures are cheap and need no
  station; ones from outside the amateur bands are cheaper still.
- **No capture yet has a wanted *modem* signal and strong common-mode
  noise.** `111852` closes half of this: a wanted signal with inter-arm
  coherence 0.982, where the nuller reaches its ceiling and Digital I/Q's
  passband-confined covariance comes out ahead of everything else
  (Finding 16). But it is `SAM`, not RADE, so it still says nothing about
  the *pilot-domain* covariance. What is wanted is a RADE station on a
  path with obvious common-mode noise.

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
  lock would let the replay be checked against the radio. `232842` shows
  what that costs: 351 of 351 blocks differ, and the reason turned out to
  be Finding 15 rather than the harness - the radio and the replay were
  tracking two different equilibria. `--verify` cannot tell those apart
  from a broken replay, which is precisely why it needs a capture armed
  cold.
- **The capture writer does not flag a retune.** `202743` moves 85 kHz at
  block 9 and `rec_flags` stays zero for all 351 blocks. Devtool defect,
  and only a nuisance while the devtools exist, but it means the flag
  cannot be trusted to find context changes in an existing file.

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

**Read this table with Finding 11.** On `233133` and `233241` the solve
behind these numbers was returning a weight of exactly zero on 65 % and
71 % of frames, and on those two captures arm 1 is 5 to 8 dB worse, so
zero is close to right and the defect is invisible here. Only `213155`
exercised the repaired estimator on most of its frames. The repair is not
in question - it is measured in the covariance and the channel, in
Findings 1 and 2 - but the decibels in this table are a weaker
confirmation of it than they look.

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

### `src/diversity_auto.c` — the MVDR guard is relative, not absolute

Finding 11. `div_mvdr2()` rejected a solve on `d2 > 1e-30`, an absolute
test on a quantity with no absolute scale, and returned a weight of
exactly zero when it fired - muting the second antenna and showing the
operator a -27 dB "tracked" value with phase 0. It now tests `d2` against
the two terms `den` is the difference of, scaled by `DIV_MVDR_EPS`, which
is the catastrophic-cancellation condition it was presumably always meant
to be.

| capture | zero frames before | after | weight jitter before | after |
|---|---|---|---|---|
| `110923` 60 m | 100 % | **0 %** | 0.00000 | 0.15349 |
| `111051` 60 m | 66 % | **0 %** | 3.61813 | 8.18489 |
| `111734` 60 m | 100 % | **0 %** | 0.00000 | 0.53162 |
| `231724` 80 m | 91 % | **0 %** | 0.07595 | 0.37029 |
| `232052` 80 m | 49 % | **0 %** | 0.12367 | 0.07455 |
| `233133` 40 m | 65 % | **0 %** | 0.16862 | 0.31140 |
| `233241` 40 m | 71 % | **0 %** | 0.08265 | 0.21365 |
| `213155` 40 m | 0 % | 0 % | 0.43416 | 0.43416 |

Every "after" jitter is bit-identical to the x10-input control in
Finding 11, which is what says the answer is now the scale-invariant one
the algebra always described. `213155`, where the guard never fired, is
unchanged to the last digit.

Decode-scored against librade, against the better antenna alone:

| capture | before | after |
|---|---|---|
| `110923` 60 m | +0.0 | **+1.7** |
| `111051` 60 m | -2.0 | **+1.2** |
| `111734` 60 m | +0.0 | **+1.8** |
| `213155` 40 m | +0.5 | +0.5 |
| `233133` 40 m | +0.5 | **+0.6** |
| `233241` 40 m | -0.0 | -0.4 |

RADE V1 now beats the better antenna on five of six. Detection is
untouched - identical acquisitions, lock uptime and time to first lock on
all eight captures, the guard being downstream of every decision the
detector makes.

`DIV_MAX_WEIGHT` is deliberately left at 10.0. Finding 14 measures the
cost of that clamp at 0.02 dB on the one capture that reaches it.

### `src/diversity_auto.c`, `src/rade_correlator.c` — antenna selection

Finding 14. A fourth objective, `DIV_AUTO_BEST`, and the per-arm SNR it
acts on, published by all four references and shown on a second status
line whatever objective is running. Measured in Finding 14: it picks
correctly on 9 of 10 captures from the wideband references, 5 of 6 from
RADE V1 and 5 of 10 from Digital I/Q, and decode-scores 1.3 dB behind Sum
on average. It is a fallback, not a default.

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

Findings 11, 13 and 15 need things the committed tools do not provide.
They were taken from a throwaway copy of
`build/rade_correlator_tunable.c` - the generated file `score_rade`
already `#include`s - with four edits and nothing else:

- a `double instr_scale` applied to `arm0[]` and `arm1[]` where
  `rade_corr_process()` reads them, which is what the scale table in
  Finding 11 sweeps;
- a dump of `acc_r00, acc_r11, acc_r01, acc_x00, acc_x01` and the
  determinant `d2` immediately before the `rade_mvdr_weight()` call in
  `rade_track()`, extended for Finding 15 with `lock_f` before and after
  the update, the discriminator's `df`, and the `nudged` flag;
- a `double instr_force_f` applied to `lock_f` immediately after
  acquisition sets it, which is what the equilibrium table in Finding 15
  sweeps - it is one assignment and it changes nothing else about the
  run;
- a dump of the acquisition statistic `sf` for every (bank, frequency)
  cell as it is computed, which is the acquisition surface table.

Driven by `divcap_replay()` with default options it reproduces the
shipping path exactly; `--weights` writes the per-block weight in
`replay_rade`'s format, which is what `score_rade --weights fixed=...`
takes for the "unguarded" column. `src/` is not touched, and the copy is
not worth committing - two edits against a generated file are quicker to
redo than to maintain.

The independent channel and noise figures in Findings 13 and 16 need no
tools at all: read the blocks out of the `.divc` with the layout in
`src/diversity_capture.h`, FFT each arm, and take the cross-spectrum over
the modem band for `h1/h0` and over the guard bins for `R`. Being a
separate implementation is the whole point of them.

Two cautions for anyone redoing the Finding 16 arithmetic. The block
record is 208 bytes with the layout in `src/diversity_capture.h`; the
`double centre` after `int32_t pad0` is padded to an 8-byte boundary, and
mis-indexing there silently swaps `width` for `bank` and every field
after it. And a two-eigenvector decomposition of the per-bin covariance
is **not** a valid way to split signal from noise here - the smaller
eigenvector is a direction, not a noise floor, so any weight can null it
and arm-1-alone scores 27 dB better than it should. The coherent/
incoherent split (`|R01|^2/R11` against the remainder) and the direct
carrier measurement agree with each other; that route was tried, gave
answers 25 to 35 dB out, and is recorded here so it is not tried again.

The capture files themselves are not in the repository - they are 46 MB a
minute. Keep them alongside this page for as long as the numbers here
matter.
