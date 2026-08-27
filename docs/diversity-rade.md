# RADE diversity: passband window and V1 pilot correlator

Two additions to the diversity auto-phasing engine for FreeDV RADE, both
selectable from the Diversity menu's "Measure on" list.

Selecting either sets the objective to **Sum (max SNR)** rather than the
usual Null default: on RADE the signal we are pointing at *is* the wanted
one, so the job is to maximise its SNR, not to cancel the strongest
correlated thing in the window. The operator can still override it.

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

Frame layout is one pilot symbol then four data symbols, so a known
192-sample pilot recurs 8.33 times a second. The pilot symbols are
Barker-13 over the carriers scaled by sqrt(2), IDFT'd to the time domain.

## Sideband

RADE arrives through an SSB passband, so which side of the tuned frequency
it occupies depends on the mode, not the frequency. Below 10 MHz LSB is
the usual choice but that is convention, not a rule, so both modes follow
`vfo[0].mode` rather than guessing.

In the shifted frame - the one WDSP works in after `xshift()`, and the one
piHPSDR's filter edges are expressed in - positive frequency is the upper
sideband. USB and DIGU filters are positive, LSB and DIGL negative (see
`filterUSB` / `filterLSB` in `filter.c`). So:

| Mode | RADE window |
|---|---|
| USB, DIGU | +750 .. +2200 Hz |
| LSB, DIGL | -2200 .. -750 Hz |

### The correlator measures the sense rather than deriving it

The first version derived the correlator's spectral sense from the mode
too, conjugating the input for LSB. **It never acquired on air**, against
a signal RADE itself was decoding at 10 dB SNR.

The convention relating the raw DDC stream to the frame WDSP works in
after `xshift()` is genuinely hard to pin down by reading the code: the
direction implied by `xshift()` multiplying by `exp(+j*2*pi*offset*t)` and
the direction implied by the signs of the USB and LSB filter edges do not
agree, and I could not resolve which premise was wrong. Guessing is
silent - the correlator looks at the mirror image and finds nothing, for
ever, with no indication of why.

So it is no longer derived. Correlating a mirrored stream against the
pilot is identical to correlating the original stream against a *mirrored
pilot*, and `conj(p)` is exactly the pilot with its carriers reflected
about zero. Acquisition searches both pilot banks over the same decimated
stream and keeps whichever correlates, at twice the acquisition cost and
no extra front end.

That removes conjugation from the sample path altogether, so there is no
longer a weight to conjugate back: whichever bank wins, `h0` and `h1`
describe the real untouched arms and the MVDR solution applies directly.

The detected sense is shown in the status line ("normal spectrum" /
"mirrored spectrum") alongside what the mode says.

**On air the mode-based rule turned out to be backwards.** Against a real
LSB signal the un-mirrored pilot bank scored 12.8 / 14.7 / 15.9 while the
mirrored bank scored 3.6 / 4.1 / 4.0 - a decisive result the opposite way
round from what deriving it from the mode predicted. Stage 1 therefore no
longer uses the mode either: it sums the energy in the modem band on both
sides of the carrier each block and keeps the stronger, and reports which
side it chose next to what the mode says.

## Stage 1: RADE passband

Places the existing cross-spectrum window on 750..2200 Hz on the correct
side of the carrier. Everything else is the wideband engine unchanged.

Gives maximum ratio combining across the modem band. It does **not** null
QRM: under a correlated interferer, plain cross-correlation locks onto
whatever is strongest and correlated, which is the interferer.

## Stage 2: RADE V1 pilot correlator (MVDR)

`src/rade_correlator.c`. Per arm: NCO shift to the tuned frame, mirror for
LSB, polyphase FIR decimation to 8 kHz, then correlation against the known
pilot over a (timing, frequency) grid.

Once locked, each frame gives channel estimates `h0`, `h1` from the pilot
correlation, and the **residual** `n_i = s_i - h_i*p` over the pilot span
gives the interference covariance `Rnn` separately from the signal. Then

```
w = conj( g1/g0 ),   g = Rnn^-1 h
```

with 1% diagonal loading. With `Rnn` diagonal and equal this reduces to
`conj(h1/h0)`, the maximum ratio answer, so it degenerates gracefully to
what stage 1 does when there is no correlated interference to null.

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

Identical on USB and LSB, with the weight coming out conjugated between
them as it should.

## Tracking

Holding a lock is deliberately far more forgiving than getting one.

The first version gated every tracking frame on a fresh detection
statistic computed from one correlation and eight probes, against
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
depend on signal level and cannot ratchet. Both terms are smoothed over
about 6 seconds, and the lock is dropped only after the ratio stays below
1.35 for ten seconds - long enough to ride out fades, short enough to
notice an over ending. A working lock reads around 2 even with a strong
in-band interferer inflating the floor.

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
tracking perfectly. It is the share of pilot-span energy the pilot itself
accounts for, which is genuinely small when something loud is sitting on
top of it. Watch the LOCK indicator, not that number.

**The frequency search covers +/-50 Hz**, matching RADE's own
acquisition. An earlier +/-25 Hz was another way to find nothing if the
operator is slightly off frequency.

**Acquisition logs its progress** once per completed integration set
(about 4 s), reporting the statistic for both pilot banks, the threshold,
the best frequency, and the decimated signal RMS:

```
rade_acquire: acq normal=2.31 mirrored=7.44 best=7.44 (need 6.0) f=+0.0 rms=1.2e-03 mode=USB
```

An RMS near zero means nothing is reaching the correlator at all, which is
a different problem from failing to correlate.
