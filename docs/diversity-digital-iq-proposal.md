# The Digital I/Q reference

> **This was a proposal; it is now shipped behaviour**, and not what the
> proposal asked for. Most of what the proposal specified already existed,
> and two of its central ideas could not work at the tap point it aimed
> at. What was built instead — an occupancy split feeding a two-element
> MVDR solve — is described here, and the reasoning that was discarded is
> recorded in [`diversity-auto-phasing.md`](diversity-auto-phasing.md).
>
> The reference description is §5 of [`diversity.md`](diversity.md), and
> how to use it is §3 of [`diversity-guide.md`](diversity-guide.md). The
> menu lists it as **FSK/Digital (occupancy MVDR)**; this document calls
> it Digital I/Q throughout, which is the `DIV_REF_DIGITAL_IQ` the code
> and the props file use.

## What it is for

A narrow digital signal — FT8, RTTY, PSK31, VARA, JS8 — in a passband
that is mostly empty. The empty part is the point: it is where the noise
can be measured on its own.

It also **replaces the wideband RADE passband reference**, which has been
removed. That mode placed a window on the nominal 750-2200 Hz modem band
and clipped it to the filter; this one starts from the same passband,
finds where the modem's energy actually is, and measures the noise
separately — which for a signal living near the noise floor, as RADE
does, is the difference that counts.

## The problem it solves

Every other reference computes **Sum** as `w = +Sxy/Sxx`. That is maximum
ratio combining *only* when the two branches carry equal, uncorrelated
noise, because nothing in the estimator ever forms a picture of the noise
apart from the signal. On a real station the assumption fails twice over:

- **Unequal branch noise.** On pre-Orion2 boards ADC1 is a bare
  rear-panel input, usually fed by a small loop or an active whip several
  dB noisier than the main antenna (`diversity.md` §1). Sum weights it as
  though it were as quiet as ADC0. The SINR-optimal weight is
  `conj(h)·N0/N1`; Sum computes `conj(h)`.
- **Correlated noise.** Much of what both antennas hear is common-mode
  hash conducted along both feedlines. Sum has no term for the
  correlation between the two branches' noise at all.

A second antenna exists largely to do something about the second of
these, and the older objective could not use it.

## How it works

```
region        = RX filter (follow ticked), or centre ± width/2
    │
    ▼   the transform is computed every block anyway
floor         = median of (Sxx+Syy) over the region
occupied      = bins > floor + 6 dB, and coherent between the arms
    │
    ├── this block's power in those bins more than 10 dB below
    │   the smoothed power that chose them?  -> stale, hold
    │
    ├── signal bins ──────────> h0 = Σ g²·Sxx,  h1 = conj(Σ g²·Sxy)
    │
    └── bins ≥ 4 clear of any
        occupied bin ────────> r00 = Σ Sxx, r11 = Σ Syy, r01 = Σ Sxy
    │
    ▼
Sum  : w = R⁻¹h        (div_mvdr2(), 1 % diagonal loading)
Null : w = -Sxy/Syy    over the occupied bins only
    │
    ▼   div_apply_weight(): +20 dB clamp, 15 % slew per block
```

Four things about this are load-bearing.

**A median, not a mean.** A signal filling part of the region would drag a
mean up with it and hide itself behind its own floor.

**A guard band.** A signal 40 dB above the noise puts more into its
neighbouring bins, through the analysis window's skirts, than the noise
floor holds — and those bins carry the signal's own channel. Feeding them
to `R` tells MVDR that the direction the signal arrives from is
interference, and it steers the null onto it. This is the textbook
failure of MVDR trained on data containing the desired signal, and it was
observed here: on a synthetic test with a strong interferer the weight
sat 8° off the correct answer until the guard was added.

**Correlated bins are not excluded from `R`.** Correlated noise is
precisely what `R` exists to describe; excluding coherent bins would
throw away the one thing this mode can do that Sum cannot. Distance from
the signal, not correlation, is what keeps the signal out of `R`.

**A transmission ending is not visible to anything else in the loop.**
Occupancy is a ratio against the median floor, so it is scale invariant
and does not see the level collapse; the coherence gate does not either,
because `Sxy`, `Sxx` and `Syy` decay together and `γ²` stays near 1 all
the way down. A 30 dB signal at 2 s averaging therefore kept the loop
reporting `track` for about twelve seconds after the transmission
stopped, adjusting the weight on noise the whole time.

Found here first, it turned out to be general, and the staleness test
that fixes it now runs for every transform reference — see §4 of
[`diversity.md`](diversity.md). It matters most on CW, where the signal
is absent for most of a transmission rather than only between them. What
is particular to this mode is that when it fires, the occupied span is
withdrawn from the status line and the panadapter too, so a signal that
has gone stops being drawn as one.

**Full and empty regions look the same and must not be treated the same.**
If the signal covers the region, the median *is* the signal and nothing
clears it — which is what a filter set snugly around the signal looks
like. Holding there would mean the better the filter, the less the mode
does. Coherence separates the two cases: a full region is coherent and is
accumulated whole, falling through to plain MRC; an empty one holds.

## Why there is no ±1500 Hz constant

The proposal's default was +1500 Hz in USB and −1500 Hz in LSB, derived
from the tapped buffer's spectral inversion. That reasoning double-counts:
the centre and width controls live in WDSP's *shifted* frame, and
`div_shift_to_bin()` applies the inversion downstream of them. A mode
table would also miss DIGU, DIGL, CW and CTUN.

The **Window follows RX filter** tick does the whole job instead. The
operator's passband is already on the correct side of the tuned frequency
in every mode, so following it needs no constant and no table — the same
argument that put the RADE references on `div_rade_side_expected()`
rather than on `vfo[].mode`.

## What it cannot do

**Separate a wanted signal from co-channel QRM.** Both are occupied and
both are correlated between the arms, so occupancy has nothing to tell
them apart by. That is what the RADE V1 pilot is for. Here the operator
separates them by placing the region, and **Null** cancels what the
region is sitting on — so unlike the RADE V1 reference, this one does not
force Sum on selection. Both objectives mean something.

## Measured

`test/diversity/test_digital.c`, on synthetic 2-FSK through a two-path
channel. FSK rather than a tone deliberately: a full-scale pure tone
leaks across the whole region even through a Blackman-Harris window, and
that leakage is correlated between the arms, so every result would be
measuring window sidelobes.

| Check | Result |
|---|---|
| Equal, uncorrelated branch noise | Within 0.2 dB and 2.3° of the Window reference on identical data — it degenerates as the algebra says |
| Aux branch 20 dB noisier | **+30.0 dB output SINR against Sum's +16.7 dB** |
| Correlated interferer clear of the signal | Stays on the signal, +38.9° against a true +37.7°; without the guard band, +29.5° |
| Occupancy span | Centre within 1 Hz of the true −1500 Hz, which also pins the frame conversion |
| Noise only | No weight produced at all |
| Signal fills the region | Falls back to MRC, within 0.1 dB of `conj(h)` |
| Strong signal stops dead | `track` to `search` in **one block** (0.09 s), weight unmoved. Without the staleness gate it never stopped tracking at all inside 17 s |

CPU cost is 0.3 % of one core at 48 kHz and 2.4 % at 384 kHz on an
i7-12700K, alongside the other transform modes; the median is capped at
4096 samples however wide the region is.

## Files

| File | Change |
|---|---|
| `src/diversity_auto.c`, `.h` | `DIV_REF_DIGITAL_IQ`, `div_digital_solve()`, `div_mvdr2()`, the modal window pair and its props |
| `src/rade_correlator.c` | `rade_mvdr_weight()` now calls the shared `div_mvdr2()` |
| `src/diversity_menu.c` | Combo entry, third modal pair, visibility, status line |
| `src/rx_panadapter.c` | Search region and occupied span overlay |
| `test/diversity/test_digital.c` | The seven checks above |
| `test/diversity/test_window.c` | A keyed carrier, for the staleness test on the Window reference |
| `test/diversity/test_props.c` | `diversity_auto_ref` migration across the retired RADE passband slot |
