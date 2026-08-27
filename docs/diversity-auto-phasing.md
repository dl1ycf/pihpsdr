# Automatic diversity phasing

Automatic determination of the DIVERSITY gain and phase, by measuring the
cross spectrum of the two antennas over a narrow, operator-controlled slice
of the passband.

## What diversity does today

Diversity combining in piHPSDR is entirely a host-side operation. The radio
only guarantees that the two receive chains are coherent and identically
configured: for Protocol 2, `new_protocol.c` ties DDC1 to DDC0
(`receive_specific_buffer[1363] = 0x02`), gives both DDCs the same
frequency and sample rate, and forces ADC1 to take ADC0's step attenuator,
band-pass filter and dither/random settings. No weighting happens in the
FPGA.

The combination itself is three lines in `rx_add_div_iq_samples()`
(`src/receiver.c`):

```c
double i_sample = i0 + (div_cos * i1 - div_sin * q1);
double q_sample = q0 + (div_sin * i1 + div_cos * q1);
```

which is `z = z0 + w*z1` with a single complex weight
`w = 10^(div_gain/20) * exp(j*div_phase)`, flat across the whole DDC
passband, applied per raw IQ sample ahead of WDSP.

## What this adds

`src/diversity_auto.c` estimates a good value for `w` and writes it back
into `div_cos`/`div_sin`, back-computing `div_gain`/`div_phase` so that the
menu, the props file and remote clients stay consistent with what is
actually being applied.

### Estimator

Every block of `nfft` sample pairs is tapped from `rx_add_div_iq_samples()`
before the summation and before the noise blanker, so both antennas are
seen with identical processing. The block is windowed and transformed, and
over the bins inside the analysis window we accumulate

```
Sxy = sum X0(k) * conj(X1(k))     Sxx = sum |X0(k)|^2     Syy = sum |X1(k)|^2
```

with exponential forgetting across blocks. Then

| Mode | Weight | Meaning |
|---|---|---|
| Null | `w = -Sxy/Syy` | minimises `E|z0 + w*z1|^2` — subtracts whatever is common to both antennas. The default. |
| Sum  | `w = +Sxy/Sxx` | equals `conj(h)` for `z1 = h*z0`, i.e. maximum ratio combining when both channels carry equal noise power. |

The two cases use **different denominators**; they are not sign-flipped
versions of one another. Since `Sxx` and `Syy` are both positive reals the
two weights are nonetheless exactly 180 degrees apart, differing only in
magnitude by `Sxx/Syy` - verified numerically at 180.00 degrees.

Both are computed from the *same* accumulated statistics, so switching
between them is only a change of formula. Nothing about the transform,
the window, the bin mask or the accumulators depends on which is
selected, and the change applies on the next block.

An early version restarted the whole analysis engine when the objective
changed, which reset those shared accumulators. With a long averaging
time both objectives then spent seconds re-converging from nothing, so
switching between them appeared to do nothing at all. The engine is now
restarted only when the analysis thread itself has to start or stop, and
an explicit change of objective is applied without slewing, since the
operator is usually switching in order to compare the two.

Fit quality is the magnitude squared coherence
`gamma^2 = |Sxy|^2 / (Sxx*Syy)`, which is 1 when one complex weight
describes the relationship perfectly. Below `Min coherence` the loop holds
rather than chasing noise.

Verified numerically against a synthetic two-path channel: the null weight
matches `-1/h` to 0.001 degrees and cancels an in-window tone by 82 dB, and
the sum weight matches `conj(h)` and delivers the `(1+|h|^2)` power gain
that maximum ratio combining predicts.

### No filter, no resampler

The passband selection is a bin mask on the FFT rather than an actual
filter. That means there is no filter to design, no group delay to match
between the two arms, and no possibility of the arms being processed
differently — the transform is identical for both by construction.

The FFT runs at the native DDC rate, so there is no decimation stage and no
NCO either. `nfft` is chosen per sample rate to land near a 12 Hz bin
width, which keeps both the resolution and the block length (~85 ms) the
same whatever the radio is running at:

| Sample rate | nfft | bin width | block |
|---|---|---|---|
| 48 kHz  | 4096  | 11.7 Hz | 85 ms |
| 96 kHz  | 8192  | 11.7 Hz | 85 ms |
| 192 kHz | 16384 | 11.7 Hz | 85 ms |
| 384 kHz | 32768 | 11.7 Hz | 85 ms |

Two 32768-point single-precision transforms per 85 ms is around 1% of one
core on a Pi 4.

The window is a 4-term Blackman-Harris. The whole point is to look at one
narrow slice of spectrum and ignore everything else, so its -92 dB
sidelobes are worth having over the -31 dB of a Hann.

### Frequency bookkeeping

We tap the raw DDC streams, ahead of WDSP. WDSP's first stage is `xshift()`,
which multiplies by `exp(+j*2*pi*offset*t)` with `offset = vfo[0].offset`.
Everything after it — the operator's passband (`filter_low`/`filter_high`)
and the SAM PLL's carrier frequency — is expressed in that shifted frame,
where the tuned signal sits at zero. So a frequency `f` in the shifted
frame is at `f - offset` in the raw frame, and that one relation covers
both the filter edges and the PLL.

This is worth stating explicitly because it means **no sign constant is
needed**. Whether the raw DDC baseband runs the same way as RF or is
mirrored never comes up: both quantities we care about arrive already
expressed in the same frame as each other.

### Reference: window or carrier

*Window* (method A) accumulates over every bin in the analysis window.
The window either follows the RX filter or is placed by hand with a centre
and width, so it can be parked on a known noise, or sized to take in just
the mark and space tones of an FSK signal.

*Carrier* (method B) accumulates over the carrier bin only, with the
carrier located from our own spectrum rather than from WDSP's SAM PLL.

The first version did use the PLL, via a `GetRXAAMDCarrierFreq()`
accessor added to `wdsp/amd.c`. On air the reported frequency wandered by
several Hz a second on a weak carrier, and the Averaging control did
nothing about it - that control drives the weight accumulators, not
WDSP's loop.

The numbers explain it. WDSP creates the SAM PLL with `omegaN` 250 rad/s
and unity damping, which is a 39.8 Hz natural frequency and about 25 Hz
of one-sided loop noise bandwidth - roughly 7 Hz rms of frequency jitter
on a carrier at 0 dB in that bandwidth. That is the right design for
demodulating SAM, where the loop has to acquire quickly and follow drift,
and about a hundred times wider than is wanted for measuring a carrier
that is not going anywhere. It cannot be narrowed without spoiling the
audio it exists to produce.

Since the spectrum is computed every block anyway, the carrier is found
as the peak bin within +/-500 Hz of the tuned frequency, refined by
parabolic interpolation on log power across the three bins about the
peak, and then smoothed with the operator's averaging time constant.
Sub-bin accurate, as slow as the operator wants, and it works in plain AM
as well - where the SAM PLL does not run at all, `xamd()` case 0 being a
simple envelope detector that never touches `phs`, `omega` or `fil_out`.

## Things to know

**The applied weight is full-band; the decision is not.** Restricting the
measurement to the window is what stops a strong out-of-band signal from
running away with the solution. But signals outside the window are still
combined, with a weight that is arbitrary for them. Harmless for audio,
since WDSP's own filter removes them — but the combined stream handed to
WDSP can peak above either arm on its own, which eats headroom and shifts
noise blanker thresholds. The synthetic test above shows total power rising
1.4 dB while the in-window tone drops 82 dB. The automatic loop is
therefore clamped to +20 dB, inside the +/-27 dB the manual sliders allow.

**Convergence is a fixed fraction of the remaining distance** (15% per
block, so a little over a second from any starting point) rather than a
fixed step. A fixed absolute step was tried first and is wrong: the time to
converge then depends on how far away the answer is, and a large `|w|` took
the best part of a minute.

**The weight is frequency flat, so it is only valid where it was measured.**
Different feedline lengths and preselector group delay make `arg h(f)`
vary across frequency. The analysis thread watches the tuned frequency,
sample rate, mode, filter edges and window settings, and throws away the
accumulated statistics whenever any of them change, rather than relying on
call sites to notify it.

**Buffers are allocated once and never freed.** The RX sample path checks
`div_auto_running` without a lock and can already be inside
`diversity_auto_sample()` when `diversity_auto_stop()` runs, so freeing on
stop would be a use-after-free. A few MB is a cheap way to make start/stop
trivially safe.

**Remote clients cannot run this.** Combining happens on the server, so
`rx_add_div_iq_samples()` never runs client-side and there would be nothing
to analyse. The auto controls are greyed out; manual gain and phase still
work and are sent over the wire as before.

## Files

| File | Change |
|---|---|
| `src/diversity_auto.c`, `.h` | the estimator and its analysis thread |
| `src/receiver.c` | tap in `rx_add_div_iq_samples()`; stop/restart around sample rate changes |
| `src/radio.c` | start/stop with diversity, props, startup hook |
| `src/diversity_menu.c` | controls and status readout |
| `wdsp/amd.c`, `amd.h`, `wdsp.h` | `GetRXAAMDCarrierFreq()`, `GetRXAAMDPLLRunning()` |

## Not done yet

The per-bin `h(f)` and coherence are computed but only the aggregate is
used. Fitting a straight line to `arg h(f)` across the window would give
the differential delay between the two feedlines directly; compensating it
with a fractional-sample delay on one arm would make the flat weight
genuinely optimal over a wider bandwidth. That is the natural next step.
