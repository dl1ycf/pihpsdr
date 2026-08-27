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
"mirrored spectrum") alongside what the mode says, which is the only way
we get to learn what the convention actually is. The stage 1 window still
uses the mode, so if those two ever disagree on air, stage 1's window is
the thing that is wrong.

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
| +10 dB | lock, **-26.5 dB** interferer, +4.0 dB signal |
| +14 dB | lock, **-25.5 dB** interferer, +4.0 dB signal |
| +20 dB and above | no lock |

Identical on USB and LSB, with the weight coming out conjugated between
them as it should.

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
