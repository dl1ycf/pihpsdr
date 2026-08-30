# RADE diversity: passband window and V1 pilot correlator

Detail on the two FreeDV RADE reference modes. For how diversity and the
automatic loop work in general, see [`diversity.md`](diversity.md). For
what this mode measurably does on recorded on-air signals - which is not
what the synthetic tests suggested - see
[`diversity-measurements.md`](diversity-measurements.md).

The pilot-correlating RADE V1 reference, selectable from the Diversity
menu's "Measure on" list. (A wideband RADE passband reference used to sit
alongside it and has been retired - see "Stage 1" below.)

Selecting it sets the objective to **Sum (max SNR)** rather than the usual
Null default: on RADE the signal we are pointing at *is* the wanted one,
so the job is to maximise its SNR, not to cancel the strongest correlated
thing in the window. The operator can still override it, and **Null** then
turns the correlator's answer through 180 degrees to cancel the RADE
station rather than combine for it - which is the quickest way to check
that the array really is pointed at it. That objective used to be ignored
here: the correlator's answer was applied whatever was selected, so the
Invert button changed the audio for one block and then slewed straight
back.

## RADE waveform, for reference

From the radae sources (`rade_dsp.h`, `rade_ofdm.c`):

```
Fs   8000 Hz    modem sample rate
Nc   30         OFDM carriers, 750 .. 2200 Hz (centred on 1500)
M    160        samples per symbol
Ncp  32         cyclic prefix
Ns   4          data symbols per modem frame
Nmf  960        samples per modem frame = 120 ms
```

Frame layout is one pilot symbol then four data symbols, so a known pilot
recurs 8.33 times a second. The symbol is 192 samples including its cyclic
prefix; the correlator's template is the 160-sample symbol itself, which
is where the "20 ms correlation window" below comes from. The pilot symbols are
Barker-13 over the carriers scaled by sqrt(2), IDFT'd to the time domain.

## Sideband

RADE arrives through an SSB passband, so the modem occupies 750-2200 Hz on
one side of the tuned carrier and the mirror image of that on the other.

**The operator's passband decides which side, and it is the only side
looked at.** The midpoint of `filter_low`/`filter_high` names it, which
covers LSB, USB and the digital modes without a mode table. Only when the
passband straddles the carrier and so says nothing — AM, SAM, FM — is
there any measurement involved.

It took three goes to get there, and the reasons are worth keeping:

### The passband decides the sideband, and nothing else

Bank 0 is the pilot as transmitted, carriers at +750..+2200 Hz in the
tapped buffer; bank 1 is its mirror. **Bank 0 is the LSB bank.**

That is measured, not derived. The tapped buffer is spectrally inverted
with respect to RF — see "Frequency bookkeeping" in `docs/diversity.md`
— so an LSB signal, already inverted once by the transmitter, arrives at
the correlator the right way up and correlates against the pilot as
transmitted. On air, in LSB, on a weak signal:

```
rade_acquire: acq normal=7.97 mirrored=4.75
```

This is the same thing an operator would say without any of the above:
the mirroring an SSB transmitter applies is undone by the time the signal
reaches the point where we correlate.

**The operator's passband names the bank, and it is the only one
searched.** The midpoint of `filter_low..filter_high` decides, which
covers LSB, USB and the digital modes without a mode table. Only when the
passband straddles zero and so says nothing — AM, SAM, FM — are both
searched.

The reasoning is not just reliability. **A RADE lock outside the passband
is of no use.** This mode exists to extract and track coherence on the
signal the operator has tuned; a modem on the other side of the carrier is
a different signal, filtered out downstream, and steering the array at it
would be actively wrong. Not looking there is also cheaper — the search is
the most expensive thing in the module, and an SSB passband takes about a
third off it at the lower sample rates (at 384 kHz the decimator dominates
and it barely shows).

#### How it got here

Three arrangements, two of which looked like they worked.

1. **Derived from `vfo[].mode`, conjugating the input for LSB.** Never
   acquired on air. But the frame conversion had the wrong sign at the
   time, so nothing acquired under CTUN whichever sense was used — that
   experiment proved nothing about sidebands.
2. **Both banks searched blind, take the winner.** Acquired, and
   introduced its own fault: the choice between banks is decided by noise
   on a signal near the noise floor, which is where RADE lives. The
   wideband RADE window, placed by the same choice, settled above an LSB
   passband and stayed there; in V1 a lock could flip the green overlay
   across the carrier at the moment of locking.
3. **The passband names it, and nothing else is searched.** Which also
   made the on-air bank logs from stage 2 readable, and they are what
   fixed the mapping.

Conjugation stays out of the sample path throughout, so there is no weight
to conjugate back: whichever bank is used, `h0` and `h1` describe the real
untouched arms and the MVDR solution applies directly.

### The analysis window follows the passband too

In the wideband RADE mode the window is the modem band, `750..2200 Hz` on
the side the passband names, **intersected with the operator's filter**. A narrower
filter therefore narrows what is measured, which is the standing rule for
every window mode: never measure outside what the operator is listening
to. The panadapter overlay is clipped the same way, so what is drawn is
what is being measured.

The pilot correlator is different and is *not* clipped. It taps the raw
stream ahead of WDSP and needs all thirty carriers whatever the filter is
set to, so in RADE V1 the overlay shows the whole modem band.

The side in use is shown in the status line as `LSB` or `USB`.

**On air the first mode-based rule was backwards**, and it took a long
time to work out why. Against a real LSB signal the un-mirrored pilot bank
scored 12.8 / 14.7 / 15.9 while the mirrored bank scored 3.6 / 4.1 / 4.0.
That is a decisive result, and it says the tapped buffer is inverted with
respect to RF - an LSB signal, already inverted once by the transmitter,
arrives at the correlator the right way up. The mapping is now bank 0 for
LSB, and the reasoning is set out under "Frequency bookkeeping" in
[`diversity.md`](diversity.md).

## Stage 1: RADE passband — retired

The first RADE mode placed an FFT window on the nominal 750-2200 Hz modem
band, on the side of the tuned frequency the operator's passband implied,
clipped it to the filter, and did maximum ratio combining across it.

It has been removed. The **Digital I/Q** reference does the same job from
the same passband and does it better: it finds where the modem's energy
actually is rather than assuming the nominal band, and it measures the
noise on the empty part of the passband instead of assuming both branches
carry equal, uncorrelated noise. For a signal that lives near the noise
floor, which is the point of RADE, that second difference is the one that
counts. See §5 of [`diversity.md`](diversity.md).

Its `diversity_auto_ref` value is migrated to Digital I/Q on restore, so
a props file that selected it comes back on its successor.

What survives from it is the rule about which sideband to measure, which
RADE V1 still uses: the operator's passband decides, full stop. Taking
the stronger of the two sides by energy was tried first and is a coin
toss on a signal near the noise floor — with no signal present the window
settled wherever noise put it, and the overlay could sit above an LSB
passband indefinitely.

## Stage 2: RADE V1 pilot correlator (MVDR)

`src/rade_correlator.c`. Per arm: NCO shift to the tuned frame, then
polyphase FIR decimation to 8 kHz, then correlation against the known
pilot over a (bank, timing, frequency) grid. The samples are not otherwise
touched — the spectral sense is handled by which pilot bank is used.

Once locked, each frame gives a pilot correlation `d0`, `d1` per arm. The
channel is accumulated from those as the **cross-spectrum** `d1*conj(d0)`
and `|d0|^2`, and the interference covariance `Rnn` is measured in the
**off-carrier bins** of the same span. Then

```
w = conj( g1/g0 ),   g = Rnn^-1 h
```

with 1% diagonal loading. With `Rnn` diagonal and equal this reduces to
`conj(h1/h0)`, the maximum ratio answer, so it degenerates gracefully to
what stage 1 does when there is no correlated interference to null.

Both of those started out as something simpler and were changed after
being measured against recorded on-air captures, so the reasoning is worth
having in one place.

### Why the covariance is not the pilot-span residual

It was, originally: `n_i = s_i - h_i*p`, on the reasoning that removing
the wanted signal from the span leaves the interference. That reasoning is
wrong, and it was costing more than everything else in this file put
together.

One scalar `h` is fitted across the whole pilot symbol, so only that one
component comes out. Everything else inside the decimator's +/-3 kHz view
stays in the residual — and on real captures that is dominated by whatever
occupies the **rejected sideband**: a station of comparable power, 0.80 to
0.84 coherent between the arms, that WDSP's own filter throws away and the
operator never hears. Measured inter-arm coherence of the residual ran
0.61 to 0.80 against 0.11 to 0.49 for the true noise, on every capture, on
three bands, with the phase wrong as well.

MVDR then did exactly what it was told and steered a null onto it. That
null lands close to the wanted signal's own inter-arm phase, so the
combiner subtracted the signal it was there to combine.

The span is 160 samples at 8 kHz, so its DFT bins *are* the modem's own
50 Hz carrier grid: the 30 carriers are bins 15..44 and the bins either
side of them carry the noise and QRM the modem is sitting in, but no
modem. `Rnn` is measured directly in those, 300 Hz to 2850 Hz, on the
pilot bank's own side of the tuned frequency — which is what keeps the
rejected sideband out, by construction rather than by hoping.

### Why the channel is a cross-spectrum

`acc_h0` and `acc_h1` used to be coherent EWMAs of `h0` and `h1`. They are
averages of a phasor that is still turning: `rade_pilot_at()` rebuilds the
reference from `n = 0` every frame while the received pilot advances with
the sample index, so `d0` turns by `2*pi*f*T` from frame to frame whatever
`f` is.

At the operator's 10.5 s averaging the coherent part measured 16 to 28 dB
below the per-frame `|h0|`, with the phase dragged 36 to 51 degrees off —
and it got *worse* the longer Averaging was set, which is the opposite of
what that control promises.

`d1*conj(d0)` and `|d0|^2` are `h1*conj(h0)` and `|h0|^2` up to one common
real scale, which is all the solve needs, and both are invariant to a
rotation the two arms share. It is the same form `div_digital_solve()`
already uses in `bin_xy`/`bin_xx`.

### What the pair was worth

Decode-scored against librade over three 40 m captures, mean
`rade_snrdB_3k_est()` against the better antenna alone:

| capture | before | after |
|---|---|---|
| `213155` | -3.4 dB | **+0.5 dB** |
| `233133` | -0.5 dB | **+0.5 dB** |
| `233241` | -1.8 dB | **-0.0 dB** |

The mode went from losing to the better antenna on all three to matching
or beating it on all three. Detection is untouched: lock uptime,
acquisitions and time to first lock are the same to within their own
scatter. See [`diversity-measurements.md`](diversity-measurements.md).

Note that `rade_corr_snr` and `rade_corr_quality` read higher than they
used to, and should: a station in the rejected sideband is no longer being
counted as interference to the one being received.

### librade is not used

The proposal this came from assumed piHPSDR could query `librade` for
`h0` and `h1`. That API does not exist: `rade_api.h` exposes `rade_sync()`,
`rade_freq_offset()`, `rade_snrdB_3k_est()` and the tx/rx calls, and
nothing that returns a complex channel estimate. The pilot is ~40 lines of
deterministic setup, so it is reproduced here instead - no build
dependency, and nothing to break when librade's internals move.

### Detection statistic

The statistic is `(peak - mean) / sd` measured **down each frequency
column**, not peak-over-total-energy and not against a global floor.

Both refinements came from measurement, and both matter:

* Normalising by received energy collapses exactly when a strong
  interferer is present - the case the mode exists for. A first version
  locked happily on a clean signal and refused to lock at all once QRM
  was added.
* A narrowband interferer correlates with the pilot by nearly the same
  amount at every timing offset (shifting the window only rotates the
  phase) but varies strongly with frequency hypothesis, so it appears as a
  per-column pedestal. With a global floor, a CW carrier 36 dB over the
  pilot lifted the mean to 8.88 against a peak of 10.0. Taking mean and
  spread down each column removes it.
* Integrating more acquisition passes does **not** fix this on its own:
  the interferer's contribution is deterministic and repeats identically
  every pass, so it never averages down. Passes are still accumulated
  (32, about 4 s) because they do help against noise.

## Measured behaviour

Synthetic RADE signal, two antennas with different channel responses for
signal and interferer, CW interferer inside the modem band. Levels are
relative to pilot RMS.

| Interferer | Result |
|---|---|
| none | lock, weight matches `conj(h)` to ~1 degree, +4.1 dB signal |
| +0.3 dB | lock, **-32.8 dB** interferer, +3.8 dB signal |
| +6 dB | lock, **-31.3 dB** interferer, +3.9 dB signal |
| +10 dB | lock, **-36.8 dB** interferer, +3.8 dB signal |
| +20 dB and above | no lock |

Identical whichever way round the spectrum arrives; the correlator finds
the sense itself and the weight it produces applies to the untouched arms
either way.

## Tracking

Holding a lock is deliberately far more forgiving than getting one.

The first version gated every tracking frame on a fresh detection
statistic computed from one correlation and a handful of probes, against
acquisition's 32 integrated passes over the whole timing-by-frequency
grid. That is orders of magnitude noisier, and it threw away locks on
signals that had just acquired at three times the required margin.

Worse, the rejection path returned without advancing the pilot pointer.
The pilot moves on by exactly one modem frame every 120 ms whether or not
a given frame is liked, so a single marginal frame pinned `lock_a` while
the ring kept filling, and a second or two later the lock ended with
"pilot ran off the ring". On air that showed up as momentary locks that
never held.

The pilot pointer now always advances.

The hold criterion went through one more iteration. Remembering the
correlation magnitude at lock and watching for a drop below it looked
reasonable and was not: the reference ratcheted up to the highest level
ever seen, so under fading the ratio lived permanently below one and any
deep enough fade eventually crossed the threshold. On air that gave
50-second locks that always ended in a fade.

That is backwards for a diversity system - a fade is exactly when the
combining weight is worth the most, and losing the pilot for a few
seconds means keep going with the last good weight, not start again.

The criterion is now the pilot correlation against the correlation floor
measured off-pilot in the same frame. That is a ratio, so it does not
depend on signal level and cannot ratchet. A working lock reads about 6 on
this reference, so there is real margin; a ratio of 1 would mean the pilot
correlates no better than anything else in the frame does. How long the
ratio has to stay down before the lock is given up is the **Hang** control
- see below.

Fixing this also improved the synthetic interferer result from -26.5 dB
to -36.8 dB, simply because tracking now runs on every frame instead of
stalling after the first marginal one.

## Averaging

The time constant for the channel and covariance estimates comes from the
**Averaging** control in the Diversity menu. It applies to both the
wideband paths and the RADE V1 correlator, converted to a per-modem-frame
forgetting factor for the latter.

It was fixed at about 1.5 s to begin with, and that is far too short. At
the pilot SNR a real signal delivers - swinging between roughly -10 and
+3 dB frame to frame - the weight swings with it, and the movement itself
degrades recovery.

For a long time lengthening it also made the channel estimate *worse*,
because the accumulators were coherent averages of a still-rotating
phasor. That is fixed - see "Why the channel is a cross-spectrum" above -
so the control now does what it says in both directions. The measurements
below predate the fix and were taken on a synthetic signal with no
residual frequency offset, where the defect does not show; they still
describe the jitter-against-lag trade the control is for.

Measured on a synthetic signal at that sort of pilot SNR, steady-state
weight jitter against averaging time:

| Averaging | Weight jitter (rms) |
|---|---|
| 1.5 s | 0.0309 |
| 3 s | 0.0172 |
| 6 s | 0.0106 |
| 10 s | 0.0111 |
| 20 s | 0.0138 |

Three times less movement at 6 s than at 1.5 s. Past about 10 s it stops
helping, because the estimate starts lagging the path instead of just
smoothing the noise on it.

The right value is a judgement about how fast a given path is fading, so
it belongs to the operator rather than to a constant in the source. The
slider now runs to 30 s; several seconds is a sensible starting point for
RADE over HF.

Note this is separate from the lock-hold smoothing, which is fixed at
about 6 s and does a different job - deciding whether the pilot is still
there at all, rather than tracking the channel.

### What was verified, and one thing that was not

The control does reach the correlator and is converted as described.
`div_auto_tau` is passed to `rade_corr_process()` and on to `rade_track()`,
which forms

    alpha = 1 - exp(-T / tau),   T = RADE_CORR_NMF / RADE_CORR_FS = 0.12 s

and that is the exact discrete equivalent of a first-order lag with time
constant `tau`, provided a frame really does arrive every `T`. It does:
counted over 400 blocks, 285 of them carried a correlator update, which
is 0.713 per block against the 0.711 the arithmetic gives (4096 samples,
decimated by 6, over 960 samples a frame). So the accumulators are given
the time constant the slider says.

The fixed constants nearby also read as documented: `RADE_MAG_ALPHA` 0.02
is 6.0 s at 8.33 frames a second, `RADE_USE_ALPHA` 0.12 is 1.0 s, and
`RADE_PROBATION` 8 frames is 0.96 s.

What is *not* the averaging time is how long the **weight** takes to
settle after the channel changes. Stepping the aux arm's channel through
90 degrees at constant magnitude - which leaves the residual covariance
alone, so only the channel estimate has to move - and timing the tracked
weight to 63% of its journey, over three seeds each:

| Averaging | Weight settled in | Ratio |
|---|---|---|
| 1.5 s | 5.1 s | 3.4 |
| 3 s | 11.1 s | 3.7 |
| 6 s | 22.0 s | 3.7 |
| 12 s | 41.1 s | 3.4 |

Consistently about three and a half times the setting, and not a simple
exponential either: the weight moves quickly for a block or two, swings
well past the target - magnitude dipping to around 0.05 against a target
of 0.78 - and then recovers slowly. That shape has not been run to
ground, and no mechanism should be inferred from these numbers beyond
what they say.

They do not contradict the sentence above them. `alpha` is the time
constant of the *estimates*, which is what that sentence claims and what
the frame-rate check confirms. But an operator reading "Averaging" will
reasonably expect it to be how long the weight takes to follow a change,
and on a large one it is several times longer than that. Worth knowing
when setting it, and worth measuring properly before anyone tunes
against it.

A caveat on the measurement: a 90-degree step is a violent thing to do to
a channel, far more than a fading path does between frames, and the
overshoot may well be particular to it. The jitter figures below are on a
static channel for the same reason - which is also why they fall all the
way to 20 s here, where the table above rises past 10 s. That rise is
attributed to the estimate lagging a fading path, and a bench channel
that does not fade cannot show it.

## Keeping the lock vs trusting the frame

These are two different decisions and treating them as one was a bug.

When the pilot went away the correlator kept the lock, which is right - a
fade should not cost the weight - but then carried on feeding the channel
estimate and the covariance from correlations that were pure noise. For up
to the full hold time the combining weight was being steered by nothing at
all. Reported from the air as "it's tracking even when there's no signal",
which was literally what it was doing.

There are now two gates on the same measurement:

* a **fast** one (about 1 s) that freezes the accumulators and the weight
  when the pilot is not there, holding the last good values;
* a **hang**, counted in consecutive frozen frames, that decides the
  signal has been gone long enough to give up and re-acquire.

The status log shows `FROZEN` while the weight is being held, and
transitions are logged.

### The reference matters

Both gates compare the pilot correlation against a reference taken at the
same timing but at frequencies far outside the lock range (+/-300 and
+/-600 Hz).

Probing off-pilot *in time* is the obvious choice and is a poor
discriminator: those positions land on data symbols carried on the same
subcarriers, and a random OFDM symbol correlates against the pilot nearly
as well as the pilot does. The ratio then sits close to one even on a
clean signal, and a threshold placed there chatters - the freeze gate
engaged and released several times a second, which meant it kept
un-freezing and updating on noise anyway.

A 20 ms correlation window has its first ambiguity null at 50 Hz, so
300 Hz away the true pilot contributes essentially nothing while noise and
interference contribute exactly as much as they do on frequency. With that
reference a clean lock reads 6.0 to 6.1 against a freeze threshold of 2.5,
which is a real margin.

Measured: weight drift while frozen on a signal that stops fell from
0.152 to 0.0046, a factor of 33, on a weight of magnitude 0.86. What
remains is the gate's engagement transient, not ongoing wander.

## Hang

`RADE V1 pilot` is the only reference that holds a *lock* - a timing, a
frequency and a pilot bank it keeps returning to. The other references
measure whatever is in the window this block and forget it over the
averaging time; there is nothing for them to give up. So the **Hang**
control appears only for this one.

It sets how long a lock survives after the pilot stops being detectable,
before the correlator gives up and searches again. The range is 1 to 30
seconds and the default is 10.

The trade is straightforward in one direction and less obvious in the
other. Long is what a single station on a fading path wants: a fade is
exactly when the combining weight is worth the most, and a lock given up
in a fade has to be bought back with a fresh acquisition on a signal that
is, at that moment, at its weakest.

Short is what a frequency several stations are taking turns on wants, and
that case is the reason the control exists. Each station arrives over its
own path and has its own best gain and phase; until the lock on the last
one is given up, the new one is being combined with the wrong weight, and
the wrong weight on a two-antenna combiner is not neutral - it can be
subtracting.

### What it replaced

A fixed ten seconds, counted off the **slow** ratio - the same
pilot-to-floor measure but smoothed with a six-second time constant. That
was too slow twice over. On a signal that simply stopped, the slow average
took about seven seconds to fall from the six a clean lock reads to the
two the test wanted, and only then did the ten start. Sixteen seconds.

Sixteen seconds is not what the ten was meant to mean, and on a
roundtable it is most of a short over: the combiner spent the beginning
of every over applying the previous station's weight.

The hang is now counted off the **fast** gate instead. That gate already
decides, about a second at a time, whether a frame is worth measuring,
and consecutive frames that are not are exactly what "the pilot is not
there" means. One good frame resets the count, so a fade that flickers
does not accumulate towards a drop - only a continuous absence does.

`RADE_HOLD_RATIO` and its counter are gone with it. The slow average
survives as the *reported* health of a lock, which is what a
level-independent ratio over several seconds is good for.

### Measured

Two stations on one frequency, the second half a modem frame away in
timing with a different channel, the first stopping dead as the second
starts:

| Hang | Lock dropped | Re-locked |
|---|---|---|
| 2 s | 3.6 s after the changeover | 2.0 s later |
| 10 s | 11.5 s after the changeover | 2.0 s later |

Eight seconds more hang cost 7.9 seconds more before the drop, so the
setting is what decides. The roughly 1.5 s on top of it in both rows is
the fast gate's own averaging, which has to run down before the count can
start.

One thing this measurement showed that a stopping signal does not. While
the fast gate is running down, the accumulators are still being fed - and
here what they are being fed is not the noise a signal that stopped
leaves behind, but *another strong station*, whose data symbols correlate
against the pilot well enough to move the weight. The weight takes a real
kick in that first second or so before the freeze engages, and after that
it is held to within 0.5 dB and 5 degrees for the whole of the hang.

The kick costs nothing that lasts, because the re-acquisition that
follows starts the channel estimate again from nothing rather than
averaging into it. Measured end to end, the weight after the changeover
lands within a quarter of the distance from the old station's answer to
what the new station gives when it is the only one there.

## False alarms

Acquisition was never tested against noise until it was accused of finding
pilots that were not there. It does not: over repeated runs of pure noise
with no signal present, the statistic reaches 3.0 to 4.6, and no run
produced a lock. The 10.8 to 15.9 seen on air are genuine detections.

The 32-pass threshold has since come down from 6.0 to **4.8**, which is
closer to that 4.6 ceiling than it looks: what the threshold produces is a
*candidate*, and probation then has to confirm it on a pilot-to-floor
ratio rather than on this statistic. Re-measured at 4.8 over two minutes
of pure noise: no false candidates and no false locks.

Be clear about what that bought, because it is not what the arithmetic
suggests. It does pass a weak signal through this gate - at the point
where a 32-pass score of 5.90 used to miss 6.0 by a tenth, a candidate is
now raised at the correct frequency. But across 7 SNRs x 3 seeds and 5
noise levels x 5 seeds on synthetic signals, 4.8 and 6.0 give **identical
outcomes**: everything that locks at one locks at the other, in the same
number of blocks. The extra candidates are turned down by probation, at
pilot/floor 1.83 against the 2.5 `RADE_USE_RATIO` requires.

So `RADE_USE_RATIO` is what currently sets the weak-signal floor, not the
acquisition sigma. It is also the constant holding the false-alarm line,
so it should not be moved without measuring what it lets through.

Both halves of that have since been measured against recorded captures,
and only the second survived. Varying `RADE_USE_RATIO` over 1.75 to 3.0
leaves lock uptime on real signals unchanged to within a few points, so it
is not the weak-signal floor of anything; but on recorded dead air it does
hold the false-alarm line, and below 2.00 that line breaks. The value
stays at 2.50. Note also that the "pure noise" runs above were synthetic:
real band noise is markedly harsher on this threshold. See
[`diversity-measurements.md`](diversity-measurements.md).

## Known limits

**Acquisition fails above roughly +15 dB interferer-to-pilot.** That is
the honest ceiling of single-pilot correlation against a strong in-band
carrier. It matters because the value of MVDR is greatest exactly where
acquisition is hardest - if the interferer were mild, stage 1 would do.

A worthwhile next step, and the reason both modes are exposed: run stage 1
first to get a partial null, then let the pilot correlator acquire on the
improved signal. Nothing in the current code does that automatically.

**A carrier landing exactly on a RADE subcarrier is the worst case** - it
is periodic over the modem frame, so it is indistinguishable from pilot
energy by any amount of frame-rate integration.

**The displayed "pilot" percentage reads low under strong QRM** even while
tracking perfectly. It is the share of the span energy the pilot itself
accounts for, which is genuinely small when something loud is sitting on
top of it. Watch the LOCK indicator, not that number.

It reads *higher* than it did before the covariance moved off the
pilot-span residual, because a station in the rejected sideband is no
longer counted against it. Only interference the modem is actually sitting
in depresses it now, which is the more useful reading of the two - but any
number written down from an older build is not comparable.

**Acquisition takes 1 to 5 s of continuous signal**, depending on how
strong it is.

It used to take 11.5 s for every signal however strong, because declaring
lock needed three consecutive evaluations, each integrating 32 passes of
one 120 ms modem frame: `3 x 32 x 120 ms`. Two things were wrong with
that. The integration length was fixed, so a signal 20 dB above the
threshold waited exactly as long as one at it; and the confirmation
re-ran the entire blind search, which is an enormously expensive way to
answer the question "is the thing we just found still there?".

What happens now:

1. **Progressive search.** The grid is scored at 8, 16 and 32 passes,
   with thresholds of 7.5, 6.75 and 4.8 sigma. The early thresholds are
   raised because scoring three times gives noise three chances; the last
   one is low because probation, not it, is what a candidate has to
   survive. A strong signal is found after 8 passes, just under
   a second.
2. **Cheap confirmation.** A detection is a *candidate*, not a lock. The
   tracker follows it for `RADE_PROBATION` (8) frames, about a second,
   applying the ordinary per-frame pilot-against-floor test at that one
   timing and frequency - one correlation and four probes per frame
   instead of another full search. **No weight is produced during
   probation**, so a false alarm costs a second of waiting and never
   moves the combiner. The status line shows "confirming candidate".

Measured on synthetic signals at 48 kHz, all four of USB/LSB with and
without CTUN acquire in **2.2 s** (`test/diversity/test_rade.c`), against
11.5 s before, with no false lock in 12.8 s of noise.

**Frequency is tracked once locked.** Acquisition leaves it quantised to
the 5 Hz search grid, and a station drifts. The pilot correlation turns by
`2*pi*df*T` from one modem frame to the next, `T` being 120 ms, so its
phase advance measures the residual, unambiguously over +/-4.17 Hz about
the tracked offset - which covers both the half-step quantisation and any
drift a station on frequency will show.

The advance `lock_f` already accounts for has to be subtracted first, and
for a long time it was not. The reference is rebuilt from `n = 0` every
frame while the received pilot advances with the sample index, so the raw
phase step measures the *absolute* offset rather than the residual, and
feeding that back made the loop an integrator with no error signal in it:
`lock_f` walked away instead of converging. Replaying a capture of two
real stations it drifted from +19 Hz to +8 Hz over 25 seconds on one over
and was still moving at the end of the other. With the expected advance
removed it settles in a second or two and stays there (+22.6 Hz and
-17.5 Hz on those same two overs). The loop is deliberately slow, about two
seconds, since a few Hz per minute is 0.01 Hz in that time. It is skipped
on any frame where the timing was nudged, because a one-sample shift
rotates the correlation by more than any frequency error would.

**The frequency search covers +/-50 Hz**, matching RADE's own
acquisition. An earlier +/-25 Hz was another way to find nothing if the
operator is slightly off frequency.

**Acquisition logs its progress** at each scoring point - 1 s, 2 s and
3.8 s into an integration - reporting the statistic for the side the passband
named, the threshold, the best frequency, and the decimated signal RMS:

```
rade_acquire: acq below carrier =7.44 (need 6.00 after 16 passes) f=+0.0 rms=1.2e-03
```

An RMS near zero means nothing is reaching the correlator at all, which is
a different problem from failing to correlate.
