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
settled for now. **Findings 20 and 22 have been acted on**: the wideband
Sum weight now carries the branch noise ratio, and the CW passband error
under Finding 8, the missing attenuator fields and the linear Averaging
scale are fixed with it - see "What was changed, and what it scored".
Findings 17, 21 and 23 remain measurements only.

**Finding 11** - the MVDR solve returning a weight of exactly zero, the
second antenna muted, the menu showing -27 dB, on between half and all of
the frames of every RADE capture in this document bar one - has been
found and fixed. RADE V1 now beats the better antenna on five of the six
captures it can be scored on. **Finding 14** adds an antenna-selection
objective and measures it; it is a floor, not a ceiling, and Sum remains
the right default. Still open: the FSK/Digital occupancy test has no
false-alarm control, and its per-arm SNR estimate is the weakest of the
four (Finding 14).

**Finding 15** is found and fixed. The frequency loop had stable lock
points spaced one modem frame rate apart - 8.3 Hz - and acquisition could
not resolve which of them was right. The radio and a cold replay of the
same samples settled on two different ones on the same capture, and **five
of the eight RADE captures in this document were tracking one whole step
off the station**. It cost 0.7 to 1.9 dB of pilot SNR and very little in
decode, because the diversity weight is nearly indifferent to it. The fix
correlates the two halves of the pilot separately, which measures the
residual over a lag short enough to be alias-free, and steps the loop a
whole number of frame rates onto the right one. See "What was changed".

**Finding 16** is the first look below the HF bands: two mediumwave
captures, one a 693 kHz broadcast with inter-arm coherence 0.982, the
other band noise at 0.52. Null reaches the ceiling on the first and the
Best objective picks correctly on both.

**Findings 17, 18 and 20** are the first look at *fast* propagation: two
wideband digital signals on 5 MHz with the frequency-selective fading of a
low-band path. Three results come out of them, none of which needed a
tool that did not already exist.

The **flat scalar channel model has a boundary and this is it.** On
`000747` the differential channel `h1/h0` has a coherence bandwidth of
188 Hz across a 3.8 kHz filter - twenty independent frequency cells - and
a weight per bin nulls 2.2 dB deeper than the single complex weight
`receiver.c` can apply. On `000332`, four minutes earlier and 140 kHz
away, the same measurement gives 0.4 dB. What separates them is whether
the path is one mode or several (Finding 17).

**A fast loop is better, and how much better depends on the objective.**
Swept through the shipping engine over the whole 0.2-30 s slider, Null
deepens as the average shortens - 2.22 dB on `000332`, monotonically, and
1.11 dB on `000747` - while Sum moves 0.33 and 0.57 dB. `000332` has the
operator dragging the slider on air, and the estimator's own coherence
reading rises from 0.85 to 0.89 as they do it. The shipping default is
2.0 s; **no change is being made**, and what would justify one is written
under "What is still open" (Finding 18).

**The wideband Sum weight assumes the two antennas have equal noise.** On
`000332` they are 6.3 dB apart, the quiet antenna is the better one by
3.5 dB, and Sum lands 0.55 dB *below* it where 1.56 dB was available.
Putting the noise ratio back recovers 2.13 dB and reproduces the ideal
weight to 0.02 dB. **This is now fixed** - the Sum weight measures the
branch noise ratio by minimum statistics and carries it, which is worth
+0.8 to +2.5 dB on the two captures whose arms are far apart and costs
1.8 dB on the FT8 one for a reason Finding 23 explains. See "What was
changed" (Findings 20 and 22).

**Findings 28 and 29** are the first captures that record the step
attenuators, and the sweep that Finding 27 asked for.

`142333` sweeps both attenuators over 0 to 4 dB with the settings in the
file, and **both arms track them one for one to within 0.03 dB** - so
nothing is compressing the step, the receiver's own noise is more than
21 dB below the band's on 15 m at midday, and the first few decibels of
attenuation on a hot arm really are free. The available two-branch gain
holds at +1.0 to +2.2 dB at every setting, on both arms this time.
`142026` is a **DRM mode B** broadcast - received in `AM` mode, which is
how DRM is fed to a decoder, and identified from its 9.7 kHz occupied
width, its absent carrier and a cyclic-prefix autocorrelation peaking at
21.30 ms. Its *noise* is 0.74 correlated between the antennas, which is
the wanted-signal-plus-common-mode-noise case this document has wanted
since Finding 5, and it carries the largest per-bin advantage in the set:
a scalar weight nulls 8.1 dB where a weight per 94 Hz bin nulls 16.5
(Finding 28). **Finding 30** asks what window and tracking method suit it
and finds that only the objective matters.

**Finding 31** is 50 baud FSK on 20 m that stops, idles on its mark tone,
fades out, and lets a second signal show through. The two are told apart
by their inter-arm channels - **+4.4 dB at -161 deg against -0.6 dB at
+160 deg**, both measured at coherence above 0.98 - which is a conclusion
a single receiver cannot reach. To *hear* it, every window is within
0.6 dB on SNR and **FSK/Digital wins on level**, landing the same SNR with
5.4 dB less level rise because it is the one reference that measures the
branch noise. To *null* it, the window goes on the tones and the averaging
short - not because short deepens the null, which moves half a decibel
across the whole slider, but because it keeps the loop out of hold, 8 %
against 78 %. The null reaches 11 dB of a reachable 13.7, and the gap to
the 20.8 dB the coherence allows is **the one-block lag**: this path moves
enough in 171 ms to cost seven decibels. Nulling the FSK leaves the other
source 5.9 dB *up*, which is what a two-branch array is for.

**Finding 30** asks what window and tracking method suit DRM, and finds
that the answer is neither: window width from five bins to the whole
passband, both weightings and averaging from 0.2 to 10 s all land within
0.04 dB of each other, because with the arms 0.4 dB apart and their noise
0.76 correlated the whole prize is +0.56 dB. **The objective is what
matters.** Null costs 5.4 to 6.4 dB and pushes a fifth of all subcarriers
below 30 dB against 1.4 % on one antenna alone - the capture has the
operator running it for half the minute - and FSK/Digital is the wrong
reference for a band-filling OFDM signal, 0.71 dB below the better antenna
because occupancy can find no unoccupied bins to measure noise in.

**Finding 29 chooses the threshold and the weighting together**, which
Finding 27 could not. Two results: the gate is not free - every increment
of threshold costs mean SNR, half a decibel across the range - and **flat
weighting at 0.20 dominates the shipping coherence at 0.30**, with
slightly fewer false alarms and 0.18 dB more signal, ahead or level on
seven captures of seven. That change is justified and **is not being made
here**; what would settle it is the same sweep scored against a decoder.

**Findings 24 to 27** answer four more operator questions and overturn one
of this document's own conclusions.

**A hot antenna costs headroom and nothing else.** Seven to fourteen
decibels of extra output on ADC1 buys an SNR that runs from 5.1 dB *worse*
to 2.2 dB better, so the level says nothing about which antenna to use.
Attenuating it is nearly free - 12 dB cost arm 1 a quarter of a decibel
and cost the array nothing - and since Finding 22 put the noise ratio into
the Sum weight, dynamic range is now the *only* reason to equalise the
chains. How far one can go beyond 12 dB is **not** established: the fit
needs the attenuator settings the capture did not record, and swings from
-30 to -14 dB of margin across plausible step sizes (Finding 24).

**The Min coherence slider was one number compared against four different
quantities** - and against nothing at all in RADE V1, where it was inert.
At 30 % it demands +0.8 dB per arm under Window and -3.7 dB under RADE.
It is now stored per reference. The sweep behind it also found that the
**Carrier reference's gate does not work at any setting**: on a signal it
clears 0.30 on 34.7 % of blocks and on pure noise on 36.0 %, because five
bins put the estimator's own bias where the threshold is (Finding 26).

**Coherence weighting is a gate bias, not a better estimate**, which
reverses Finding 6. It does not improve the estimate on any capture, and
at a *matched false-alarm rate* it is 1.4 to 5.6 points worse than flat on
the gate too. It looked better only because it was compared at a fixed
threshold while inflating the statistic on signal and on noise alike
(Finding 27). Nothing has been removed; the finding is recorded.

**Findings 21, 22 and 23** answer an operator's question and turn up a
larger defect than the one they were asked about. Three captures on
2 September - 20 m voice and CW near the MUF, and 30 m FT8 - all with
**ADC1 running 9.8 to 13.2 dB hotter than ADC0**.

**A hot second antenna costs 3.6 dB and it is not the attenuator's
fault.** On `002534` the loop made the audio 14.8 dB louder with a noise
floor 18.3 dB higher and an SNR 3.6 dB *below* ADC0 alone, where +1.4 dB
was there to be had. The cause is Finding 20 again: Window and Carrier
give Sum the channel ratio with no noise term, so they assume the two
branches are equally noisy, and here they are 12 dB apart. `002710` has
the operator stepping the ADC1 attenuator twice while recording, which is
the experiment that settles it - the *available* gain is unchanged at
+1.6 to +1.9 dB at all three settings, while what the loop *achieved* goes
-3.4, -3.5, **+1.8 dB**. Equalising the chains does not improve the
estimate; it makes the estimator's assumption true. And on all three
captures FSK/Digital, the one reference that measures branch noise, beats
Window's Sum by 0.7 to 4.2 dB with an output 10 to 14 dB quieter
(Finding 22).

**The optimum averaging time is not set by the fading rate.** `002534`
fades in half a second and wants a 30 s average on Sum; `003309` fades
eight times slower and wants 0.2 s. What sets it is whichever quantity in
the solve is hardest to measure - here, an occupancy split hunting for
noise bins inside a voice passband. Null still wants the shortest average
that passes the gate, as Finding 18 found (Finding 21).

**On a band full of stations the displayed coherence is not any signal's
coherence.** On 30 m FT8 individual bins reach 0.946 while the passband
aggregate is 0.413, because two dozen stations spread `arg(h1/h0)` around
the whole circle. 0.413 still passes the 0.30 gate, and the weight that
results serves none of them: a per-bin weight nulls **5.4 dB** deeper than
the scalar, the largest such gap in this document (Finding 23).

Setting those up found a documentation error. **Finding 8's CW passband is
wrong** - the engine's window in CW includes the sidetone, so it is
-300..+300 Hz and not the +250..+850 the finding names, and both of that
finding's scored tables were computed in empty band noise. Corrected in
place under Finding 8; the conclusion survives and three of the premises
do not.

**Finding 19** closes an open item. `115357` catches the operator walking
the dial down 18 Hz in nineteen one-hertz steps: `DIV_RETUNE_HZ` = 20
holds the estimate through all of it where the old exact comparison would
have reset nineteen times, and the path's own fading moved `h1/h0` three
times further over the same nine seconds than the dial did.

The USB pilot bank, previously the most valuable missing measurement, is
now confirmed on air - see Finding 12, and confirmed again on a second
band in Finding 15's capture set.

Read in order, the findings divide into two groups. The **Window**
reference gains 1.6 to 1.8 dB over the better antenna on every voice
capture, on two bands and both sidebands. Everything else - RADE V1 on
four captures, FSK/Digital on CW - lands *below* the better antenna, and
in each case for a reason that has been isolated and measured. RADE V1 has
since been repaired and now matches or beats the better antenna on all
three captures it was scored against; FSK/Digital on CW has not.

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
10.5 s, hang 5.2 s, objective Sum; the captures added from August 30
onwards are not - averaging runs 1.9, 4.0, 5.6 and 4.8 s, `111852` has the
operator changing both the reference and the objective while it records,
and in `000332` and `000747` the operator moves the Averaging slider
itself, from 10.4 s down to 0.2 s in the first and around 0.2 to 3.4 s in
the second. Those two are the subject of Finding 18.
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
| `213018`, `213128` | 7.047 | DIGL | FSK/Digital | not analysed |
| `233133` | 7.047 | DIGL | RADE V1 | 0-36 s and 48-60 s |
| `233241` | 7.047 | DIGL | RADE V1 | throughout |
| `231724` | 3.588 | DIGL | FSK/Digital | 0-6 s and 30-60 s |
| `232052` | 3.588 | DIGL | RADE V1 | 0-5.8 s only, then dead air |
| `231532` | 3.588 | DIGL | FSK/Digital | **none** |
| `232750` | 3.588 | DIGL | RADE V1 | **none** |
| `233423` | 14.240 | DIGU | RADE V1 | **none** - band noise, occasional weak SSB |
| `233615` | 1.985 | LSB | RADE V1 | **none** - strong local interferer on ADC0 |
| `235853` | 3.663 | LSB | Window, coherence | analog voice, 33 s |
| `000012` | 3.663 | LSB | Window, coherence | analog voice, 60 s, 5 kHz filter |
| `235837` | 3.663 | LSB | Window, coherence | analog voice, **3.2 s - too short to use** |
| `000209` | 14.262 | USB | Window, coherence | analog voice, 43 s, nfft 8192 |
| `000328` | 14.262 | USB | Window, coherence | analog voice, 60 s, 5 filter changes |
| `001054` | 14.0522 | CWL | FSK/Digital | CW, 60 s, nfft 16384 |
| `001157` | 14.0522 | CWL | FSK/Digital | CW, 60 s, **operator tuning: 23 context changes** |
| `110923` | 5.3685 | USB | RADE V1 | **first bank-1 capture**, locked 65 % |
| `111051` | 5.3685 | USB | RADE V1 | two acquisitions, arm 1 the *better* antenna |
| `111328` | 5.3685 | USB | RADE V1 | **none** - band noise, 192 kHz |
| `111734` | 5.3715 | USB | RADE V1 | locked 70 % |
| `202743` | 7.09203 | DIGL | RADE V1 | marginal - quality 0.15, averaging **1.9 s** |
| `232842` | 1.987 | DIGU | RADE V1 | **bank 1 on a second band**, locked 94 %, averaging 5.6 s |
| `111852` | 0.6929 | SAM | Window, then Carrier, then FSK/Digital | **mediumwave** - 693 kHz broadcast, objective changed mid-capture |
| `112151` | 0.7244 | SAM | FSK/Digital | **mediumwave** band noise, partly coherent |
| `115357` | 7.19702 | DIGL | RADE V1 | locked 93 %, averaging 4.0 s, **operator walks the dial down 18 Hz** |
| `000332` | 5.42810 | USB | Window, coherence | **wideband digital**, continuous, **operator sweeps Averaging 10.4 -> 0.2 s on air** |
| `000747` | 5.28750 | USB | FSK/Digital, Window, then Carrier | **wideband digital**, 18 dB over the floor, fast selective fading, averaging 0.2-3.4 s |
| `002534` | 14.19500 | USB | Window, coherence | **20 m analog voice**, several operators, averaging **0.2 s**, ADC1 12.3 dB hot |
| `002710` | 14.01194 | CWL | Window, coherence | **20 m CW**, nfft 65536, **operator steps the ADC1 attenuator twice** |
| `003309` | 10.13611 | USB | FSK/Digital, then Window | **30 m FT8**, nfft 16384, many stations, averaging 0.2-2.7 s |
| `142026` | 11.65999 | AM | Window, **flat** | **DRM mode B, 10 kHz** - received in `AM` with a +/-6 kHz filter. ADC1 at **23 dB of attenuation**; operator cycles objective, reference and weighting |
| `142333` | 21.04004 | CWL | Window, coherence | **15 m CW**, nfft 65536, **both attenuators swept 0-4 dB** |
| `154822` | 14.11781 | USB | Window, coherence | **50 baud FSK, 205 Hz shift**, stops and idles on mark, then a **second source** appears out of band. No operator changes |

`202743` begins on 7.177 MHz and retunes to 7.09203 MHz at block 9. The
recorder did **not** set the context-changed bit for it: `rec_flags` is
zero on all 351 blocks. That is a devtool defect, not a radio one, but it
means the flag cannot be used to find retunes in this file - the
`frequency` field has to be read directly.

`111852` and `112151` are the first captures below 1.8 MHz, the first in
`SAM`, and the first where the *second* antenna is the loud one by a wide
margin: ADC1 runs 14.5 to 15.2 dB above ADC0 across the whole passband on
both. See Finding 16.

`000332` and `000747` are the first captures of **wideband digital signals
on a fast low-band path** - continuous, band-filling, and with the
frequency-selective fading that makes a scalar weight an approximation
rather than a model. They are also the first where the operator moved the
Averaging control while recording. See Findings 17, 18 and 20.

`115357` is a second 40 m RADE capture at a different dial frequency from
the 7.047 MHz set, recorded to catch a deliberate slow QSY. See
Finding 19.

`154822` is the first capture with **two identified sources in it**, told
apart by their inter-arm signatures rather than by their frequencies, and
the first test of the FSK/Digital reference on actual FSK. See Finding 31.

`142026` and `142333` are the first captures in **format version 2**, so
they are the first that record what the step attenuators were set to.
`142333` sweeps both of them and `142026` was taken with ADC1 already 23 dB
down; between them they answer the question Finding 24 had to leave open.
`142026` is also the first capture in the **AM** mode group, the first with
a wanted signal sitting on noise the two antennas largely share, and the
first carrying a **digital broadcast**: it is Digital Radio Mondiale, not
conventional AM, received in `AM` mode with the filter opened out. The
mode column says what the radio was set to; the signal is OFDM. See
Findings 28 and 30.

`002534`, `002710` and `003309` were taken in one session with **ADC1
running 9.8 to 13.2 dB hotter than ADC0** - the first captures where the
imbalance between the two receive chains is the subject rather than a
detail, and the first where the operator moved a step attenuator while
recording. The two 20 m captures were taken with the band close to its
maximum usable frequency and carry the fastest fading in the set. See
Findings 21, 22 and 23. `002710` is also the capture that found the CW
window error corrected under Finding 8.

One field in the capture is not what it looks like. `ctun_frequency`
reads 7047000 on **every capture after `213155`** - it is a stale copy
left over from the first session, CTUN not being in use, and it has
nothing to do with what the radio was tuned to. The dial frequency is the
`frequency` field, which is what the table above lists. It matters only
because `div_context_changed()` compares all three frequency fields, and a
stale one that never moves simply never fires.

The five "none" captures are the most valuable ones in the set. A capture
of nothing is what says whether a detector threshold is safe, and it costs
nothing but a minute of a quiet band.

The four 60 m captures were taken with **no note recorded**, which the
devtools README asks for and which nothing enforces. What is known of
them comes from the operator afterwards: ADC0 is the main antenna,
sometimes tuned and sometimes not, ADC1 an untuned doublet. That
asymmetry is the subject of Finding 13, and the missing note is the
reason it had to be established by measurement rather than read off the
file. Every capture from `115357` onwards has no note either. Findings 20 and 22 turn on which antenna was on which ADC
and on what the attenuators were set to, and neither is recorded, so the
omission has now cost something in three findings. The attenuator is the
worse gap of the two: `struct divcap_block` mirrors no `att0`/`att1`
field, so on `002710` the two settings the operator chose have to be
inferred from arm 1's own noise floor.

`231724` and `232052` are a matched pair: same band, same path, five
minutes apart, one running each reference. That comparison did more work
than any single capture.

## Method, and four traps worth knowing about

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

### Trap 4: a level-ratio metric is invalid for a weight that moves with the level

Finding 6's yardstick for analog voice - passband power over the blocks
that contain speech against the same over the blocks that do not - is
sound for a weight that is the same in both sets of blocks, which is what
the shipping loop produces because it holds through the gaps.

It is not sound for a *hypothetical* weight recomputed every block. On
`002534` the equal-noise MRC weight scores +28.18 dB by that metric and
+18.32 dB when scored against a guard region - a 9.9 dB disagreement,
because the weight itself is large on speech blocks and derived from noise
on quiet ones, so it modulates the very ratio being measured. The arm-0,
arm-1 and as-it-ran rows agree between the two metrics to a tenth of a
decibel; only the recomputed weights diverge.

Use a level ratio only for weights that do not know which set of blocks
they are in. For anything else, score against noise measured separately -
and see "Two guard regions, not one" under Finding 18 for how to do that
without scoring a weight against the noise it just cancelled.

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
cross-spectrum the FSK/Digital reference already uses in `bin_xy`,
`bin_xx`.

## Finding 3: what the combiners deliver

Decode-scored, mean `rade_snrdB_3k_est()` in dB. "available" is the ideal
two-branch combiner built from the measured channel and the dead-air
noise.

| capture | arm 0 | arm 1 | RADE V1 | FSK/Digital | ideal |
|---|---|---|---|---|---|
| `213155` 40 m | 7.2 | **9.6** | 6.2 | 8.7 | 11.1 |
| `233133` 40 m | **9.4** | 1.5 | 8.9 | 8.3 | 10.2 |
| `233241` 40 m | **10.3** | 4.8 | 8.5 | 9.8 | 9.9 |
| `231724` 80 m | **4.4** | 1.7 | 4.0 | 5.1 | 5.1 |

Against the better arm:

| capture | RADE V1 | FSK/Digital | ideal |
|---|---|---|---|
| `213155` | **-3.4** | -0.9 | +1.5 |
| `233133` | **-0.5** | -1.1 | +0.8 |
| `233241` | **-1.8** | -0.4 | -0.3 |
| `231724` | **-0.3** | +0.7 | +0.8 |

**RADE V1 is below the better antenna on all four captures.** FSK/Digital
is better than RADE on three of four, and reaches the ideal on `231724`.
Neither reliably beats simply selecting the better antenna.

Two honest caveats. On `233133` FSK/Digital is 0.6 dB *worse* than RADE,
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
| replayed FSK/Digital | 21.34 (+1.23) | 20.63 (+0.08) |
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

Window and FSK/Digital both build their channel *and* their covariance
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
| FSK/Digital | 41 % | 37 % |

Flat weighting spends most of the voice *holding*, because the coherence
it reports is diluted by the noise-only bins in the window and falls below
`div_auto_coherence_min`. Weighting each bin by its own coherence lifts
the figure past the gate. That is the mechanism `diversity.md` describes,
now measured. On SNR the two are 0.58 dB apart on the narrow passband and
identical on the wide one - so on these two captures coherence weighting
never hurt and sometimes helped.

### FSK/Digital is the wrong tool for voice

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

Two captures on 14.0522 MHz CWL, FSK/Digital reference, 600 Hz filter
(-850 to -250), sidetone 550 Hz - which is also the first exercise of the
CW branch of `div_frame_off()` on recorded data.

**The tapped-frame window this finding was scored in is wrong.** It is
given below as +250 to +850, the plain inversion of the filter. The engine
does not use the plain inversion in CW: `div_shift_to_bin()` returns
`-(s + div_frame_off(ctx))` and `div_frame_off()` adds the sidetone in
CWL, so the window is **-300 to +300 Hz**. Everything under "Correction:
the fallback is not the path responsible" was measured in a band with no
signal in it. See "Second correction" at the end of this finding, which
supersedes both tables.

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

**FSK/Digital is the wrong reference for a narrow CW passband.** That much
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
FSK/Digital, and *no signal at all* - the mode produces a weight on 30 %
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
guess.

### Second correction: the window itself was wrong

`002710` is a third CW capture (Finding 21) and setting it up meant
working out where the engine's window actually lands in CW. It is not
where this finding says.

`div_bin_range()` maps the operator's filter through
`div_shift_to_bin(ctx, s) = -(s + div_frame_off(ctx))`, and
`div_frame_off()` adds the sidetone in CWL. For a -850..-250 filter at a
550 Hz sidetone that gives **-300..+300 Hz**, not +250..+850. Measured on
the spectrum averaged over each capture, with the band median over
+/-4 kHz as the reference:

| | `001054` | `001157` |
|---|---|---|
| energy in -300..+300 (the engine's window) | **+3.0 dB over the band median** | **+14.8 dB** |
| energy in +250..+850 (what this finding used) | +0.3 dB | -0.1 dB |

The region the tables above were scored in is empty band noise. Rerunning
this finding's own metric - the ten strongest bins of the window against
the weakest 120, Blackman-Harris, averaged over the capture - in the
engine's real window:

| | `001054` | `001157` |
|---|---|---|
| arm 0 | **+12.91 dB** | **+24.86 dB** |
| arm 1 | +12.27 (-0.64) | +24.17 (-0.69) |
| as it ran on air | 10.39 (**-2.52**) | 22.84 (**-2.02**) |
| replayed from cold | 10.61 (-2.30) | 24.47 (-0.39) |

Three of this finding's premises do not survive. The signals were **not**
weak - the window stands 12.9 and 24.9 dB above its own floor, not the
"3.6 to 4.5 dB above the window median" reported above. The two antennas
are **not** 1.7 to 2.0 dB apart - they are 0.64 and 0.69 dB apart, so
there was real diversity gain on the table rather than almost none. And
"no combination gets near arm 0 alone ... because arm 1 is 1.7 to 2.0 dB
worse" is not the mechanism, because arm 1 is not worse.

What survives is the conclusion. The combiner still lands 0.4 to 2.5 dB
below the better antenna on both captures, with two nearly equal arms and
something to gain, which is a worse result than the original tables
suggested rather than a better one.

The old figures are left in place above rather than deleted: they are
reproducible to the second decimal from the wrong window, which is how the
error was identified, and a reader who finds the same numbers should know
why.

Until the open question above is settled, the advice stands: use **Window**
on a narrow CW passband, not FSK/Digital - though note that Finding 22
measures the opposite preference on a pair of antennas far apart in noise,
and both CW captures here have nearly equal arms.

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
| FSK/Digital | `sig_xx`, `sig_xy` | `r00`, `r11` (occupancy split) | none |
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

| capture | better arm | Window | Carrier | RADE V1 | FSK/Digital |
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
Carrier pick ADC1 there, RADE V1 and FSK/Digital pick ADC0, and there is
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
FSK/Digital is the weakest by a distance, which is consistent with the
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

### The obvious remedy does not work

The first thing tried was to correlate at `lock_f` and `lock_f +/- 8.333`
and keep the strongest. It fails, and the reason is worth writing down
because the arithmetic looks like it should go the other way.

One pilot symbol is 20 ms, so its correlation resolves frequency to about
50 Hz. That is *coarser* than the 8.33 Hz step, not finer: 8.33 Hz is
well inside the main lobe, where `sinc(8.333 * 0.02)` = **-0.40 dB**. A
neighbour one alias step away is only four tenths of a decibel down even
when it is wrong, and measured on air the difference is smaller still and
does not have a consistent sign:

| capture | `lock_f` | neighbour above | neighbour below |
|---|---|---|---|
| `232842`, at +7.78 | correct | -0.54 dB | -0.27 dB |
| `232842`, at +16.12 | one step high | -1.39 dB | **+0.52 dB** |
| `110923` | one step high | -1.19 dB | +0.37 dB |
| `111734` | one step high | -1.27 dB | +0.40 dB |
| `213155` | one step low | +0.27 dB | -1.20 dB |
| `233133` | one step low | +0.32 dB | -1.23 dB |

The right neighbour does read higher, but by 0.3 to 0.5 dB - and on
`232842` at the *correct* frequency a neighbour still reads only 0.27 dB
down. A rule that keeps the strongest of three would step the loop on
almost every capture, right or wrong.

### What does work: half a pilot

The alias exists because the discriminator's lag is one whole modem
frame. Shorten the lag and it goes away. Correlating the first 80 samples
of the pilot and the last 80 separately and taking `arg(c2 * conj(c1))`
measures the residual over 10 ms, which is unambiguous over
**+/-Fs/M = +/-50 Hz** - the entire acquisition range.

It is a much noisier discriminator than the frame-to-frame one: 80
samples is half the correlation gain over a fifteenth of the lever arm.
That is fine, because it is not being asked to *track* anything. It is
averaged coherently for a few seconds and used once, to decide which
equilibrium the frame-rate loop should be sitting in - and the answer
only has to be good to half a step.

Measured against the best equilibrium found by forcing `lock_f`, it is
good to 1.3 Hz:

| capture | best equilibrium | half-pilot estimate | error |
|---|---|---|---|
| `232842` | +7.78 Hz | +6.50 Hz | -1.28 |
| `110923` | -2.14 Hz | -1.71 Hz | +0.43 |
| `111734` | -2.15 Hz | -2.02 Hz | +0.13 |
| `213155` | -9.24 Hz | -7.96 Hz | +1.28 |

and it is **independent of where the loop is sitting**, which is the
whole point. Started at nine different offsets from -20 to +30 Hz on
`232842`, it returned +6.30 to +6.70 Hz - the same answer from basins
25 Hz apart.

Forcing `lock_f` on the captures whose equilibria were swept above shows
what the old loop was giving up. The best equilibrium against the one
acquisition actually chose:

| capture | acquisition chose | best is | quality | pilot SNR |
|---|---|---|---|---|
| `110923` | +6.19 Hz, q 0.824, +6.69 dB | **-2.14 Hz** | 0.880 | **+8.65 dB** |
| `111734` | +6.18 Hz, q 0.799, +5.99 dB | **-2.15 Hz** | 0.850 | **+7.52 dB** |
| `213155` | -17.36 Hz, q 0.463, -0.65 dB | **-9.24 Hz** | 0.692 | **+3.52 dB** |
| `232842` | +7.78 Hz (right) | +7.78 Hz | 0.507 | +0.12 dB |

So `232842`, the capture that started this, was the *lucky* one on
replay. Three of the others were a step out and paying 1.5 to 4.2 dB for
it. The fix and what it scored are under "What was changed".

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
| Sum / FSK/Digital | **+36.38 dB** | **+0.15** | -0.20 dB, -33.4 deg |
| Best (all three) | +36.22 dB | -0.01 | +20.00 dB (the rail) |
| best fixed weight | +36.40 dB | +0.17 | -2.75 dB, -48.0 deg |

FSK/Digital's answer looks wrong and is not. It applies a weight of
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
| loop, Null / FSK/Digital | **-14.37 dB** |
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
| loop, Null / Window, Carrier, FSK/Digital | -0.76 to -0.79 dB |
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

| | true arm 1 advantage | Window | Carrier | FSK/Digital |
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

## Finding 17: fast frequency-selective multipath, and the limit of one complex weight

Two captures on 5 MHz, both continuous wideband digital signals filling
the operator's filter, both with the fast multipath of a low-band
ionospheric path. They are the first captures in this document taken on
that kind of propagation, and between them they bracket it: `000332` is a
path a single complex weight describes and `000747` is one it does not.

Everything in this finding is measured directly from the `.divc` files -
2048-sample transforms, 93.75 Hz bins, 10.7 ms sub-blocks, the passband
taken inverted per the sideband rule - with no engine in the loop. The
guard region for noise is +3500 to +6000 Hz in the tapped frame, which is
clear of the passband on both and clear of the interferer `000332` carries
at +6.75 to +7.4 kHz.

| | `000332` | `000747` |
|---|---|---|
| arm 0 passband over the guard floor | 28.1 dB median (p10 21.4, p90 30.7) | 17.9 dB median (p10 13.4, p90 21.9) |
| passband | -2850..-150 (2.7 kHz filter) | -3950..-150 (3.8 kHz filter) |
| arm1 - arm0, passband | -2.78 dB | +3.17 dB |
| guard `N1/N0`, noise coherence | **-6.31 dB**, 0.084 | +2.90 dB, 0.083 |
| inter-arm coherence, passband | **0.901** | 0.501 |
| the same per bin, averaged over the minute | 0.896 | **0.363** |
| the same per 10.7 ms sub-block, over bins | 0.913 | **0.657** (p90 0.896) |

The last two rows are the whole finding in miniature. On `000747` the two
antennas agree 0.66 at any given instant and 0.36 once the minute is
averaged. **The decoherence is temporal, not spatial** - and a loop with a
long averaging time manufactures it.

### The differential channel across the passband

`h1/h0` per 171 ms block, per 93.75 Hz bin, power-weighted across blocks:

| | `000332` | `000747` |
|---|---|---|
| `\|h1/h0\|` ripple across the band | 1.04 dB sd (p90 2.00) | **7.04 dB sd** (p90 8.93) |
| phase residual after a straight line | 6.8 deg sd (p90 11.2) | **82.8 deg sd** (p90 131) |
| differential delay from that line | -3.7 us median, -20..+17 p10..p90 | -60 us median, -445..+297 p10..p90 |
| `\|rho(h)\|` across 94 / 188 / 375 / 750 Hz | 0.976 / 0.957 / 0.935 / 0.898 | **0.724 / 0.411 / 0.216 / 0.134** |
| **coherence bandwidth**, `\|rho\|` = 0.5 | **> 1.8 kHz** (the whole passband) | **188 Hz** |
| `\|rho(h)\|` over 0.17 / 0.51 / 1.0 / 2.0 s | 0.983 / 0.976 / 0.963 / 0.939 | 0.930 / 0.755 / **0.462** / 0.282 |
| **coherence time**, `\|rho\|` = 0.5 | > 10 s | **~1.0 s** |

Read the delay row with care. On `000332` a straight line through the
phase leaves 6.8 degrees, so the slope means something and the answer is
a differential delay of a few tens of microseconds. On `000747` it leaves
**83 degrees**, which is as large as the thing being fitted: there is no
single differential delay to quote, and the honest description of that
channel is its coherence bandwidth. 188 Hz across a 3.8 kHz filter is
**twenty independent frequency cells in the operator's passband**, and a
scalar weight is one cell.

### What that costs, measured

Weight fitted on block n and applied to block n+1 - a genuine hold-out,
and the same one-block lag the radio has. Output passband power against
arm 0 alone:

| | `000332` | `000747` |
|---|---|---|
| one complex weight for the whole band | -9.57 dB | -3.06 dB |
| an independent weight per 93.75 Hz bin | -9.95 dB | **-5.23 dB** |
| difference | 0.37 dB | **2.17 dB** |

On the flat path a wideband combiner is worth four tenths of a decibel and
is not worth having. On the frequency-selective one it is worth **2.2 dB**,
and that is exactly the group-delay distortion the present architecture
cannot touch: `src/receiver.c` forms

```c
i_sample = i0 + (div_cos * i1 - div_sin * q1);
```

one complex scalar across the whole passband. Nothing in the estimator
is at fault - the scalar is the correct MVDR answer *given* that a scalar
is what will be applied.

Two things this does **not** say. It does not say the array can equalise
either arm: correcting the group delay *within* one antenna's signal is an
equaliser's job and two antennas cannot do it. What a per-bin combiner
does is choose, at each frequency, the spatial combination that is best at
that frequency, which flattens the *composite* channel because the two
arms fade in different places. And it does not say 2.2 dB is available
cheaply - twenty weights need twenty times the estimation, on a signal
that decorrelates in a second.

### The scalar model, restated

The RADE captures put `h1/h0` inside +/-0.63 dB and
+/-12 degrees across the passband, and this document has been carrying
"a single complex weight is the correct model" on the strength of it.
That holds on `000332`, on `115357`, and on every capture measured before
these. It does not hold on `000747`, and the difference is not the band or
the antennas - it is whether the path is one mode or several.

## Finding 18: on a fast path the averaging time is the binding constraint, and the answer depends on the objective

`000332` contains the experiment written into it: at t = 4.1 to 6.3 s the
operator drags the **Averaging** slider from 10.4 s to 0.2 s and leaves it
there for the rest of the minute. This finding is that sweep, done
properly - `run_ref` driving the whole shipping engine over both captures
at thirteen averaging times, from 0.2 s to the 30 s top of the slider.

The weight series `run_ref` writes is applied to the capture one block
late and scored two ways. **Null** is output passband power against arm 0
alone: no fitting, nothing to overfit. **Sum** is an SNR, and needs the
care Trap 1 demands - see "Two guard regions, not one" below.

### Null: faster is better, on both captures, over the whole slider

| tau | `000332` null | ideal | `000747` null | ideal | `000747` holding |
|---|---|---|---|---|---|
| 0.2 s | **-9.16 dB** | -9.50 | **-1.70 dB** | -2.92 | 23 % |
| 0.5 s | -9.01 | -9.39 | -1.37 | -2.64 | 23 % |
| 1.2 s | -8.74 | -9.13 | -1.15 | -2.17 | 23 % |
| 2.5 s | -8.35 | -8.76 | -1.06 | -1.80 | 26 % |
| 5.1 s | -7.83 | -8.21 | -0.92 | -1.52 | 39 % |
| 10.4 s | -7.36 | -7.71 | -0.98 | -1.36 | 45 % |
| 30 s | -6.94 | -7.35 | -0.59 | -1.27 | 7 % |

"ideal" is the best causal two-branch weight from the same EWMA, applied
the same block late - the ceiling the estimator is being measured against,
not a different objective.

**2.22 dB on `000332` and 1.11 dB on `000747`, bought by nothing but
shortening the average.** The curve has not flattened at 0.2 s on either,
and the shipping loop tracks its own ceiling to within 0.3 to 0.4 dB on
`000332` at every point - so on that capture the loop is not the
limitation, the averaging time is.

`000332` is monotone at every one of the thirteen points swept. `000747`
is not quite: it reverses by 0.09 dB between 5.1 and 7.0 s and by 0.11 dB
between 10.4 and 17 s, in the region where the holding fraction is
swinging between 26 and 45 %. The trend across the slider is 1.11 dB and
the reversals are a tenth of that, so they are scatter rather than a
turning point - but the column is quoted here as measured rather than
smoothed.

`000747` sits about 1.2 dB below its ceiling at every setting, and the
whole of that gap is the coherence gate - which turns out to be doing the
right thing, not the wrong one. At tau 0.2 s it holds 23 % of blocks,
whose reported coherence averages 0.128 against 0.618 for the ones it
passes. Scored on the two sets separately, the ideal weight reaches
**-3.93 dB on the blocks the gate passed and -0.23 dB on the blocks it
held**. The gate is declining to act where there was nothing to collect,
and the "shortfall" is the ceiling being computed over blocks a sane loop
should sit out.

### Sum: only where the channel actually moves

| tau | `000332` Sum | ideal | `000747` Sum | ideal |
|---|---|---|---|---|
| arm 0 alone | 27.43 dB | | 18.10 dB | |
| arm 1 alone | **30.95 dB** | | **18.37 dB** | |
| 0.2 s | 30.40 (**-0.55**) | 32.48 (+1.53) | **20.84 (+2.47)** | 21.22 (+2.85) |
| 1.2 s | 30.38 | 32.58 | 20.42 | 20.85 |
| 5.1 s | 30.29 | 32.68 | 20.04 | 20.25 |
| 30 s | 30.07 (-0.88) | 32.58 (+1.63) | 20.27 (+1.90) | 20.06 (+1.69) |

Bracketed figures are against the better arm.

Shortening the average is worth **0.57 dB on `000747` and 0.33 dB on
`000332`** - a quarter of what it is worth to Null. That difference is
not noise, it is the two objectives wanting different things. Null has to
put a null exactly on the interferer and a null is exquisitely sensitive
to a stale estimate; Sum only has to get the co-phasing direction roughly
right, and on a path whose channel is still 0.94 correlated after two
seconds a longer average buys variance reduction that nearly pays for the
lag. The ideal column on `000332` says so directly: its best point is
tau 5.1 s, not 0.2 s.

So "a fast loop is better" is true, and it is a statement about **Null**
much more than about Sum.

`000332`'s Sum sits 0.55 dB *below* the better antenna while 1.56 dB was
available. That is not the averaging time - see Finding 20.

### The on-air sweep says the same thing

The operator's own slider drag, scored from the recorded `div_cos`,
`div_sin` over the blocks each setting was in force. Segment-local, so
each row is compared with the two arms over the same blocks:

| `000332`, as it ran | tau | output | arm 0 | arm 1 | reported coherence |
|---|---|---|---|---|---|
| Sum, blocks 0-23 | 10.4 s | +23.63 dB | +21.34 | +25.21 | 0.849 |
| Sum, blocks 24-37 | 10.4 -> 0.2 | +22.74 | +19.55 | +27.11 | 0.743 |
| Sum, blocks 38-130 | 0.2 s | +31.04 | +27.81 | +32.00 | **0.892** |
| Null, blocks 131-229 | 0.2-1.3 s | null **-7.81 dB** | - | - | 0.883 |
| Sum, blocks 230-350 | 0.2 s | +30.21 | +26.48 | +31.24 | 0.890 |

The absolute levels move because the signal did, which is why the arm
columns are there. What is stable across the change is the **reported
coherence**: 0.85 at 10.4 s, 0.74 through the drag, 0.89 for the rest of
the minute at 0.2 s. The estimator's own quality figure improves when the
average is shortened, on a signal that has not changed, which is the
on-air face of the 0.36-against-0.66 pair at the top of Finding 17.

The 0.2 s Null segment reaches -7.81 dB on air against **-9.05 dB** for a
cold replay at the same setting over the same blocks. The replay starts
with empty accumulators and the radio did not, and the operator was also
nudging the slider between 0.2 and 1.3 s through that stretch; the same
on-air-against-replayed gap is in Finding 6, and it is unexplained there
too.

The cold replay at 10.4 s reproduces the recorded on-air weight over the
blocks the operator had at that setting to `|w|` 0.54 against 0.61 and
62 against 73 degrees - close, and not identical, for the same reason.

### Block length is not the knob

The Resolution control sets the transform size and so the block period,
and 170.7 ms at nfft 32768 is a sixth of `000747`'s coherence time. It is
tempting to blame that. It is not the limit:

| best weight per | `000332` | `000747` |
|---|---|---|
| 171 ms analysis block | -9.64 dB | -3.31 dB |
| 10.7 ms sub-block | -9.92 dB | -3.37 dB |

0.3 dB and 0.06 dB. Both rows are fitted in sample - which is why the
block row reads a little better here than the -9.57 / -3.06 hold-out in
Finding 17 - and the bias is far larger on the sub-block row, which is
fitted on a sixteenth of the data, so the true gap is smaller than the
table shows rather than larger. What limits the estimate is the variance
of the statistics, not how often they are refreshed. **Averaging is the
right control and Resolution is not.**

### Two guard regions, not one

The Sum column needs a noise measurement and neither capture has any dead
air - both signals are continuous - so Finding 6's voice-against-quiet
split cannot be used, and there is no decoder for either signal, so the
document's most trusted yardstick is unavailable too.

What is left is guard bins, and they have to be used carefully. An SNR
built from the *same* bins the solve fitted its covariance on scores the
weight against the noise realisation it just cancelled. Done that way,
`000332` scored **+4.9 dB over the better antenna** at tau 0.2 s - more
than a two-branch array can deliver, and Trap 1 wearing a different hat.

Every Sum figure above therefore uses a **split guard**: the loop and the
ideal weight see +1000 to +3000 Hz, and the score is taken on +3500 to
+6000 Hz, never the reverse. With the split in place `000332` reads
-0.55 dB and the ideal +1.56 dB, which are numbers a two-branch array can
actually produce.

## Finding 19: a slow QSY, and the 20 Hz retune tolerance measured at last

Finding 9 added `DIV_RETUNE_HZ` = 20 and this document has recorded ever
since that it "is a first number, not a measured one ... justified by what
it must not break rather than by how far the dial can move before `h1/h0`
really has changed", and that settling it wanted "a capture of a
deliberate slow QSY across a band with a steady signal in view".

`115357` is that capture. 7.197018 MHz `DIGL`, RADE V1, averaging 4.0 s,
locked 93 % of the minute from two acquisitions with a mean pilot SNR of
7.4 dB - and at t = 13.0 to 21.8 s the operator walks the dial **down 18
hertz in nineteen one-hertz steps**.

| | `115357` |
|---|---|
| steps | 19, over blocks 76-128 |
| total movement | 18 Hz in 8.8 s |
| resets, exact comparison | **19** |
| resets, `DIV_RETUNE_HZ` = 20 Hz | **0** |

### What the dial move can actually affect

Only the reset. `ctx->frequency`, `ctx->ctun_frequency` and `ctx->offset`
are read in exactly one place in `src/diversity_auto.c` - the three
comparisons in `div_context_changed()`. The analysis window comes from the
filter, the pilot bank from the mode, and `div_frame_off()` from the
offset and the sidetone. So an 18 Hz dial move changes nothing the
estimator computes, and the only question is whether throwing the estimate
away was right.

### Discarding the estimate would have been wrong, and the control says so

`h1/h0` measured independently from the raw blocks over the passband
(+560 to +2440 Hz in the tapped frame), first third of the walk against
last third, beside a settled stretch of the same length taken later in the
same capture:

| | change in `h1/h0` |
|---|---|
| across the 18 Hz walk (blocks 76-128) | **-2.10 dB, +7.9 deg** |
| control: settled, same length (blocks 180-232) | **+7.10 dB, +16.3 deg** |

**The path moved more by itself, in nine seconds, than the dial move moved
it.** An estimate that survives ordinary fading has no business being
discarded for eighteen hertz, and the exact comparison would have
discarded it nineteen times.

### One thing that is not what it looks like

Everything in this section is the radio's own recorded state, block by
block, rather than a replay. The applied weight does move violently in
that window - a mean
block-to-block step of 0.44 against 0.04 over the settled stretch - and
it would be easy to write that up as the retune damaging the loop. It is
not. The RADE lock dropped at **block 68**, eight blocks *before* the
first dial step, spent blocks 76 to 80 confirming, and re-locked at block
81; what follows is the weight slewing from `|w|` 1.09 to about 5 as a
fresh estimate builds. Locked runs 91 % across the walk against 100 %
settled, and holding 9 % against 0 %, for the same reason.

The two events overlap in time and are unrelated. This is the second time
in this document that a plausible mechanism read off a recorded series
turned out to be a coincidence - see the correction under Finding 8 - and
it is worth the same warning: check the timeline before believing the
story.

**The 20 Hz tolerance is now measured rather than argued.** What it does
not yet establish is where the tolerance stops being right; 18 Hz is
inside it by two hertz, and a walk of several hundred hertz would say
whether the number is generous or mean.

## Finding 20: the wideband Sum weight assumes the two antennas have equal noise

`000332` is the first capture where a wideband reference runs Sum on a
pair of antennas whose noise floors are far apart, and it costs 2.1 dB.

The Window and Carrier references end in one line:

```c
double den  = (div_auto_mode == DIV_AUTO_SUM) ? acc_xx : acc_yy;
double sign = (div_auto_mode == DIV_AUTO_SUM) ? 1.0 : -1.0;
div_apply_weight(sign * acc_xy_re / den, sign * acc_xy_im / den);
```

For Sum that is the window's own channel ratio and nothing else:
**maximum ratio combining under the assumption that the two branches carry
equal noise.** It is the right answer when they do, and there is no noise
measurement in that reference to tell it otherwise - Finding 14 says as
much, and adds a minimum-statistics floor tracker to publish the per-arm
figure, which the Sum path does not use.

On `000332` the arms are not equal. ADC1 is 2.78 dB down on signal and
**6.31 dB down on noise**, so it is the better antenna by 3.5 dB - the
same "quiet is not deaf" pattern Finding 13 found on 60 m, seen here by a
wideband reference rather than by the correlator. Which antenna that was
is not recorded: this capture has no note either.

Scored with the split guard of Finding 18, over the whole minute:

| | `000332` | `000747` |
|---|---|---|
| arm 0 alone | 27.43 dB | 18.10 dB |
| arm 1 alone | **30.95** | **18.37** |
| shipping Window / Sum, tau 0.2 s | 30.40 (**-0.55**) | 20.84 (+2.47) |
| the channel ratio, per block - what the solve computes | **30.40** | 21.39 |
| the same, scaled by the noise ratio `N0/N1` | **32.53 (+1.58)** | 21.30 |
| ideal causal MVDR | 32.51 (+1.56) | 21.16 |
| best single fixed weight, exhaustive search | 32.37 (+1.42), `\|w\|` +10.3 dB at +62 deg | 20.41 |

The "channel ratio, per block" row reproduces the shipping figure to the
second decimal on `000332`, which is what identifies the mechanism rather
than merely suggesting it. Putting the noise ratio back recovers **2.13 dB** and lands
within 0.02 dB of the ideal weight. The weight it wants is `|w|` = +10 dB
where the loop applies -3 dB: not a small correction, a factor of twenty
in power, and in the direction of the antenna the operator would assume
was the worse one.

`000747` is the control. There the arms are 2.9 dB apart in noise and the
channel ratio dominates, so the two forms agree to 0.09 dB and the
shipping loop is already within 0.4 dB of ideal - and 0.43 dB *ahead* of
the best single fixed weight, which is tracking earning its keep again on
the capture where the channel actually moves.

### Why this has not shown up before

It needs two things at once, and no earlier capture had both. The voice
captures ran Window / Sum but their arms are only 2.4 to 2.5 dB apart on
noise (Finding 4), where the correction is worth a few tenths. The
mediumwave pair is 13 to 15 dB apart, which is far worse - but there the
noise is 78 % correlated and arrives nine degrees from the signal, so
Finding 16 measures the *entire* array gain available at 0.17 dB over the
better antenna, and Window / Sum collected all of it by applying `w` =
+14.95 dB, which is the channel ratio and happened to be right. The 60 m
pair, where the difference is largest, was recorded on RADE V1.

`000332` is the first capture with a wideband reference, a Sum objective,
arms far apart on noise, and a real prize on the table.

Finding 14 already built the missing number. What it did not do is feed it
to the Sum weight, and this is the first measurement that says what that
is worth. **This has since been acted on**, though not by reading Finding
14's figure - the floor tracker behind it turned out to have a fault of
its own, described under "What was changed, and what it scored".

## Finding 21: 20 m near the MUF, and where the optimum averaging time really comes from

Three captures taken in one session on 2 September: analog voice and CW
on 20 m, with the band close to its maximum usable frequency, and FT8 on
30 m. They were recorded to answer a timing question, and they answer it -
but not the way Finding 18 would predict.

| | `002534` voice | `002710` CW | `003309` FT8 |
|---|---|---|---|
| band, mode | 14.195 MHz USB | 14.01194 MHz CWL | 10.136107 MHz USB |
| engine window | -2850..-150 Hz | **-400..+400 Hz** | -3450..-150 Hz |
| block | 170.7 ms (nfft 32768) | 341.3 ms (nfft 65536) | 85.3 ms (nfft 16384) |
| operator's averaging | **0.2 s** | 3.4 s | 0.2 -> 2.7 -> 1.9 -> 0.2 s |
| inter-arm coherence, passband | 0.783 | 0.636 | 0.413 |
| **coherence time** of `h1/h0` | **~0.5 s** | ~1.0 s | ~4.1 s |
| coherence bandwidth (`\|rho\|` = 0.5) | 188-375 Hz | - | 94-188 Hz |
| arm 0 SNR / arm 1 SNR | +19.02 / +13.93 dB | +12.39 / +9.58 dB | +20.81 / +20.90 dB |

`002534` has the fastest fading in this document: `|rho(h1/h0)|` falls to
0.649 in one 171 ms block and 0.523 in two. `000747`, the previous
fastest, took a whole second to reach 0.46. That is what 20 m near the MUF
does, and it is the regime the operator asked about.

The CW window is **-400..+400 Hz**, not the plain inversion of the filter:
`div_shift_to_bin()` subtracts `div_frame_off()`, which in CW is the
sidetone. See the correction under Finding 8, which this capture is the
reason for.

### The sweep, and the surprise

`run_ref` over the whole 0.2-30 s slider, both wideband references, both
objectives. SNR is guard-referred (split guard, per Finding 18), against
the better arm:

| tau | voice, Window Sum | voice, FSK/Digital Sum | FT8, Window Sum | FT8, FSK/Digital Sum |
|---|---|---|---|---|
| 0.2 s | -3.74 | -1.23 | **+0.75** | **+1.48** |
| 0.5 s | -3.77 | -2.23 | +0.63 | +1.40 |
| 1.2 s | -3.78 | -1.25 | +0.44 | +0.87 |
| 3.4 s | -3.50 | -0.04 | +0.17 | +0.60 |
| 10.4 s | **-3.45** | +0.68 | +0.18 | +0.74 |
| 30 s | -3.49 | **+0.79** | +0.15 | +0.49 |
| ideal causal | +1.37 | +1.37 | +2.04 | +2.04 |

Two things fall out, and only one of them was expected.

**On the fastest-fading capture in the set, the Sum objective wants a
*long* average, not a short one.** `002534` fades in half a second and its
best FSK/Digital Sum is at 30 s, two dB better than at 0.2 s. On the FT8
capture, whose channel is eight times slower, the same reference wants
0.2 s. The fading rate and the optimum averaging time run in opposite
directions on these two.

**And on Window Sum the averaging time does not matter at all** - 0.33 dB
across a slider that spans two and a half orders of magnitude, on every
capture. That mode is 3.5 dB adrift on `002534` for a reason that has
nothing to do with timing. See Finding 22.

### The rule that fits all five captures

Finding 18 found Null improving monotonically as the average shortened on
`000332` and `000747`, and read it as the loop chasing the channel. That
is right as far as it goes, and Null behaves the same way here - `002534`
nulls deepest at 0.2 s (-3.81 dB against -3.14 at 30 s). But the Sum
results above do not fit "faster is better on a fast path", and the reason
is that **the averaging time is set by whichever quantity in the solve is
hardest to measure, not by how fast the channel moves.**

- Where the estimate is easy - a strong, continuous, band-filling signal,
  as in Findings 17 and 18 - shortening the average costs nothing and
  collects the fading. Faster is better.
- Where the estimate is fragile, lengthening it buys more than the fading
  costs. On `002534` the fragile quantity is FSK/Digital's occupancy
  split, which has to find noise-only bins inside a voice passband -
  precisely the failure Finding 6 describes, where "SSB voice
  intermittently fills the passband, so the median rides up with the
  speech". Averaging thirty seconds of it produces a usable noise ratio;
  averaging two tenths of a second does not.
- On `003309` the same split has clean gaps between FT8 signals to work
  in, so it is reliable at any averaging time and short wins again.

The practical form of that: **Null wants the shortest average that still
passes the coherence gate. Sum wants whatever the noise estimate needs**,
which is short on a signal with quiet bins in the window and long on one
without.

### Holding, and why the slider has a floor in practice

The coherence gate holds 37 to 68 % of blocks on these three captures at
most settings, far more than the 23 % of `000747`. On `003309` it reaches **84 % at
tau 30 s** - the accumulators average across FT8 signals that are not
there at the same time, the reported coherence falls below 0.30, and the
loop stops updating. That is the mechanism that eventually punishes a long
average, and it appears in the numbers before the fading does.

## Finding 22: a hot second antenna, and whether system gain has to be corrected before summing

The operator's report had four parts: the second antenna has much more
output; it raises the noise floor of the audio; it does not degrade the
SNR; and setting the ADC1 step attenuator helps. Three of those are
confirmed below. The third is true on two of the three captures and badly
false on the voice one, where the loop gave away 3.6 dB. The capture that
settles the attenuator question is `002710`, where it was moved twice
while recording.

### The imbalance, measured

Guard-region noise floor, arm 1 against arm 0, on the three captures of
2 September:

| | `002534` | `002710` (before any ATT) | `003309` |
|---|---|---|---|
| arm 1 - arm 0, noise floor | **+12.26 dB** | **+13.20 dB** | **+9.78 dB** |
| arm 1 - arm 0, passband | +7.29 dB | +9.28 dB | +9.93 dB |
| **arm 1 - arm 0, SNR** | **-5.09 dB** | **-4.18 dB** | **+0.10 dB** |

So the extra output is not extra signal. On `002534` arm 1 is 12.3 dB
louder and 5.1 dB *worse*; on `003309` it is 9.8 dB louder and exactly as
good. **A level difference says nothing about which antenna to prefer** -
which is the same lesson as Finding 13's quiet doublet, arriving from the
opposite direction.

### What the operator was hearing

`002534`, whole minute, weight applied one block late, everything referred
to arm 0 alone:

| weight | `\|w\|` | output level | output noise floor | SNR | vs arm 0 |
|---|---|---|---|---|---|
| arm 0 alone | 0 | +0.00 dB | +0.00 dB | +19.02 | 0.00 |
| **as it ran on air** | 1.97 | **+14.78 dB** | **+18.31 dB** | +15.41 | **-3.60** |
| equal-noise MRC = what the solve computes | 1.17 | +15.05 | +15.73 | +18.32 | -0.69 |
| noise-aware MRC | 0.072 | +1.92 | +0.56 | +20.39 | **+1.37** |
| ideal causal MVDR | 0.074 | +1.87 | +0.52 | +20.39 | +1.37 |

The audio was **14.8 dB louder with a noise floor 18.3 dB higher**, and
the SNR was **3.6 dB worse than simply listening to ADC0**. The level and
the noise floor are exactly as reported. The SNR is not: on this capture
it was degraded, and the 18.3 dB of extra noise is not a cosmetic
consequence of a louder output but 3.6 dB of real loss hidden inside it.
On `003309` the same loop cost only 0.61 dB and on `002710` plateau C it
gained, which is why the effect can be heard for a long time before it is
believed.

### The mechanism is Finding 20, on a second band and a second mode

`div_apply_weight(sign * acc_xy_re / den, ...)` gives Sum the channel
ratio and nothing else, so the Window and Carrier references assume the
two branches carry equal noise. Here they are 12.3 dB apart. The two MRC
forms differ by exactly that factor and the measurement says so: the
equal-noise weight is `|w|` 1.165, the noise-aware weight is 0.072, and
the ratio is **16.2 against a measured noise ratio of 16.8**.

The cleanest confirmation is that a reference which *does* measure the
branch noise gets it right on the same blocks. `div_digital_solve()` calls
`div_mvdr2(r00, r11, ...)` with `r00` and `r11` accumulated over the
**unoccupied** bins of the window, so FSK/Digital has a genuine per-arm
noise term where Window has none:

| best Sum over the whole tau sweep | Window | FSK/Digital | ideal |
|---|---|---|---|
| `002534` voice | -3.45 dB, level +14.1 | **+0.79 dB, level +2.2** | +1.37, level +1.9 |
| `002710` CW | -1.20 dB, level +13.0 | **+0.74 dB, level +3.0** | +1.60, level +3.2 |
| `003309` FT8 | +0.76 dB, level +17.1 | **+1.48 dB, level +3.3** | +2.04, level +3.5 |

**On all three, FSK/Digital's Sum beats Window's Sum by 0.7 to 4.2 dB and
produces an output 10 to 14 dB quieter.** The two references share
`div_mvdr2()` and differ in one input. This reverses, for badly matched
arms, the advice under Findings 6 and 14 - which were measured on pairs
2.4 to 2.5 dB apart, where the correction is worth a few tenths and
FSK/Digital's other weaknesses dominate.

### The attenuator experiment

`002710` contains the operator's own test. ADC1's noise floor steps down
twice while ADC0's stays at -67.1 dB throughout:

| plateau | blocks | t | arm 1 - arm 0, floor | arm 1 SNR | arm 0 SNR |
|---|---|---|---|---|---|
| A, as found | 2-74 | 0-25 s | **+13.20 dB** | +9.72 dB | +13.90 dB |
| B, ~6 dB in | 78-134 | 26-46 s | **+7.23 dB** | +9.44 dB | +11.88 dB |
| C, ~12 dB in | 138-174 | 46-59 s | **+1.49 dB** | +9.47 dB | +9.16 dB |

Two results, and they are the answer to the question.

**The attenuation cost arm 1 nothing.** Its SNR is +9.72, +9.44, +9.47 dB
across 12 dB of attenuation - flat to a quarter of a decibel. The control
that the band was not simply quiet is arm 0, untouched by the attenuator,
whose SNR fell 4.7 dB over the same minute as the path faded.

**And the optimum did not move either:**

| plateau | ideal `\|w\|` | ideal SNR | vs better arm | as it ran, vs better arm |
|---|---|---|---|---|
| A | 0.107 | +15.59 | **+1.68** | **-3.35** |
| B | 0.255 | +13.47 | **+1.59** | **-3.50** |
| C | 0.624 | +11.36 | **+1.89** | **+1.78** |

The ideal weight simply rescales - `|w|` rises with each 6 dB step, which
is what a weight referred to a quieter arm 1 must do - and the gain it
delivers is 1.6 to 1.9 dB at every setting. **The available SNR is
invariant to system gain.** That is not a surprise: scaling arm 1 by
`alpha` scales `h1` by `alpha` and `R11` by `|alpha|^2`, the MVDR weight
scales by `1/alpha`, and the combination is identical. Finding 11 measured
the same invariance from the other side, where a x10 input scaling left
the answer bit-identical.

What is *not* invariant is what the shipping loop achieved: **-3.35, -3.50
and +1.78 dB**. At plateau C the operator's Window / Sum weight is within
0.11 dB of ideal; at plateau A it is 5.0 dB adrift. Equalising the two
chains did not improve the estimate - it made the estimator's assumption
true.

### So: does system gain have to be corrected before summing?

**For SNR, no in principle and yes in practice - and the practice has now
been fixed.** The optimum weight and the SNR it delivers do not depend on
the branch gains. The Window and Carrier Sum weight was not scale-
invariant, because it assumed the branch noise was equal, and matching the
chains was what made that assumption hold: an effective workaround, worth
5.1 dB here, for a missing term in the solve. That term is now in the
solve - see "What was changed, and what it scored" - so on a path where
the noise ratio can be measured at all, the attenuator setting no longer
decides whether Sum helps or hurts. Where it cannot be measured, because
the signal never stops, the old behaviour is what remains and equalising
the chains is still worth doing.

**For level, yes, and no attenuator setting fixes it.** `src/receiver.c`
forms `i0 + (div_cos * i1 - div_sin * q1)`: arm 0 is pinned at unity and
nothing normalises the result, so the array's gain to the wanted signal is
`g = 1 + w * (h1/h0)` and the output level follows it. Even the *ideal*
weight raises the level - measured median `20 log|g|` of +0.21, +1.50 and
+2.38 dB on the three captures, with a p90 of +2.5 to +6.0 dB - so an
operator switching diversity on hears a step and a higher absolute noise
floor even when the SNR improves. Dividing the combined output by `g`
would hold the wanted signal at exactly its arm-0 level and let the noise
floor fall by the SNR gain instead, which is what diversity is supposed to
sound like. **That is not implemented and is not being implemented here**;
it is one complex divide per block by a quantity the solve already has.

### Two cautions

A digital scale factor is not a step attenuator. Attenuating a hot antenna
reduces what the antenna hears but not what the receiver adds, so far
enough down the arm becomes receiver-noise limited and its SNR really does
degrade. On `002710` that had not begun: arm 1's own floor fell 5.97 and
5.74 dB across two nominal 6 dB steps, and its SNR moved 0.25 dB. The
headroom is not unlimited and this capture does not say where it ends.

And the capture cannot show the attenuator directly. `div_context_changed()`
compares `att0` and `att1`, so each step reset the statistics on the
radio - but `struct divcap_block` mirrors no attenuator field and
`m.rec_flags` is hardcoded to zero, so neither the values nor the resets
are in the file. The plateaus above are inferred from arm 1's own noise
floor, and a `run_ref` replay does not reproduce the two resets. Both are
devtool gaps; see "What is still open".

## Finding 23: a band full of stations, and one weight to serve them all

`003309` is 30 m FT8 - the operator's note is that there are many stations
and they should not correlate. Per signal they correlate extremely well.
It is the passband that does not.

Per 93.75 Hz bin, averaged over the minute, over the operator's 3.3 kHz
filter:

| | value |
|---|---|
| inter-arm coherence, whole passband | **0.413** |
| best individual bins | **0.946, 0.904, 0.862, 0.819, 0.814** |
| `\|h1/h0\|` across the bins carrying signal | -25 to +11 dB |
| `arg(h1/h0)` across the same bins | **-170 to +174 deg, circular sd 68 deg** |
| `\|rho(h)\|` across 94 / 188 / 375 Hz | 0.658 / 0.214 / 0.036 |

Each FT8 signal is 50 Hz wide and arrives from its own direction, so each
has its own differential phase, and twenty-four bins carrying signal spread
that phase around the whole circle. The aggregate coherence is what is
left after averaging them, and 0.413 is **not** the coherence of any
signal in the passband - it is the resultant of two dozen of them.

That matters operationally, because 0.413 is comfortably above
`div_auto_coherence_min` = 0.30. **The gate passes, and the weight the
loop then produces is a compromise aimed at no station in particular.**

### What it costs, and what it does not

The scalar limit here is the sharpest in the document. Weight fitted on
block n, applied to block n+1:

| | `003309` |
|---|---|
| null, one complex weight for the whole band | -2.07 dB |
| null, an independent weight per 93.75 Hz bin | **-7.46 dB** |
| difference | **5.39 dB** |

Against 2.17 dB on `000747` and 0.37 dB on `000332` (Finding 17), this is
the largest per-bin advantage measured - as it should be, since the cause
here is not one path with several modes but two dozen paths with one
direction each.

And yet a scalar weight is still worth **+2.04 dB** on Sum, because the
two antennas happen to have equal SNR on this capture (+20.81 against
+20.90 dB) and the phase spread has a preferred direction rather than
being uniform - a circular sd of 68 degrees leaves a mean resultant of
about 0.49. Narrowing the window onto individual signals does not reliably
improve on that: the three strongest coherent groups give +1.37, +3.03 and
+2.68 dB against the whole passband's +2.04, and the single strongest bin
gives +0.36 because it is already at 30 dB SNR and has nothing to gain.

So the honest summary is not "there is no correlation to find". It is that
**the displayed coherence understates the per-signal coherence by a factor
of two, and the weight is an average over stations that want different
weights.** For a band like this the case for a narrow, hand-placed window
- or the Carrier reference - is that it measures one signal instead of
averaging two dozen, not that it is bound to score better.

## Finding 24: a hot antenna costs dynamic range, and attenuating it is nearly free

Finding 22 answered the operator's question about *SNR* - the available
gain does not depend on the branch gains, and the Sum weight now carries
the noise ratio that made it look as though it did. This is the other
half: below 30 MHz a receiver is rarely limited by its own noise, because
the RF noise floor sits well above thermal, so gain on an already hot
antenna does not buy sensitivity. What it spends is **headroom**.

### How hot, and how little it means

Whole-band rms in the tapped stream, and the guard-region noise floor
beside it:

| capture | arm 1 - arm 0, rms | arm 1 - arm 0, noise floor | arm 1 - arm 0, SNR |
|---|---|---|---|
| `002534` 20 m voice | +7.7 dB | +12.3 dB | **-5.1 dB** |
| `002710` 20 m CW | +7.1 dB | +13.2 dB | **-4.2 dB** |
| `003309` 30 m FT8 | +10.0 dB | +9.8 dB | **+0.1 dB** |
| `111852` 693 kHz | +13.2 dB | +13.1 dB | **+2.2 dB** |
| `112151` 724 kHz | +14.3 dB | +14.5 dB | **-1.6 dB** |
| `000332` 5.4 MHz | **-10.0 dB** | -6.3 dB | **+3.5 dB** |

Seven to fourteen decibels of extra output on the five hot-arm captures,
and the SNR that comes with it runs from -5.1 to +2.2 dB. `000332` is the
control and it runs the other way: there arm 1 is 10 dB *quieter* and
3.5 dB better. **Level and sensitivity are unrelated, and every decibel of
the difference is spent on headroom rather than on hearing.**

### What attenuating it costs: measured

`002710` is the experiment, and it is worth being precise about what it
does and does not establish.

**Established, with no model in the way.** Across two steps of the ADC1
attenuator, arm 1's own SNR was +9.72, +9.44 and +9.47 dB - flat to a
quarter of a decibel - while ADC0, untouched, fell 4.7 dB over the same
minute as the path faded. The available two-branch gain was +1.68, +1.59
and +1.89 dB over the better arm at the three settings. So the
attenuation cost that arm **0.25 dB in total** and cost the array nothing,
and the control says the band really was moving while it happened.

**Not established: how much further one could go.** The obvious next step
is to fit `P(a) = E/a + I` - external noise attenuated, the receiver's own
not - and read off the headroom. It does not survive contact with the
data, because *the capture does not record what the attenuator was set
to*. The observed floor steps are 6.06 and 5.67 dB, which bracket the true
steps without pinning them, and the fit is violently sensitive to the
difference:

| assumed true step | implied `I/E` | implied cost of 20 dB | of 30 dB |
|---|---|---|---|
| 6 dB | -30 dB | 0.4 dB | 2.9 dB |
| 7 dB | -13.7 dB | 7.0 dB | 16.2 dB |

and the same fit run against the measured SNR rather than the floor gives
-24 dB from the 12 dB step and -16 dB from the 6 dB one, which do not
agree either. **One capture with unrecorded attenuator settings cannot
answer this**, and saying it could would be the third time in this
document that a tidy model was fitted to a number that would not hold it.

That is now fixable rather than merely regrettable: the capture format
records `att0` and `att1` from version 2, so the same minute recorded
again would settle it. Until then the honest statement is that **at least
12 dB is nearly free** and the curve beyond that is unmeasured.

### So: is there a case for attenuating the hotter antenna?

Yes, and it is now a *dynamic range* case only, which is a change from
what Finding 22 could say.

Before the Sum weight carried the noise ratio there were two reasons to
equalise the chains, and the SNR one dominated: an unequalised pair cost
3.6 dB on `002534` because the solve assumed the branches were equally
noisy. That reason is gone. What remains is that ten to fourteen decibels
of gain on an arm whose SNR is no better - and on these captures usually
worse - consumes converter and analogue headroom to no purpose, and
headroom below 30 MHz is what a receiver runs out of first.

The measured cost of taking it back is 0.25 dB over 12 dB. The measured
benefit to the array is zero, because the array never needed the gain. The
benefit to everything upstream of the tap is real and is not something
this instrument can see.

### What this instrument cannot see

The capture is taken after the DDC, so its levels are the tap's scaling
and not the converter's: the tapped rms runs -53 to -93 dBFS across these
captures, which says nothing about how close either ADC came to clipping.
**No headroom figure in this document is a headroom figure at the
converter.** The case above rests on the measured SNR cost of attenuating
and on the invariance of the available gain, both of which the tap does
see. Confirming the other half - that the hot arm was actually eating
headroom - needs the radio's own ADC overload indication, and would make a
good companion measurement to a capture taken with the attenuator swept.

## Finding 25: what each reference measures, and which signals defeat it

Twenty-three findings in, the document has never said in one place what
the four references actually measure and where each stops working. This is
that summary. Nothing here is new measurement; every row points at the
finding that produced it.

### The four, and what each is looking at

| reference | channel `h` from | noise / interference from | gate statistic |
|---|---|---|---|
| **Window** | cross-spectrum over the whole analysis window | **nothing** until Finding 22 - now a windowed minimum | `γ²` over the window |
| **Carrier** | the same over `2·DIV_CARRIER_BINS+1` = 5 bins | as Window | `γ²` over those 5 bins |
| **FSK/Digital** | cross-spectrum over the *occupied* bins | the **unoccupied** bins of the same window | `γ²` over the occupied bins |
| **RADE V1** | pilot correlation, `d1·conj(d0)` | off-carrier bins of the pilot span | `rade_corr_quality`, a signal fraction |

Only two of the four have ever had a real noise measurement, and that is
the single fact that explains most of the differences between them.
FSK/Digital's occupancy split is why it was the only reference that got
Sum right on a badly matched pair before Finding 22 (`+0.74` against
Window's `-1.20` dB on `002710`); RADE V1's guard bins are why it can run
a genuine MVDR and null an interferer the pilot is not pointed at.

### Where each fails, and why

| reference | fails when | evidence |
|---|---|---|
| Window | the window is mostly noise - voice, which fills it only intermittently | flat weighting holds 83 % of the time on speech (Finding 6) |
| Window | the passband holds several signals wanting different weights | FT8: per-bin coherence 0.946, aggregate 0.413 (Finding 23) |
| Carrier | the carrier's spatial signature differs from the band's | 3.6 dB given up on `111852`, 9 degrees of difference (Finding 16) |
| Carrier | few bins, so `γ̂²` is biased high and the gate is nearly a formality | see the `1/N` floor below |
| FSK/Digital | no false-alarm control on occupancy | a weight on 30 % of blocks with **no signal at all** on `231532` (Finding 8) |
| FSK/Digital | the passband is full, so there are no noise bins | the "region is full" fallback, plain MRC (Finding 8) |
| RADE V1 | no pilot | reports nothing on the four voice captures, correctly (Finding 14) |
| all four | one complex weight, several propagation modes | 188 Hz coherence bandwidth on `000747`; 2.2 dB left to a per-bin weight (Finding 17) |

### Against signal type, which is the axis that actually predicts it

| signal | what it does to the estimator |
|---|---|
| continuous band-filling digital | the easy case: coherence 0.90, scalar model holds, every reference works (`000332`) |
| multi-mode low-band path | coherence bandwidth 188 Hz across a 3.8 kHz filter - twenty independent cells, and a scalar weight is one of them (`000747`) |
| many independent stations | per-signal coherence up to 0.95, aggregate 0.41, and the weight serves none of them; the largest per-bin gap in the set, 5.4 dB (`003309`) |
| analog voice | fills the window only in bursts, so the gate statistic is diluted and the weighting control exists to compensate (Finding 6) |
| CW | a window of a few bins, where the estimator's own bias is comparable with the thing being measured (Findings 8 and 21) |
| a single strong carrier | the array has nothing to add if the noise arrives from the same direction - 0.17 dB available on `111852` (Finding 16) |
| dead air | every reference holds, correctly, on all seven no-signal captures except FSK/Digital's occupancy (Finding 8) |

The pattern worth carrying away is that **band predicts very little and
signal structure predicts almost everything**. The same two antennas on
the same afternoon gave coherence 0.90 and a flat channel on one frequency
and 0.50 with twenty frequency cells on another 140 kHz away.

## Finding 26: one slider, four statistics, and one mode where it did nothing

`div_auto_coherence_min` is a single number compared in three places, and
each place compares it with something different:

| where | what is compared | reference |
|---|---|---|
| `diversity_auto.c:2425` | `γ²` over the whole analysis window | Window, Carrier |
| `diversity_auto.c:1845` | `γ²` over the **occupied** bins | FSK/Digital |
| `diversity_auto.c:1663` | a **single bin's** `γ²`, deciding which bins are occupied | FSK/Digital |
| — | nothing | **RADE V1** |

Three consequences, in increasing order of how much they cost.

**The control was inert in RADE V1.** `div_auto_coherence` is set to
`rade_corr_quality` there and then never compared with anything. The menu
row was hidden in that mode, which is at least consistent, but it means
the one reference where an operator most often watches a marginal signal
had no way to say "do not act on that".

**It did two jobs at once in FSK/Digital.** Moving the slider changed
which bins the estimate is *made from* as well as whether the estimate was
acted on. Those want different numbers: the per-bin test is a false-alarm
test on one bin, where `γ̂²` is biased upward and a noise-only bin is small
rather than silent.

**And the numbers are not comparable.** For equal arms and uncorrelated
noise a `γ²` gate at `g` demands a per-arm SNR of `√g/(1−√g)`; a quality
gate at `q` demands `q/(1−q)`, because `rade_corr_quality` is
`acc_sig/(acc_sig + acc_r00)` - a signal fraction, not a coherence:

| slider | Window / FSK, `γ²` | RADE V1, quality |
|---|---|---|
| 5 % | −5.4 dB per arm | −12.8 dB pilot |
| 30 % | **+0.8 dB per arm** | **−3.7 dB pilot** |
| 60 % | +5.4 dB | +1.8 dB |
| 90 % | +12.7 dB | +9.5 dB |

### What each reference actually reports

`run_ref` over all thirty-two captures, every reference, at each
capture's own averaging time. The signal rows exclude the five no-signal
captures; the RADE rows count locked blocks only.

| reference | blocks | median | p10 | p90 | ≥ 0.30 |
|---|---|---|---|---|---|
| Window | 11313 | 0.503 | 0.112 | 0.814 | 75.5 % |
| Carrier | 11313 | 0.165 | 0.015 | 0.622 | 34.7 % |
| FSK/Digital | 11313 | 0.556 | 0.118 | 0.881 | 79.5 % |
| RADE V1 | 4432 | 0.681 | 0.358 | 0.850 | 93.3 % |

And on the five captures with no signal in them - what a threshold exists
to reject:

| reference | median | p90 | p99 | ≥ 0.30 |
|---|---|---|---|---|
| Window | 0.048 | 0.181 | 0.505 | **5.7 %** |
| Carrier | **0.197** | **0.563** | **0.951** | **36.0 %** |
| FSK/Digital | 0.048 | 0.213 | 0.614 | 9.2 % |
| RADE V1 | — | — | — | never locks |

### The Carrier reference's gate does not work at all

Read those two tables together for Carrier. On a signal it clears 0.30 on
34.7 % of blocks; on **pure noise** it clears 0.30 on 36.0 %. The
threshold is very slightly more likely to pass when there is nothing there
than when there is.

The cause is arithmetic rather than a fault. `γ̂²` over `N` independent
averages sits at about `1/N` on uncorrelated arms, and Carrier accumulates
`2·DIV_CARRIER_BINS+1` = **five** bins where Window accumulates a couple
of hundred. Choosing the threshold from the data rather than by hand makes
it plain — the value that lets only 5 % of no-signal blocks through, and
what each reference then keeps of the signal:

| reference | threshold for 5 % false alarm | signal blocks kept | today at 0.30 |
|---|---|---|---|
| Window | 0.34 | **71.3 %** | 5.7 % false / 75.5 % kept |
| Carrier | 0.71 | **5.6 %** | 36.0 % / 34.7 % |
| FSK/Digital | 0.48 | **60.4 %** | 9.2 % / 79.5 % |

Window and FSK/Digital separate signal from noise properly. **Carrier does
not separate them at any threshold** - its ROC is nearly the diagonal, and
no value of this control can fix that. What would is more bins or a longer
average, which is a change to `DIV_CARRIER_BINS`, not to a threshold, and
is not made here.

### What was changed, and why the defaults did not move

The threshold is now stored **per reference** rather than per mode group,
which is the axis that fixes its meaning, and RADE V1 has one of its own.
The FSK/Digital per-bin test has been split out as `DIV_OCC_COH`.

The defaults stay where they were, and the sweep is the reason: for
Window, 0.30 gives 5.7 % false alarms against an optimum of 0.34 at 5.0 %
- the same point to within the measurement. For FSK/Digital the band gate
at 0.30 costs 9.2 %, and raising it to 0.48 would halve that at a cost of
19 points of uptime; the false-alarm problem in that mode is the *per-bin*
occupancy test (Finding 8), which is now a separate constant and is where
that work belongs. RADE V1 defaults to **zero**, which is the behaviour it
replaces exactly - and there is no no-signal data to set it from, because
the correlator's own acquisition never locks on any of the five, so
`RADE_USE_RATIO` is already doing that job.

So the change buys the operator four controls that mean four things
instead of one that meant four, and buys the code a per-bin threshold that
can be swept without moving a gate. It does not, by itself, change what
the radio does.

## Finding 27: coherence weighting is a gate bias, not a better estimate

Finding 6 measured flat against coherence weighting on two voice captures
and concluded that coherence weighting "earns its place, mostly by passing
the gate" - it lifted the fraction of blocks that produced a weight from
17 % to 43 % and from 18 % to 30 %. That is correct and it is not the
whole comparison.

### It does not improve the estimate

Scored offline with the branch noise ratio supplied and no gate in the
way, so that only the estimator is being measured. Output SNR against the
better arm:

| capture | flat | `γ²` | `γ²/(1−γ²)` | debiased `γ²` | ideal |
|---|---|---|---|---|---|
| `000332` | **+1.65** | +1.61 | +1.59 | +1.60 | +1.61 |
| `000747` | **+2.86** | +2.83 | +2.72 | +2.83 | +2.80 |
| `002534` | **+1.33** | +1.26 | +1.26 | +1.20 | +1.37 |
| `002710` | **+1.55** | +0.89 | +0.48 | +0.59 | +1.60 |
| `003309` | +2.06 | +1.98 | **+2.09** | +1.93 | +2.04 |

Flat is as good or better on four of five and 0.66 dB better on `002710`.
`γ²/(1−γ²)` - the minimum-variance weight for combining per-bin channel
estimates, and the obvious principled candidate - is no better, and
debiasing `γ̂²` first is no better either. **None of the weightings beats
simply summing the cross-spectrum.**

### And it does not improve the gate either, once the comparison is fair

Coherence weighting raises the reported `γ²` on **all thirty-two
captures**. It raises it on the ones with no signal in them too: `231532`,
which holds nothing, goes from 0.019 to 0.047 and its holding fraction
falls from 91 % to 77 %. Selecting the most coherent bins and then
measuring the coherence of what you selected is a biased estimator, and
the bias does not know whether there is a signal underneath it.

Comparing the two at a **fixed threshold** therefore compares them at
different false-alarm rates, which is what Finding 6 did:

| at the shipped 0.30 | signal blocks passed | no-signal blocks passed |
|---|---|---|
| flat | 67.5 % | 2.1 % |
| coherence | 75.5 % | 5.7 % |

Coherence weighting looks better because it is operating further up the
same curve. Holding the false-alarm rate constant instead, over all
thirty-two captures:

| false alarm | flat: threshold, signal kept | coherence: threshold, signal kept | difference |
|---|---|---|---|
| 2 % | 0.310 → **66.0 %** | 0.435 → 60.5 % | **−5.6 points** |
| 5 % | 0.220 → **74.5 %** | 0.335 → 71.8 % | **−2.7 points** |
| 10 % | 0.110 → **86.2 %** | 0.185 → 84.8 % | **−1.4 points** |

**Flat is better at every operating point.** The gain Finding 6 measured
was the threshold being effectively lowered, and the threshold control
does that on its own, transparently, and without also biasing the number
the operator is shown.

### What follows

On this evidence the control that has no remaining purpose is **coherence
weighting**, not flat - which is the opposite of the question that
prompted the measurement. Three qualifications before anything is removed:

- the estimate column is scored on five captures with a passband SNR
  metric that Finding 23 shows is unreliable where a scalar weight cannot
  serve the passband, and `003309` is exactly that case;
- the ROC comparison rests on five no-signal captures, and `231532` is the
  only one of them that is not already holding 100 % of the time, so the
  false-alarm sample is thinner than the block counts suggest;
- coherence weighting is the shipping **default**, so retiring it moves
  every operator's operating point unless the thresholds move with it.
  Those two decisions are coupled and want making together.

Nothing has been removed. The finding is recorded and the control is left
alone; what it changes today is that `diversity.md` should stop describing
coherence weighting as the better estimator, because it is not one.

## Finding 28: the attenuator question, answered — and a wanted signal under common-mode noise

Two captures on 2 September, and the first taken with the capture format
that records the step attenuators. They answer the question Finding 24
had to leave open and they add the case the document has been asking for
since Finding 5.

| | `142026` | `142333` |
|---|---|---|
| band, mode | 11.65999 MHz, **DRM** in `AM` | 21.04004 MHz **CWL** |
| filter, engine window | +/-6000 Hz | -1050..-50, window **-500..+500** |
| reference, weighting | Window, **flat** | Window, coherence |
| averaging | 0.20 s | 2.90 s |
| attenuators | ADC1 at **23 dB**, stepped to 21 | **both swept**, 0 to 4 dB |
| what the operator did | cycled objective, reference and weighting | swept ADC1 up and back, then ADC0 |

### `142333`: the attenuator behaves exactly as the premise says

Finding 24 could not fit a receiver-noise contribution to `002710`,
because the attenuator settings were not recorded and the answer swung
from -30 to -14 dB across plausible step sizes. Here they are recorded,
and the fit is not needed - the numbers can simply be read:

| | guard-region floor | change |
|---|---|---|
| ADC1 at 0 dB | -48.67 dB | — |
| ADC1 at 1 dB | -49.70 | **-1.03** |
| ADC1 at 4 dB | -52.67 | **-4.00** |
| ADC0 at 0 dB | -59.86 | — |
| ADC0 at 2 dB | -61.84 | **-1.98** |
| ADC0 at 4 dB | -63.88 | **-4.01** |

**Both arms track the attenuator one for one, to within 0.03 dB over
4 dB.** Nothing is compressing the step, which is what a receiver noise
floor near the band's would do. Reading the precision as a bound: a step
short by 0.05 dB would put the receiver's own noise 21 dB below the
band's, and one short by 0.02 dB would put it 25 dB below. It is at least
that far down on both arms, on 15 m at midday, and the measurement cannot
say how much further.

So the operator's premise holds where it was tested, and the practical
form of it is that **the first 4 dB of attenuation on either arm is free
to within the measurement**, with the cost curve only starting where the
band noise stops dominating - more than 20 dB away here.

The available two-branch gain does not move with the attenuator either,
which is the other half of Finding 22's invariance seen on a second
capture and this time on *both* arms:

| att0 / att1 | arm 0 SNR | arm 1 SNR | ideal, vs the better arm |
|---|---|---|---|
| 0 / 0 dB | +10.19 | +7.25 | **+1.96** |
| 0 / 4 dB | +10.54 | +9.62 | **+2.24** |
| 2 / 0 dB | +8.45 | +4.15 | +1.02 |
| 4 / 0 dB | +16.04 | +11.42 | +1.12 |

The per-arm SNR wanders by 12 dB across the minute because the signal is
keyed CW on a fading path, so those columns are not comparable between
rows; the last column is, and it stays between +1.0 and +2.2 dB with no
trend against either attenuator.

`142333` is otherwise a hot-arm capture of the usual kind: ADC1 11.2 dB
above ADC0 on noise and 3.4 dB worse on SNR, coherence time about 1.5 s,
and only 0.53 dB available to a per-bin weight - a single narrow signal in
a 1 kHz window has no frequency structure to exploit.

### `142026`: a digital broadcast under strongly common-mode noise

This is the case listed as open since Finding 5 and again after Finding 16:
a signal worth listening to, sitting on noise the two antennas largely
share.

**It is not AM.** The radio was in `AM` with the filter opened to
+/-6 kHz, which is how DRM is fed to a decoder, but the signal is Digital
Radio Mondiale - and reading it as a broadcast carrier would have made
nonsense of everything below. Three measurements identify it. The occupied
band is **9703 Hz** wide with sharp skirts; there is **no carrier**, the
strongest bin standing only 7.3 dB over the in-band median and wandering
about rather than sitting at zero; and the cyclic-prefix autocorrelation
peaks at a lag of **21.30 ms**, against the 21.33 ms useful symbol
duration of DRM **mode B**, two and a half times higher than any other
lag. Scored on a 46.875 Hz grid - mode B's subcarrier spacing - 209
subcarriers stand within 12 dB of the strongest, spanning +/-4875 Hz,
against the 206 that mode B's 10 kHz configuration carries.

So it is an OFDM broadcast, not a modem with a pilot, and it still says
nothing about the pilot-domain covariance. What window and tracking method
suit it is Finding 30.

| | value |
|---|---|
| passband coherence | 0.877 |
| **guard-region noise coherence** | **0.74 to 0.75** |
| arm 1 - arm 0, passband / noise | -0.61 / -0.99 dB |
| arm 1 advantage, SNR | +0.38 dB |
| signal over the floor | about 41 dB |

The noise coherence was checked in four separate guard regions either side
of the passband - 0.735, 0.741, 0.751, 0.753 - because a broadcast 41 dB
above the floor is exactly the signal that could put splatter in a guard
band and fake the number. It did not: the four agree to 0.02.

Two things follow.

**The operator had already equalised the chains, with 23 dB.** The two
arms arrive within a decibel of each other on both signal and noise, which
is what Finding 24 recommends and is the first capture in the set taken
that way deliberately. It is also why this capture says nothing about the
Sum weight's noise ratio: with the branches matched there is nothing for
that term to correct.

**And the scalar weight is at its worst here.** Weight fitted on block n
and applied to n+1:

| | `142026` |
|---|---|
| null, one complex weight for the whole band | -8.08 dB |
| null, an independent weight per 93.75 Hz bin | **-16.46 dB** |
| difference | **8.38 dB** |

That is the largest per-bin advantage in this document - against 5.4 dB on
30 m FT8 (Finding 23) and 2.2 dB on the multi-mode 5 MHz path
(Finding 17). The cause is a 12 kHz window, four times wider than anything
measured before, over which `|rho(h)|` falls to 0.78 at 750 Hz and 0.70 at
1.5 kHz: about four independent frequency cells, each wanting its own
weight, and a scalar getting one answer for all of them.

The scalar still nulls 8 dB, which is a good deal more than the 0.8 to
3 dB the fast-fading captures allowed, because here the thing being
nulled is 0.75 coherent and sitting still. **Common-mode noise is what a
two-branch array is for, and this is the first capture in the set where a
wanted signal and that kind of noise appear together.**

## Finding 29: the threshold and the weighting, chosen together

Finding 27 left this open: coherence weighting measures no better than
flat and worse at matched false alarm, but it is the shipping default, so
retiring it moves every operator's operating point unless the threshold
moves with it. The two decisions are coupled and wanted settling together.

This is that sweep. `run_ref` gained a `--cohmin` option; the Window
reference was run over seven captures that have an independent noise
reference, at six thresholds and both weightings, each at the averaging
time the operator actually had. The false-alarm column is the fraction of
blocks on the five no-signal captures that would have produced a weight -
which needs no extra runs, because the gate sits downstream of the
accumulators and the statistic it compares does not depend on it.

| threshold | flat: FA | flat: SNR | flat: hold | coherence: FA | coherence: SNR | coherence: hold |
|---|---|---|---|---|---|---|
| 0.00 | 100 % | **−0.75** | 9 % | 100 % | −0.98 | 13 % |
| 0.10 | 11.3 % | −0.89 | 21 % | 20.7 % | −1.11 | 23 % |
| 0.20 | **5.2 %** | **−1.02** | 28 % | 8.8 % | −1.17 | 27 % |
| 0.30 | 2.1 % | −1.14 | 35 % | **5.7 %** | **−1.20** | 34 % |
| 0.45 | 0.6 % | −0.99 | 51 % | 1.4 % | −1.18 | 48 % |
| 0.60 | 0.0 % | −1.26 | 64 % | 0.5 % | −1.23 | 60 % |

SNR is the mean over the seven captures against the better antenna. The
absolute level is unflattering and should not be read as a verdict on the
loop: the runs use each capture's *recorded* averaging time, which on
`002534` is the 0.2 s that Finding 21 measured as its worst setting, and
two of the seven are cases the scalar weight is known to lose on. What is
being compared here is the columns against each other, and for that every
row is a like-for-like pair.

### Three things fall out

**The gate is not free.** Reading down either SNR column, every increment
of threshold costs mean SNR: 0.5 dB from "act on everything" to "act on
almost nothing". That is the gate blocking legitimate updates as well as
illegitimate ones, and it is the trade the control exists to let an
operator make. It has not been stated in this document before.

**Flat wins at every matched operating point, end to end.** Comparing at
equal false alarm rather than at equal threshold:

| false alarm | flat | coherence | flat ahead by |
|---|---|---|---|
| ~11 % | 0.10 → **−0.89 dB** | 0.20 → −1.17 dB | **0.29 dB** |
| ~5 % | 0.20 → **−1.02 dB** | 0.30 → −1.20 dB | **0.18 dB** |
| ~2 % | 0.30 → **−1.14 dB** | 0.45 → −1.18 dB | 0.03 dB |

which confirms Finding 27's ROC result in decibels rather than in
detection points, and adds that the margin closes as the threshold rises -
the two weightings converge where almost everything is being held anyway.
Per capture at the 5 % point, flat is ahead or level on **seven of seven**,
by 0.00 to 0.63 dB.

**Flat wants a threshold about 0.10 lower** for the same false-alarm rate,
which is the practical form of the bias Finding 27 identified: coherence
weighting inflates the statistic, so the same number is a laxer test.

### What the pair should be

The shipping pair is **coherence at 0.30**: 5.7 % false alarm, −1.20 dB.
**Flat at 0.20** gives 5.2 % false alarm and −1.02 dB - very slightly
*fewer* false alarms and 0.18 dB more signal. It dominates the current
default on both axes, and there is no operating point at which coherence
weighting is the better half of the pair.

**That change is justified by these measurements and is not being made
here.** What holds it back is the same thing that made the earlier
single-axis comparisons misleading: seven captures, one reference, and a
metric whose absolute values are poor enough that a 0.18 dB mean is a
small signal to move a default on. What would settle it is the same sweep
against a decoder on the RADE captures, where the yardstick is synced
frames rather than a passband ratio - and that is the one measurement this
document trusts most and has never applied to the weighting question.

The other three references are untouched by any of this: the weighting
control only ever reached `DIV_REF_BAND`, FSK/Digital hard-codes coherence
weighting of its own, and Carrier's gate does not discriminate at any
threshold (Finding 26).

## Finding 30: DRM — the objective is the only setting that matters

`142026` is a DRM mode B broadcast (Finding 28), and the question it was
recorded to answer is which window and which tracking method suit it. The
answer is that **none of them make a measurable difference and the
objective makes a six-decibel one.**

### Scoring an OFDM signal properly

A band-aggregate SNR is the wrong yardstick here. DRM equalises every
subcarrier separately from its own scattered pilots, so frequency-selective
*distortion* is the decoder's problem and it solves it; what it cannot fix
is a subcarrier pushed into the noise, and coded OFDM fails when too many
of them are. A scalar diversity weight across a 10 kHz band whose
inter-arm channel is not flat could do exactly that - co-phase the middle
and subtract at the edges - and an aggregate power ratio would not show it.

So everything below is scored **per subcarrier and per block**: 351
analysis blocks times 209 subcarriers on the 46.875 Hz mode B grid, noise
taken from a guard region clear of the signal. The columns are the mean
over those 73 000 cells, their 10th percentile, and the fraction falling
below 30 dB - the tail that a decoder actually notices.

### The shipping engine, every reference and both objectives

| | mean | p10 | % < 30 dB | mean `\|w\|` |
|---|---|---|---|---|
| arm 1 alone (the better arm) | +40.48 | +36.11 | 1.39 % | — |
| **Window / Sum** | **+40.76** | **+36.65** | **0.93 %** | 0.86 |
| Carrier / Sum | +40.66 | +36.47 | 1.02 % | 1.07 |
| FSK/Digital / Sum | **+39.76** | +34.42 | 2.52 % | 0.69 |
| Window / Null | **+34.12** | +26.63 | **21.1 %** | 0.95 |
| Carrier / Null | +35.05 | +26.83 | 19.4 % | 1.05 |
| FSK/Digital / Null | +34.16 | +26.54 | 21.2 % | 1.04 |

**Null costs 5.4 to 6.4 dB and puts a fifth of all subcarriers below
30 dB, against 1.4 % on one antenna alone.** On a wanted OFDM signal the
minimum-power weight is a signal-cancelling weight, and the capture
contains the operator running it for the first twenty-one seconds and
again from t = 31.7 s. That is the one setting on this menu that can ruin
a DRM decode, and it does not look wrong from outside - the loop reports a
healthy coherence throughout.

**FSK/Digital is the wrong reference for DRM**, by 0.71 dB below the
better antenna and 2.52 % of cells in the tail. Its noise pair comes from
the *unoccupied* bins of the window, and a band-filling OFDM block has
none - this is the "the region is full" fallback of Finding 8, reached on
a real signal and measured for the first time.

**Window and Carrier both work**, which is worth saying because there is
no carrier in a DRM signal at all: at 40 dB SNR any five bins estimate the
channel well enough, so the Carrier reference tracking a wandering OFDM
peak still lands within 0.1 dB of Window.

### Window width, weighting and averaging do not matter

Emulated over the same capture so that the window can be placed anywhere,
Sum in every case:

| window and setting | mean | p10 | % < 30 dB |
|---|---|---|---|
| follow the filter, +/-6000 Hz, flat, tau 0.2 | +40.84 | +36.76 | 0.85 % |
| the same, coherence weighting | +40.84 | +36.76 | 0.84 % |
| narrow, +/-500 Hz on the band centre, tau 1 | +40.86 | +36.89 | 0.78 % |
| narrow, +/-500 Hz at **-4000 Hz** | +40.87 | +36.83 | 0.82 % |
| narrow, +/-500 Hz at **+4000 Hz** | +40.84 | +36.77 | 0.88 % |
| five bins only | +40.88 | +36.93 | 0.77 % |
| a window placed **off the signal**, +5000..+6000 | +40.70 | +36.51 | 1.06 % |

Everything from five bins to the whole passband lands within **0.04 dB**,
and a weight estimated at one edge of the block does no harm at the other.
Averaging from 0.2 to 10 s moves nothing by more than 0.05 dB either. Even
a window sitting entirely *off* the signal still delivers +0.23 dB, because
what it measures there is the common-mode noise and co-phasing on that is
most of the answer.

The reason none of it matters is the last row of the sum: with the two
arms within 0.4 dB of each other and their noise **0.76 correlated**, the
most that maximum ratio combining can give is

`10 log10( 2 / (1 + r) )` = **+0.56 dB**

and the shipping loop collects +0.28 to +0.40 of it. There is no
estimation problem to solve at 40 dB SNR - the prize is small because the
noise is shared, and every reasonable estimator finds it.

### What the scalar weight does *not* do

It does not notch the band. The fraction of cells below 30 dB falls from
1.39 % on the better antenna to 0.93 % with Window / Sum, and the spread
of per-subcarrier SNR across the block is 0.70 dB against 0.73 dB for one
antenna alone. The worry that a single complex weight would co-phase the
middle of a 10 kHz OFDM block and subtract at its edges is not borne out
here - though this capture cannot test it hard, because the inter-arm
channel would have to be far more selective than it is before 0.4 dB of
array gain could turn into a notch.

### What is still missing

All of this is measured at 40 dB SNR, where the estimate is free. The
window and averaging questions have teeth only where the estimate is hard,
and that means a **weak** DRM signal - one near the decoder's threshold,
where a narrow window may not have enough signal to work from and a wide
one may span more channel variation than a scalar can follow. That capture
has not been taken. The same is true of the notch worry: it needs a path
whose inter-arm channel varies far more across 10 kHz than this one's.

## Finding 31: FSK — settings to hear it, settings to null it, and a second source proved by its signature

`154822` is 14.117805 MHz `USB`, a 150-2550 Hz filter, Window with
coherence weighting, Sum, averaging 3.41 s, both attenuators at zero, and
not a single operator change in the minute. It carries an FSK signal that
stops part way through, and the operator's note is that what is left
afterwards may be a different source. It is, and the diversity data is
what proves it.

### What is actually there

Measured at 5.86 Hz resolution and by mixing each tone to zero:

| | |
|---|---|
| tones | **+1096 Hz** and **+1301 Hz** in the operator's shifted frame |
| shift | **205 Hz** |
| keying | run lengths cluster at 20 and 40 ms -> unit element 20 ms, **50 baud** |
| inter-arm channel `h1/h0` | **+4.4 dB at -161 deg**, coherence **0.996** |

Fifty baud on a 200 Hz shift is a utility FSK format, not the amateur
45.45 baud on 170 Hz - worth stating, because the tone spacing is what a
hand-placed window has to cover.

The minute has four parts:

| t | what |
|---|---|
| 0 - 6 s | FSK keying between both tones |
| 6 - 30 s | **the mark tone alone**, idling, same signature to within 1 dB and 5 deg |
| 30 - 42 s | the carrier fades out; coherence falls to 0.6 and the tone is gone |
| 42 - 60 s | it returns, `h1/h0` **+1.8 to +5.4 dB at -161 to -171 deg** |

So the carrier that comes back is the *same* source. What appears while it
is away is somewhere else entirely.

### The second source, and how the signature settles it

During the gap a signal switches on **4.5 to 5.0 kHz above the dial** -
outside the operator's 2550 Hz filter, so audible only if he retunes,
though the panadapter shows it. It is present at t = 34-41 s and again at
47-51 s, and absent otherwise: 25 to 37 dB over the floor with an inter-arm
coherence up to **0.98**.

| | `h1/h0` |
|---|---|
| the FSK station and its idling carrier | **+4.4 dB at -161 deg** |
| the signal revealed in the gap | **-0.6 dB at +160 deg** |

Five decibels apart in magnitude and thirty-nine degrees apart in phase,
each measured at coherence above 0.98, which is far outside what either
estimate could wander by. **They arrive by different paths, so they are
different sources** - and that is a conclusion a single receiver cannot
reach, because from one antenna the two look like the same kind of carrier
appearing and disappearing on a busy band.

### To hear it: any window works, but the reference decides the level

Scored over t = 0-30 s while the signal is on, as tone SNR - the two tone
bands against in-band noise taken from 1600-2400 Hz, where the inter-arm
noise coherence is only 0.088 so there is a full three decibels of
diversity gain on the table:

| setting | tone SNR | tones | noise | output level | `\|w\|` |
|---|---|---|---|---|---|
| arm 0 alone | +39.52 | 0.00 | 0.00 | — | — |
| arm 1 alone | +40.14 | +4.56 | +3.94 | — | — |
| **FSK/Digital, follow** | **+42.10** | +5.28 | +2.70 | **+5.3 dB** | 0.66 |
| Window, follow the filter | +41.98 | +10.73 | +8.26 | +10.7 dB | 1.05 |
| Window, hand-placed on both tones | +41.92 | +10.73 | +8.32 | +10.7 dB | 1.10 |
| Window, on the mark tone only | +41.48 | +10.53 | +8.57 | +10.5 dB | 1.17 |
| Carrier, on the mark tone | +41.48 | +10.53 | +8.57 | +10.5 dB | 1.21 |

**Every setting is within 0.6 dB on SNR** - the signal is 40 dB out of the
noise and a coherence of 0.996 makes the channel trivial to estimate from
any of these windows. What separates them is the **output level**:
FSK/Digital lands the same SNR with 5.4 dB less level rise, because it is
the one reference that measures the branch noise and so applies `|w|` 0.66
where the others apply 1.05 to 1.21 (Finding 22). On a signal like this
that is the whole difference an operator would notice.

**So: FSK/Digital, follow the filter.** It is the reference this mode was
written for, the occupancy split finds the two tones without being told
where they are, and it is the only one that does not make the audio jump.

### To null it: the one-block lag is the limit, not the averaging

| | tone suppression |
|---|---|
| ceiling from the coherence, `10 log(1-γ²)` at γ = 0.9959 | **-20.83 dB** |
| ideal weight, computed and applied on the same block | -31.77 dB |
| **ideal weight, applied one block late** - what the architecture allows | **-13.71 dB** |
| shipping loop, window on both tones, tau 3.41 s | **-11.05 dB** |

The loop collects 11 dB of a reachable 13.7, and **the averaging time is
almost irrelevant**: -11.41 dB at tau 0.2 s against -10.94 at 10 s, half a
decibel across the whole slider. What tau changes is how often the loop is
live at all - **holding runs 8 % at 0.2 s and 78 % at the 3.41 s the
operator had**.

The gap between 13.7 and 20.8 dB is the one-block lag. The weight is
computed from block n and applied to block n+1, 171 ms later, and this
path moves enough in 171 ms to cost seven decibels of null depth. That is
not something a menu setting can recover; it is the block period, which
the Resolution control sets.

**So: to null this, window on the tones, averaging short.** Short does not
deepen the null - it keeps the loop out of hold, which is what matters
when the interferer is keying and the path is moving. A finer Resolution
would shorten the block and is the only thing here that could take the
null past 14 dB, at the cost of a noisier per-block estimate; that has not
been tested.

### Nulling one source does not null the other

The weight that nulls the FSK leaves the second source alone - it comes
out **5.9 dB up** relative to arm 0, not down, because the null is aimed
39 degrees and 5 dB away from where that signal sits. Two sources with
different spatial signatures, one cancelled by 11 dB and the other
untouched, is exactly what a two-branch array is for, and it is the first
time this document has been able to show it on two identified signals
rather than on a signal against its own noise.

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

### How much margin is left, and does `112151` force a change

Swept finely, `112151` clears at **2.375** - the false lock survives 2.25
and is gone by 2.375. The shipping 2.50 therefore sits **5 % above the
false-alarm boundary**, where before this capture the worst case was
`233615` at 1.50 and the margin was 67 %.

The cost of raising it was measured over the eight RADE captures, on the
fixed correlator, as locked fraction and mean pilot SNR:

| `use_ratio` | 2.25 | 2.50 | 3.00 | 3.50 | 4.00 |
|---|---|---|---|---|---|
| `232842` | 94 % | 94 % | 94 % | 94 % | **88 %** |
| `110923` | 66 % | 65 % | 64 % | 64 % | 64 % |
| `111051` | 72 % | 68 % | 68 % | 68 % | 68 % |
| `111734` | 70 % | 70 % | 70 % | 70 % | 66 % |
| `213155` | 93 % | 93 % | 93 % | 91 % | 91 % |
| `233133` | 57 % | 55 % | 52 % | 51 % | 53 % |
| `233241` | 56 % | 56 % | 60 % | 60 % | 60 % |
| `202743` | 70 % | 73 % | 72 % | 71 % | **54 %** |

Nothing measurable happens between 2.25 and 3.50 - the same
scatter-not-trend the earlier sweep found - and the cliff is between 3.50
and 4.00, where the two weakest captures lose 6 and 19 points. `202743`
matters most here: pilot SNR -5.9 dB, quality 0.15, eight acquisitions in
a minute. It is the only genuinely marginal capture in the set, and 3.00
costs it one point.

**Assessment: a change to 3.00 is justified by the measurements, and is
not being made yet.** It would put the threshold 26 % above the false
alarm and 17 % below the cliff, roughly centred, at no measured cost. What
holds it back is that the case rests on one capture, at 724 kHz, in `SAM`,
in a configuration no operator would choose - the filter is symmetric, so
`div_rade_side_expected()` returns 0 and *both* pilot banks are searched,
which is the weakest setting the detector has. Against that, the whole
argument for the present value rests on a set with one marginal capture in
it. Two more dead-air captures - one more mediumwave, one on a quiet
amateur band - would settle it either way, and are a minute each.

**Conclusion: leave `RADE_USE_RATIO` at 2.50 for now**, and treat the
margin as the thing to watch rather than the value. There is no measured
benefit to lowering it on real signals, and a measured false-alarm cost at
2.25. This supersedes an earlier suggestion of 2.00 that was based
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
- **The flat scalar channel model is right on a one-mode path, and only
  there.** `h1/h0` measured per subcarrier varies by +/-0.3 to +/-0.63 dB
  in magnitude and +/-3 to +/-12 degrees in phase across 750-2200 Hz on
  the RADE captures, with differential delay under 6 us; `000332` and
  `115357` agree, at 1.0 to 1.7 dB of ripple, 7 to 12 degrees of residual
  and a differential coherence bandwidth wider than the whole passband.
  **`000747` breaks it**: 7 dB of ripple, 83 degrees of residual and a
  coherence bandwidth of 188 Hz, which is twenty independent frequency
  cells inside one filter (Finding 17). A single complex weight remains
  the right *architecture* - it is what `receiver.c` applies, and the
  estimator's answer is the correct one given that constraint - but on a
  multi-mode low-band path it is an approximation costing a measured
  2.2 dB, not a model. A voice passband cannot resolve the question either
  way (Finding 6).
- **The per-arm statistic gets the sign right.** Across thirteen captures
  it picks the antenna that decodes or measures better 11 to 12 times out
  of 13 on the wideband references, including the mediumwave pair where
  the better antenna is 14.5 dB *quieter* than the other (Findings 14 and
  16). Its accuracy is another matter - see Finding 13 on the guard bins.
- **Every reference holds correctly when there is no signal.** On all five
  no-signal captures RADE V1 reported locked 0.00, holding 1.00,
  coherence 0.003-0.008 (0.00 recorded and 0 acquisitions replayed on
  `111328`), and FSK/Digital never produced a weight. On the
  voice captures all three references hold through the gaps between overs.
  None of them invents an answer from noise, including with the 160 m
  interferer at full strength on ADC0.
- **Level says nothing about which antenna is better.** ADC1 has been
  measured 13 to 15 dB *louder* and better (Finding 16), 9.8 dB louder and
  exactly equal (`003309`), and 12.3 dB louder and 5.1 dB worse
  (`002534`). Only the ratio of signal to noise decides, which is what
  MVDR computes and what a listener cannot hear.
- **The available SNR does not depend on the branch gains; what the
  shipping Sum weight achieves does.** Scaling one arm rescales `h` and
  `R` together and the optimum weight rescales inversely, so the
  combination is unchanged - measured directly across 12 dB of step
  attenuator in Finding 22, where the ideal gain held at +1.6 to +1.9 dB
  at every setting, and from the other side in Finding 11, where a x10
  input scaling left the answer bit-identical. The Window and Carrier Sum
  weight is the exception, because it assumes the branch noise is equal
  (Finding 20).
- **Shortening the average helps Null; what it does to Sum depends on the
  estimate, not on the fading.** The null deepens across the whole
  0.2-30 s slider on every fast-path capture - 1.1 and 2.2 dB on `000332`
  and `000747` (Finding 18), and again on `002534`. Sum is different: it
  moves 0.33 and 0.57 dB on the two September captures, wants **30 s** on
  `002534` whose channel fades in half a second, and wants 0.2 s on
  `003309` whose channel is eight times slower. The averaging time is set
  by whichever quantity in the solve is hardest to measure - on `002534`
  that is FSK/Digital's occupancy split inside a voice passband
  (Finding 21).
- **The coherence gate declines the right blocks.** On `000747` at
  tau 0.2 s it holds 23 % of blocks, and an ideal weight reaches -3.93 dB
  on the ones it passed against -0.23 dB on the ones it held (Finding 18).
  Finding 10's limit still stands - the gate cannot separate a signal both
  antennas hear from noise both antennas hear - but on this path it is
  giving up nothing.
- **Reception is often close to anti-phase, but not always.**
  `arg(h1/h0)` measured -177, -161, -162, -105, -7 and -4 degrees across
  the 40/80 m overs, and +82, -36 and +24 on 60 m. It is just the path;
  nothing structural, and the 60 m set shows the whole circle is in use.

## What is still open

- **Two captures where the zero-weight guard fired have no decode score
  after the fix.** Finding 11 is fixed and the change is scored on eight
  captures for zero frames and weight jitter, but only six carry a decode
  column. `231724`, where the guard fired on 91 % of frames, and
  `232052`, where it fired on 49 %, are not among them - `232052` has
  5.8 s of modem and `231724` runs the FSK/Digital reference, so both
  want a deliberate re-score rather than a rerun of the standard harness.
  Neither is expected to move much; the point is that "RADE V1 beats the
  better antenna" rests on six.
- **The alias resolver has never been watched acquiring cold on air.**
  Finding 15 is fixed and scored on replay, where the resolver's 4 s of
  averaging is invisible because every capture was armed on a signal that
  was already there. What has not been seen is a station appearing while
  the radio is listening, so the operator's view of it - a lock that
  settles, then moves 8 Hz a few seconds later, with the menu's frequency
  readout following - can be checked against what the loop should do.
- **`202743` moved and cannot be checked.** It is the one capture where
  the resolver's step is not corroborated by a quality improvement: mean
  pilot SNR went from -5.92 to -5.85 dB, which is nothing. On a capture
  that acquires eight times in a minute at quality 0.15, the step is
  plausible and unproven. A marginal capture that stays locked would
  settle it.
- **Threshold policy needs more dead air, and `112151` narrowed the
  margin to 5 %.** The boundary is at 2.375 against a shipping 2.50 -
  see the assessment under "False alarms", which recommends 3.00 and
  explains why it has not been taken. Two more quiet captures decide it.
  Dead-air captures are cheap and need no station; ones from outside the
  amateur bands are cheaper still.
- **A null is limited by the block period, and nothing on the menu says
  so.** Finding 31 measures an FSK interferer at inter-arm coherence 0.996
  - a 20.8 dB null by that alone - reaching 13.7 dB for a weight applied
  one block late and 11.0 dB for the shipping loop. Seven of the nine
  decibels lost are the 171 ms between measuring the channel and applying
  the weight. The Resolution control sets that period and the operator has
  no way to know it is the binding constraint; a finer setting is the only
  thing that could take the null deeper, at the cost of a noisier per-block
  estimate, and it has not been tried. That sweep is one capture and an
  afternoon.
- **The window and averaging questions are untested where they matter.**
  Finding 30 measures them on DRM at 40 dB SNR and finds every setting
  within 0.04 dB, which is the right answer there and says nothing about a
  signal near its decoding threshold - where a narrow window may not have
  enough to work from and a wide one may span more channel variation than
  a scalar can follow. A weak DRM capture, or any wideband digital signal
  a few dB above threshold, is what would separate them.
- **No capture yet has a wanted *modem* signal and strong common-mode
  noise.** `111852` closed half of it - a wanted signal at inter-arm
  coherence 0.982, where the nuller reaches its ceiling (Finding 16) - and
  `142026` closes more: a DRM broadcast 41 dB over a floor whose *noise* is
  0.74 correlated between the arms, measured in four separate guard
  regions, where a scalar weight nulls 8 dB and a per-bin weight 16
  (Finding 28). Both are AM, so neither says anything about the
  **pilot-domain** covariance. A RADE station on a path with obvious
  common-mode noise is still the missing capture.

- **Analog voice has been measured on one band, one path, two usable
  captures.** The +1.6 to +1.8 dB is worth confirming elsewhere, and the
  gap between the on-air and replayed rows in Finding 6 is not understood.
  A voice capture several averaging times long, armed before the loop has
  converged, would settle it.
- **No capture yet is marginal.** Every over here decoded at 99 %+ on
  either antenna alone. The case where diversity actually matters is the
  one that is hard to catch and easy not to bother recording.

- **The 20 Hz retune tolerance is measured at its lower end only.**
  Finding 19 closes the original item: on `115357` an 18 Hz walk moved
  `h1/h0` by 2.10 dB where ordinary fading moved it 7.10 dB over the same
  nine seconds, so holding the estimate was right and the exact comparison
  would have thrown it away nineteen times. What is not settled is where
  the tolerance *should* stop. 18 Hz is inside 20 by two hertz, so the
  capture says the number is not too large and says nothing about whether
  it is too small. A walk of several hundred hertz on a steady signal
  would give the other end of the curve.

- **The default averaging time may be wrong for a fast path, and one
  band is not enough to move it.** Finding 18 measures the whole
  0.2-30 s slider on two captures and finds Null monotonically better as
  the average shortens - 2.22 dB on `000332`, 1.11 dB on `000747` -
  against a shipping `div_auto_tau` of 2.0 s. That is not sufficient to
  change a default. Both captures are on one band, on one path, on one
  afternoon; every other capture in this document was taken on a path that
  barely moves, where a short average mostly adds variance; and Finding 6
  measured coherence weighting earning its place chiefly by keeping the
  *gate* open, which a short average could undo. What would settle it is
  the same `run_ref --tau` sweep across the existing capture set, so that
  the cost on a slow path is measured rather than assumed. It is an
  afternoon's work with the tools already committed.
- **Per-bin combining is the only route to the remaining 2.2 dB, and
  nothing has been designed.** Finding 17 measures a scalar weight at
  -3.06 dB and an independent weight per 93.75 Hz bin at -5.23 dB on
  `000747`, out of sample. Collecting that means a weight per bin or an
  FIR in `receiver.c` where there is now one complex multiply, twenty
  times the estimation on a channel that decorrelates in a second, and a
  CPU budget nobody has looked at. The measurement says the headroom is
  real; it says nothing about whether it is affordable. A second
  frequency-selective capture would say whether 2.2 dB is typical or is
  one path.
- **The threshold defaults are reproductions, not choices.** Finding 26
  measures the optimum for Window at 0.34 against the shipped 0.30 and for
  FSK/Digital's band gate at 0.48 against 0.30, and keeps 0.30 in both
  because the first difference is inside the measurement and the second
  belongs with `DIV_OCC_COH`, which is the false-alarm lever in that mode
  and has not been swept. Sweeping it against the five no-signal captures
  is the next piece of work, and it is what Finding 8's open item has been
  waiting for.
- **The Sum weight now carries the branch noise ratio; what is left is
  the estimator behind it.** The term itself is in (see "What was
  changed"), and it recovers most of the 3.6 to 5.0 dB Findings 20 and 22
  measured. Two limits are known and neither is closed. On a signal that
  never stops the windowed minimum sits on faded signal rather than noise
  - `000332` reads +1.3 dB against a true +6.3 dB - and the 6 dB
  clearance test cannot tell a 14 dB fade from a gap. On `003309` the
  correct magnitude scores 1.8 dB *worse* than the old wrong one, because
  the phase it multiplies is an average over two dozen stations that want
  different phases (Finding 23). Both want more captures rather than more
  tuning: six variants of the estimator were tried against seventeen
  scored points and the mean moved between -0.20 and +1.97 dB, which is a
  set too small to choose on.
- **The combined output has no gain normalisation.** `receiver.c` pins
  arm 0 at unity, so the array's gain to the wanted signal,
  `g = 1 + w * h1/h0`, lands directly on the audio: measured median
  +0.21, +1.50 and +2.38 dB with the *ideal* weight on the three
  September captures, and +14.8 dB with the shipping one on `002534`
  (Finding 22). Dividing the output by `g` would hold the signal at its
  arm-0 level and let the noise floor fall by the SNR gain instead, which
  is both what an operator expects and what makes an A/B against
  diversity-off meaningful. It is one complex divide per block by a
  quantity the solve already has. What is not known is how it interacts
  with the slew, with Hold, and with the AGC - a weight that is holding
  while the channel moves would have a stale `g` - so it wants measuring
  before it is written.
- **The attenuation budget is measured over 4 dB and open beyond it.**
  Finding 28 has the first capture with the settings recorded, and both
  arms track the attenuator one for one to within 0.03 dB over 4 dB, which
  puts the receiver's own noise more than 21 dB below the band's on 15 m
  at midday. `002710` adds that 12 dB cost 0.25 dB. What is still missing
  is the far end: a sweep to 20 or 30 dB in known steps, which would show
  where the curve actually bends, and the radio's own ADC overload
  indication alongside it, because the tap is downstream of the DDC and
  cannot see headroom at the converter. Neither costs more than a minute.
- **The Carrier reference's coherence gate has no discriminating power.**
  Measured over thirty-two captures it clears 0.30 on 34.7 % of blocks
  that hold a signal and 36.0 % of blocks that hold none, and no threshold
  separates them (Finding 26). Five bins put `γ̂²`'s own `1/N` bias where
  the threshold is. The fix is more bins or a longer average -
  `DIV_CARRIER_BINS`, or letting the carrier tracker's own smoothing feed
  the gate - and neither has been tried. Until then the mode is usable
  because the *estimate* is fine on a real carrier; it is the gate that
  cannot tell whether there is one.
- **The weighting and threshold pair is measured; the change is not made.**
  Finding 29 ran the sweep the previous version of this item asked for,
  and it says **flat at 0.20 dominates the shipping coherence at 0.30** -
  slightly fewer false alarms and 0.18 dB more signal, with flat ahead or
  level on seven captures of seven. What holds the change back is that
  0.18 dB is a small margin on one reference and a passband metric. The
  measurement that would settle it is the same sweep scored **against
  librade on the RADE captures**, where the yardstick is synced frames;
  that is the document's most trusted metric and has never been pointed at
  the weighting question.
- **FSK/Digital occupancy has no false-alarm control.** On `231532`, with
  no signal anywhere, the mode produces a weight on 30 % of blocks,
  through the normal path. Three bins clearing a 6 dB-over-median
  threshold is not evidence when the region holds two hundred of them, and
  `DIV_OCC_MIN_BINS` does not scale with region width. What is wanted is
  a threshold whose false-alarm rate is known: dead-air captures with
  FSK/Digital selected at several filter widths would give it directly.
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
- **The attenuator experiment cannot be replayed on the capture that
  contains it.** The capture writer now records `att0` and `att1` (format
  version 2) and `run_ref` follows them, so the reset an attenuator change
  causes is reproducible from here on. `002710` was taken before that and
  is a v1 file: its two steps are still only visible as a step in arm 1's
  own noise floor, and a replay of it runs both attenuators at zero
  throughout. Anything in Finding 22 that depends on the resets - the
  tau 10.4 and 30 s rows in particular - wants a fresh capture of the same
  experiment to be checked against.
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
RADE V1 and 5 of 10 from FSK/Digital, and decode-scores 1.3 dB behind Sum
on average. It is a fallback, not a default.

### `src/rade_correlator.c` — the frequency alias is resolved

Finding 15. `rade_correlate_split()` returns the pilot correlation and
the two half-length correlations it is made of, at no extra cost; the
phase between the halves is accumulated coherently at `RADE_ALIAS_ALPHA`
and, once `RADE_ALIAS_MIN` frames have gone into it, read as a residual
that is unambiguous over +/-50 Hz. If it exceeds half a frame rate by
`RADE_ALIAS_MARGIN`, `lock_f` moves a whole number of frame rates and the
frame-rate discriminator is re-armed. Nothing else is reset: the channel
estimate is insensitive to the move, measured at 0.05 dB and 0.7 degrees.

Where the loop settled, over the settled part of a cold replay:

| capture | before | after | step |
|---|---|---|---|
| `110923` | +5.80 Hz | **-2.22 Hz** | -8.02 |
| `111051` | +6.08 Hz | **-2.16 Hz** | -8.25 |
| `111734` | +6.17 Hz | **-2.16 Hz** | -8.33 |
| `213155` | -17.49 Hz | **-9.21 Hz** | +8.29 |
| `233133` | -5.93 Hz | **+2.32 Hz** | +8.25 |
| `202743` | +36.95 Hz | +45.22 Hz | +8.27 |
| `232842` | +7.77 Hz | +7.77 Hz | none needed |
| `233241` | +2.23 Hz | +2.23 Hz | none needed |

**Six of eight.** The steps are 8.02 to 8.33 Hz, which is the frame rate
to within what the loop's own tracking then absorbs.

What it bought, over the whole replay:

| capture | mean pilot SNR | mean quality | decode, vs the better arm |
|---|---|---|---|
| `110923` | 6.43 -> **7.85 dB** | 0.813 -> 0.856 | +1.7 -> **+1.9 dB** |
| `111734` | 6.80 -> **7.47 dB** | 0.819 -> 0.842 | +1.8 -> +1.7 dB |
| `213155` | 2.50 -> **3.18 dB** | 0.636 -> 0.669 | +0.5 -> **+0.8 dB** |
| `111051` | 1.71 -> 1.75 dB | 0.596 -> 0.598 | +1.2 -> +1.2 dB |
| `233133` | 1.40 -> 1.43 dB | 0.585 -> 0.587 | +0.6 -> +0.6 dB |
| `202743` | -5.92 -> -5.85 dB | 0.230 -> 0.232 | -2.3 -> -2.3 dB |
| `232842`, `233241` | unchanged | unchanged | unchanged |

Lock uptime, time to first lock and acquisition count are unchanged on
every capture, and **no synced frame changes anywhere**. The decode
column moves by 0.1 to 0.3 dB and no further, exactly as Finding 15 said
it would: this is a fix for the health of the lock, not for the audio.
`111734`'s 0.1 dB the wrong way is inside the scatter of a measurement
whose synced frame count did not move.

Convergence was checked by forcing `lock_f` at acquisition. `232842`
returns to +7.77 Hz from nine starting points spanning -20 to +30 Hz;
`213155` returns to -9.24 Hz from six spanning -18 to +22.

Both constants were swept. `alias_min` from 8 to 96 frames moves mean
pilot SNR monotonically - 0.1 dB between 8 and 32, 0.4 dB between 32 and
96 - because a shorter average acts sooner; **32 is kept**, matching the
EWMA's own time constant, on the grounds that a wrong step is worse than
a slow one and 0.1 dB is not worth the noise. `alias_margin` from 0 to
3.0 Hz changes nothing at all on any capture: the measured residuals are
either well inside half a step or well outside it, so 1.5 Hz is a
guardrail rather than a tuned value. At 4.0 it starts to suppress real
steps.

All seven no-signal captures still produce **zero acquisitions** with the
resolver in.

The one visible cost is settling time. `test_rade`'s station-handover
case now needs 13.7 s after a re-lock rather than 6.8 s to reach the new
station's weight, because the resolver wants `RADE_ALIAS_MIN` frames plus
a couple of averaging times. Measured at the old 6.8 s the weight is
mid-correction, at +4.87 dB against the +1.94 dB it settles to. That test
also says something the on-air captures cannot: with the loop on the
right alias, MVDR recovers `conj(h)` to 0.6 dB and 2.7 degrees on the
synthetic channel, where before the fix it was 4.0 dB and 17 degrees out.

### `src/diversity_auto.c` — the Sum weight carries the branch noise ratio

Findings 20 and 22. The Window and Carrier references formed Sum as
`acc_xy/acc_xx`, which is `conj(h1/h0)` and nothing else - maximum ratio
combining under the assumption that the two branches carry equal noise.
They now multiply it by `N0/N1`, which is what makes it maximum ratio
combining.

The ratio is the hard part, because these two references have no noise
bins to measure one in. `div_arm_nratio_update()` takes it by minimum
statistics over a bounded window: a 0.5 s smoothing of the unweighted
window power on each arm, the minimum of the pair over the current
`DIV_NRATIO_WIN` = 5 s slot and the one before it, published only when the
smoothed power stands `DIV_ARM_MIN_DB` above that minimum on both arms.
Both arms are taken from the same slot so the pair is contemporaneous. On
a signal with no gaps the clearance test never passes, no ratio is
published, and the weight is exactly what it was before.

Measured against the guard-region truth, offline, on the captures with
real gaps in them:

| | estimator | measured truth |
|---|---|---|
| `002534` | **-11.90 dB** | -12.26 dB |
| `003309` | **-9.35 dB** | -9.83 dB |
| `000332` (no gaps, 14.6 dB of fading) | +1.33 dB | +6.31 dB |

Scored through `run_ref` at five averaging times on four captures,
against the better antenna, with the split guard of Finding 18:

| capture | tau | before | after | ideal |
|---|---|---|---|---|
| `002534` voice | 30 s | -3.49 | **-1.02** | +1.37 |
| `002534` voice | 3.4 s | -3.50 | **-1.27** | +1.37 |
| `002534` voice | 0.2 s | -3.74 | -2.98 | +1.37 |
| `002710` CW | 0.2 s | -1.78 | **+0.59** | +1.60 |
| `002710` CW | 10.4 s | -2.04 | **-0.17** | +1.60 |
| `003309` FT8 | 0.2 s | +0.75 | **-1.05** | +2.04 |
| `000332` | 10.4 s | -0.88 | -0.75 | +1.56 |
| `000332` | 0.2 s | -0.55 | **-1.15** | +1.56 |

Seventeen points in all: **twelve improve, five do not, and the mean is
+0.49 dB.** The gains are where the change exists to help - `002534` and
`002710`, whose arms are 12 to 13 dB apart on noise - at +0.8 to +2.5 dB,
and the audio level comes down with them, from +14.8 dB over arm 0 to
+6.4 dB on `002534`.

Three things this does **not** do, all of them measured rather than
supposed.

It does not reach the ideal weight. On `002534` it lands 2.4 dB short:
the estimator reads -11.9 dB where the truth is -12.3, which is close, but
the loop's channel estimate and the coherence gate account for the rest.
The mode went from 3.6 dB *below* the better antenna to 1.0 dB below it,
which is most of the defect and not all of it.

It regresses on `003309`, by 1.6 to 1.9 dB at every averaging time, and
the estimator is not at fault there: it reads -9.35 dB against a truth of
-9.83. What happens is Finding 23. That passband holds two dozen stations
whose `arg(h1/h0)` covers the whole circle, so the loop's *phase* is an
average over signals that want different phases; a large `|w|` approximates
"use arm 1 alone" and is safe, while the correct magnitude with a
meaningless phase partially cancels. The old behaviour scored well there
by accident. This is the cost of being right about the magnitude on a band
where a scalar weight cannot be right about anything else, and it is an
argument for narrowing the window on such a band, not against the noise
ratio.

And it under-corrects on a continuous signal. `000332` never stops, so the
windowed minimum sits on faded signal rather than noise and reads +1.3 dB
against a true +6.3 dB. The clearance test passes because the fading is
14.6 dB deep, which is more than the 6 dB the test asks for. Cost: 0.6 dB
at tau 0.2 s, 0.1 dB gained at 10.4 s. A test that separated a deep fade
from a gap would fix it and is not obvious; requiring two consecutive
windows to agree within 3 dB was tried, and it refused the CW capture as
well and gave up 1.5 dB there.

### `src/diversity_auto.c` — one fault the fix turned up

The floor tracker's minimum can be the startup transient rather than the
band. `arm_fast0`/`arm_fast1` begin at zero, so the first blocks after a
reset are quieter than anything that follows, and a minimum seeded from
one of them sits far below the signal for as long as
`DIV_FLOOR_RISE_DB` - a fifth of a decibel a second - takes to climb out.
On a signal with gaps the gaps pull it back and nothing shows. On one
without, the clearance test passes anyway and a ratio of two *signals* is
read as a ratio of two noises: on the synthetic continuous carrier in
`test_window` that inverted the Sum weight outright, +2.09 dB where
-2.11 dB is right.

`div_arm_nratio_update()` therefore keeps its own smoother, seeded from
its first block. The shared `arm_floor0`/`arm_floor1` are left exactly as
they were, because the per-arm figure of Finding 14 is measured on them
and seeding them is *worse* - it sets the floor to the first block's power,
signal included, and on a signal with no gaps nothing ever pulls it down
again. That was tried and cost 0.3 to 2.0 dB on every capture.

### `test/diversity/test_window.c` — the CW passband, in both CW modes

Finding 8's second correction. A CW filter does not land at the plain
inversion of `filter_low..filter_high`: `rx_set_filter()` folds the
sidetone in, `div_shift_to_bin()` takes it back out, and the window lands
symmetrically about the zero beat in CWL and CWU alike. The code was
always right and the document was wrong for two findings.

`test_cw_follow` pins it with the window following the filter, which is
what an operator actually runs and what the old `test_cw_zero` did not
cover. Four cases: a note 100 Hz off the dial, which is inside the true
window and outside the plain-inversion one, must be **tracked** in CWL and
in CWU; a note at +900 Hz, which is inside the plain-inversion window and
outside the true one, must be **ignored** in both. A regression that drops
the sidetone from `div_shift_to_bin()` fails all four.

### `src/diversity_capture.h` — the capture records both attenuators

A devtool change, and the one open item it closes. `div_context_changed()`
compares `att0` and `att1`, so moving either resets the statistics; the
capture recorded neither, so on `002710` - where the operator stepped ADC1
twice while recording - the settings had to be inferred from arm 1's own
noise floor and a replay could not reproduce the resets.

`att0` and `att1` occupy what was `pad0` plus the padding the compiler was
already inserting before `centre`, so the block record is the same 208
bytes and **a v1 capture still replays**; `DIVCAP_VERSION` goes to 2 and
the tools say once that a v1 file's attenuators are unknown rather than
letting two zeros be read as two settings. `run_ref` follows them block by
block, `replay_rade` prints them, and `test_capture` round-trips them.

### `src/diversity_menu.c` — the Averaging slider is geometric

Findings 18 and 21 both turn on settings below a second, and on a linear
0.2-30 s scale everything below five seconds sits in the first sixth of
the travel. The widget now carries a position and converts: equal ratio
per pixel, which is the natural spacing for a time constant, putting
**0.2 to 5 s in 64 % of the travel**. A thousand steps over a 150:1 range
is half a percent a step, so a round trip through the widget moves the
value by less than a quarter of a percent - the default 2.0 s comes back
as 2.0046. Nothing else changes: `div_auto_tau` is still seconds
everywhere, including over the client/server link and in the properties
file.

### `src/diversity_auto.c`, `src/diversity_menu.c` — the coherence threshold is per reference

Finding 26. One slider was compared against four different quantities and
against nothing at all in RADE V1. It is now stored per reference, beside
the per-reference window pairs it sits next to:

- `div_band_cohmin`, `div_carrier_cohmin`, `div_digital_cohmin` and
  `div_rade_cohmin`, mirrored into `DIV_SETTINGS`, saved and restored by
  the modal blocks and by the flat property keys;
- `diversity_auto_ref_store()` / `diversity_auto_ref_recall()` move the
  live value with the reference. The window pair was swapped in the menu;
  both now swap in the engine, so a client, a modal block and a properties
  restore all get it and not only the one path the menu takes;
- `div_settings_load()` and the properties restore end by taking the live
  threshold from the selected reference's slot. Without that a radio
  starting up in RADE V1 would have gated on whatever the previous
  reference was set to - 0.30, against a mode that had no gate at all;
- **RADE V1 is gated**, on `rade_corr_quality`, defaulting to zero, which
  is the behaviour it replaces exactly;
- FSK/Digital's per-bin occupancy test is now `DIV_OCC_COH`, its own
  constant, so that moving the gate no longer changes which bins the
  estimate is made from;
- the menu row is shown in every reference, relabelled **Min quality (%)**
  in RADE V1, and each reference's tooltip says what its number is
  measured over.

Defaults are unchanged, and the sweep in Finding 26 is why: for Window
0.30 gives 5.7 % false alarms against an optimum of 0.34 at 5.0 %, which
is the same point to within the measurement. Nothing the radio does moves.

An operator's existing `.props` upgrades silently. `GetProp` leaves an
absent key alone, so the three wideband references inherit the single
`diversity_auto_coherence_min` the file carries, which is the behaviour it
was written under; `div_rade_cohmin` is deliberately left out of that
migration, because zero is what "no gate" means. `test_modal` checks both.

What the sweep also found and this does **not** fix: the Carrier
reference's gate has no discriminating power at any threshold, because it
averages five bins and `γ̂²` sits near `1/N` on noise. That wants
`DIV_CARRIER_BINS` or a longer average, and is left alone.

### What was thrown away

The FSK/Digital occupancy guard written for Finding 8. It moved the score
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

`RADE_ALIAS_ALPHA`, `RADE_ALIAS_MIN` and `RADE_ALIAS_MARGIN` are in the
tunable manifest, so the sweeps under "What was changed" are
`replay_rade --sweep alias_min=8:96:8` and the like, with
`--set alias_margin=1e9` turning the resolver off for a before-and-after.

`replay_rade` drives the correlator directly and sweeps its constants;
`run_ref` drives the whole engine so the FSK/Digital solve can be run over
a recording; `score_rade` decodes.

Finding 29's grid is `run_ref --ref band --mode sum --weighting flat|coherence
--cohmin T`, one run per cell. The false-alarm column needs no runs of its
own: the coherence gate sits downstream of the accumulators, so the
statistic it compares does not depend on the threshold, and a single run
per weighting gives the whole curve by counting its `quality` column
against each candidate. That was not true before the FSK/Digital per-bin
test was split out into `DIV_OCC_COH` - until then, moving the gate moved
which bins the estimate was made from, and every point needed its own run. See
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
  the update, the discriminator's `df`, the `nudged` flag, and the
  correlation magnitude at `lock_f` and at `lock_f +/- 8.333 Hz` - that
  last is the neighbour-magnitude table, and it is the measurement that
  killed the first remedy;
- a `double instr_force_f` applied to `lock_f` immediately after
  acquisition sets it, which is what the equilibrium tables in Finding 15
  sweep and what the convergence check under "What was changed" starts
  from - it is one assignment and it changes nothing else about the run;
- a dump of the acquisition statistic `sf` for every (bank, frequency)
  cell as it is computed, which is the acquisition surface table.

Driven by `divcap_replay()` with default options it reproduces the
shipping path exactly; `--weights` writes the per-block weight in
`replay_rade`'s format, which is what `score_rade --weights fixed=...`
takes for the "unguarded" column. `src/` is not touched, and the copy is
not worth committing - two edits against a generated file are quicker to
redo than to maintain.

The independent channel and noise figures in Findings 13, 16, 17, 19 and
20 need no tools at all: read the blocks out of the `.divc` with the
layout in `src/diversity_capture.h`, FFT each arm, and take the
cross-spectrum over the modem band for `h1/h0` and over the guard bins for
`R`. Being a separate implementation is the whole point of them.

Findings 21 to 23 use the same arrangement as 17, 18 and 20, with the
guard regions moved to suit each capture: +1000..+3000 Hz fit and
+3500..+6000 Hz score on `002534`, -4000..-2000 and -7000..-5000 on
`002710`, +1000..+3000 and +4000..+6000 on `003309`. The CW capture is cut
at 8192 samples rather than 2048, because its window is 800 Hz wide and
93.75 Hz bins do not resolve it.

**The CW window is `-(filter + sidetone)`, not `-filter`.** Finding 8 got
this wrong and nothing caught it until a third CW capture was set up. Take it from
`div_shift_to_bin()` and `div_frame_off()` rather than from the inversion
rule, or check it the way it was checked here: average the spectrum over
the capture and see which candidate band stands above the band median.

Findings 17, 18 and 20 use one arrangement consistently, and repeating
them means repeating it. Each 32768-sample block is cut into sixteen
2048-sample sub-blocks, Hann windowed, giving 93.75 Hz bins and 10.7 ms of
time resolution; the passband is the operator's filter reflected about
zero, per the sideband rule. Two guard regions are taken, **+1000 to
+3000 Hz** and **+3500 to +6000 Hz** in the tapped frame, both clear of
the passband on these captures and both clear of the interferer `000332`
carries at +6.75 to +7.4 kHz. Anything that *fits* a weight sees only the
first; anything that *scores* one sees only the second. That split is not
fastidiousness - scoring on the fitting region gave `000332` a Sum figure
4.9 dB over the better antenna, which is Trap 1 and is impossible.

**`run_ref` is not bit-deterministic on every capture.** It paces the
worker thread with `g_usleep()`, so a capture heavy enough per block can
have the queue drain differently between runs. Two runs of the *same*
binary over `002710` - nfft 65536, the largest transform in the set -
produce weight series differing by up to 0.12, which is larger than most
of the differences this document attributes to code changes elsewhere.
`002534` and `003309` repeat bit-identically. Before reading a small
difference on a heavy capture as a result, run it twice and see what the
instrument does on its own; the regression check for Finding 26 was made
useless by exactly this until it was.

The tau sweep in Finding 18 is `run_ref` as committed, one run per point:

```
./run_ref cap.divc --ref band --mode null --tau 0.2 --out w.csv
```

with the weight series applied to the capture one block after the samples
that produced it - `run_ref` writes the weight the engine had *after*
block n, and the radio applies it to block n+1. The "ideal" columns are
the best causal two-branch weight from the same EWMA at the same lag, so
the estimator is compared with its own ceiling rather than with a
different objective.

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
