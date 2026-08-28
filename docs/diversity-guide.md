# Automatic diversity phasing — what was built, and how to use it

piHPSDR's diversity combiner takes two coherent receive chains and adds
them with one complex weight:

```
y = z0 + w·z1
```

Until now `w` was set by hand, with a gain and a phase control, and
finding the right pair meant hunting by ear every time the band, the
antenna or the interference moved. This work adds an analysis thread that
measures the relationship between the two antennas and sets `w` for you,
either to **cancel** something or to **combine** for the best signal.

Everything is host-side. The radio only guarantees that the two chains are
coherent and identically configured; no weighting happens in the FPGA.

**Contents**

1. [Quick start](#1-quick-start)
2. [The two objectives: Null and Sum](#2-the-two-objectives-null-and-sum)
3. [Choosing what to measure on](#3-choosing-what-to-measure-on)
4. [The rest of the controls](#4-the-rest-of-the-controls)
5. [Reading the status line and the overlay](#5-reading-the-status-line-and-the-overlay)
6. [Worked examples](#6-worked-examples)
7. [What was changed, and why](#7-what-was-changed-and-why)
8. [Limits and things to know](#8-limits-and-things-to-know)
9. [Where to look next](#9-where-to-look-next)

---

## 1. Quick start

1. **Diversity Enable** in the Diversity menu. This re-plumbs the DDCs, so
   expect a moment's interruption.
2. Set **Auto** to `Sum` to combine both antennas for the best signal, or
   `Null` to cancel the strongest thing the two have in common.
3. Leave **Measure on** at `Window (wideband)` with **Window follows RX
   filter** ticked — that is where it starts.
4. Watch the status line at the bottom of the menu. When it says `track`
   and the coherence is above about 30 %, the weight it has found is the
   one being applied.

That is the whole of it for ordinary use. Everything below is about doing
better than that on a particular signal.

---

## 2. The two objectives: Null and Sum

Both are computed from the same measurement — the cross spectrum of the
two antennas over the analysis window — and they differ only in sign and
in which power normalises them:

| | Weight | What it does |
|---|---|---|
| **Null** | `w = −Sxy / Syy` | Cancels whatever the two antennas have most in common |
| **Sum** | `w = +Sxy / Sxx` | Co-phases the antennas on it (maximum ratio combining) |

They are 180° apart. That is the useful property: if you are not sure
whether the array is pointed at the wanted signal or at the interference,
press **Invert** and listen. It swaps the objective *and* turns the weight
in force through 180° immediately, so the answer is audible at once rather
than after the loop reconverges.

The feature ships **off** — `Auto` starts at `Off (manual)` and the weight
stays where you left it. Null is offered first of the two objectives,
cancelling a local noise source being the more common need. Selecting
either RADE reference switches to Sum, since on RADE the signal being
pointed at is the wanted one.

---

## 3. Choosing what to measure on

The **Measure on** control decides which part of the spectrum the decision
is taken from. The weight is always applied to the whole band; only the
*decision* is restricted.

The menu shows only the controls the selected reference can use, so the
dialog changes shape as you move between them.

### Window (wideband)

Accumulates over every bin in an analysis window. With **Window follows RX
filter** ticked, that is your passband. Untick it and you get **Window
centre** and **Window width** to place it by hand.

Use it for:

- **General work.** Follow the filter, Coherence weighting, and let it run.
- **A known noise.** Park a window directly on a carrier or a birdie and
  select Null. The window may sit *outside* the passband — measuring the
  noise on its own, away from the wanted signal, is often the cleaner
  measurement.
- **FSK and other two-tone signals.** Size the window to take in just the
  mark and space tones.

### Carrier (AM/SAM)

Accumulates over the carrier bin alone, with the carrier found from our
own spectrum. Much more selective than a window, and the right choice for
AM broadcast.

The **Window centre** and **Window width** controls become the *search
region*. That is what lets you track a carrier other than the primary one:
park a 1 kHz window on +5 kHz and the primary is outside the search
entirely, so you can null the station carrying that carrier while
listening to the one on frequency.

### RADE passband

Places the window on the FreeDV RADE modem band, 750–2200 Hz, on the side
of the tuned frequency your passband implies, clipped to your filter.
Maximum ratio combining across the modem band. No QRM nulling.

### RADE V1 pilot (MVDR)

The most capable and the most specialised. It correlates against RADE V1's
known pilot symbol, which separates the wanted signal from everything
else, and that in turn allows the *interference* covariance to be
estimated on its own. The result is a weight that steers a null onto the
QRM rather than onto the signal you are trying to decode.

It needs an actual RADE V1 signal and takes 1–5 s to acquire.

---

## 4. The rest of the controls

| Control | What it is for |
|---|---|
| **Window centre / width** | Where to look. Kept separately for Window and Carrier, so aiming the carrier tracker does not destroy your wideband window |
| **Resolution** | 12 / 6 / 3 Hz bins. Finer bins lift a weak signal out of the per-bin noise floor — a different thing from Averaging, which reduces the variance of an estimate rather than improving the SNR it is made from. Each step halves the update rate |
| **Weighting** | `Coherence` weights each bin by how well the two antennas agree in it. On speech it roughly halves the gain error, because the noise-only parts of a wide window stop diluting the answer. `Flat` is the older behaviour, kept for comparison |
| **Averaging** | 0.2–30 s time constant. Longer is steadier and follows fading more slowly. A weak AM carrier or an HF RADE path usually wants several seconds |
| **Min coherence** | Below this the loop holds rather than adapts, so it does not chase noise when there is nothing worth combining |
| **Restart averaging** | Throw away the statistics and start again |
| **Hold** | Stop *applying* the answer without stopping the loop |
| **Invert** | Swap Null and Sum |

### Hold

The loop keeps measuring, the status line keeps showing where it has got
to, and the gain and phase controls become yours. Releasing puts the
tracked answer back in place in one step.

Two things this makes easy that were not:

- **Comparing** the loop's answer against a hand-set one on the same
  signal, without losing the loop's progress.
- **Riding out** a period when the band is doing something the loop should
  not follow.

It is released when the menu closes — there is no indicator for it
anywhere else, and a loop that had silently stopped applying anything
would be hard to diagnose.

While the loop is driving, the diversity gain and phase controls are inert
everywhere — the menu sliders, the encoder actions and the popup sliders
alike. Hold is how you take the weight over.

---

## 5. Reading the status line and the overlay

The status line is four fixed columns: what is being measured, what the
loop is doing, one detail belonging to the mode, and the weight.

```
Win 12Hz  track  coh 100%     -2.1 dB   +32°
Car  3Hz* HOLD   +400000 Hz  -12.3 dB  +179°
RADE V1   LOCK   LSB 100%     -2.1 dB   +32°
```

| Field | Meaning |
|---|---|
| First | The reference, and for the transform modes the bin width actually achieved. A `*` means the window ran past the Nyquist limit for this sample rate and was clamped |
| Second | `track` adapting · `wait` holding, nothing coherent enough to measure · `HOLD` your Hold · `search` looking · `confrm` confirming a RADE candidate · `LOCK` RADE tracking · `fade` RADE locked but the pilot is too weak to measure from |
| Third | Coherence, or the carrier frequency, or the RADE sideband and pilot percentage |
| Fourth | The weight. Under Hold this is the value the loop has **tracked to**, not the one being applied — the sliders show what is applied, and seeing the two apart is the point of the control |

On the RX panadapter the analysis window is drawn as a translucent green
band under the trace. It is worth watching: it is otherwise an invisible
setting that changes what the radio does, and it can legitimately sit
outside the passband where there is nothing else to see. In Carrier mode a
brighter line marks where the tracker has settled inside the search
region.

With **Window follows RX filter** ticked, the green band lands exactly on
the filter shading. That is a free check that the frequency conversion is
right.

---

## 6. Worked examples

**A local noise source on 40 m, wanted signal is SSB.**
Auto `Null`, Measure on `Window`, follow the filter, Coherence weighting,
Averaging around 3 s. If the noise is stronger outside the passband than
in it, untick follow and park a 1–2 kHz window directly on the noise; the
weight applies to the whole band regardless of where it was measured.

**Weak AM broadcast with a splattering carrier 5 kHz up.**
Auto `Null`, Measure on `Carrier`. Set Window centre to +5000 and width to
1000, so the search cannot see the wanted station's own carrier. Set
Resolution to 6 or 3 Hz and Averaging long — 10 s or more. The carrier is
not going anywhere, and the only thing that helps a weak one is a narrower
bin.

**SSB voice, no carrier to lock to.**
Auto `Sum` or `Null` as needed, Measure on `Window`, follow the filter,
Weighting `Coherence`. Hand-placing a narrow window on the loudest part of
the voice used to be the only way to make this work; coherence weighting
is what makes running the whole passband the better option, because it
discounts the bins where the two antennas do not agree.

**FreeDV RADE.**
Measure on `RADE V1 pilot`. Check the status line shows the sideband you
are actually using — that is the one thing about this mode you can get
wrong, and with the passband on the wrong sideband it will never lock.
Averaging several seconds. Expect `search` → `confrm` → `LOCK` within a
few seconds of the transmission starting.

**Comparing against your own settings.**
Let the loop settle, press **Hold**, and set the gain and phase by hand.
The status line's fourth field still shows what the loop makes of it.
Release Hold to snap back to the loop's answer.

---

## 7. What was changed, and why

### A bug found on the way in

Under Protocol 2, ADC1 never received the dither and random settings —
`new_protocol.c` shifted the bits the wrong way. The two receive chains
were therefore *not* identically configured, which matters for a
measurement that assumes they are. Fixed separately, ahead of everything
else; see [`diversity-dither-fix.md`](diversity-dither-fix.md).

### The engine

A tap in `rx_add_div_iq_samples()` copies the raw, uncombined pair into a
small queue ahead of the combiner and the noise blanker, so both antennas
are seen with identical — that is, no — processing. An analysis thread
transforms a block of each arm, accumulates the cross and auto spectra
over the chosen window, and writes a weight. The block period is 85 ms at
every sample rate.

The transform modes cost under 2 % of one core at 384 kHz on a desktop and
less at lower rates. RADE V1 while searching is the peak load at 5–8 % and
is nearly rate-independent, because acquisition works on a fixed 8 kHz
decimated stream. A Raspberry Pi is several times slower at this kind of
scalar work, so scale accordingly — the searching figure is the one to
watch there. Measured numbers are in
[`diversity.md`](diversity.md) §7.

### Four references, built in stages

Window and Carrier first, then the two RADE modes. The carrier tracker
originally used WDSP's SAM PLL, which turned out to be about a hundred
times wider than wanted for measuring a stable carrier — 25 Hz of loop
bandwidth gives roughly 7 Hz of jitter. Finding the peak in our own
spectrum instead gives 0.002 Hz and works in plain AM as well.

### Things that only measurement settled

Two conventions in this code could not be pinned down by reading it, and
reasoning about them produced confident wrong answers more than once:

- **The tapped buffer is spectrally inverted with respect to RF.** A
  signal above the dial appears at a negative complex frequency in it.
  Three independent on-air observations say so. Until this was established
  a hand-placed window at +5 kHz measured −5 kHz, the RADE window sat on
  the wrong sideband, and under CTUN the analysis measured a window `2 ×
  offset` away from the one drawn on the screen.
- **Which pilot bank corresponds to which sideband.** Bank 0, the pilot as
  transmitted, is the LSB bank — LSB inverts the audio on transmit, and
  the path to the tap inverts it again, so it arrives the right way up.

Neither showed with a symmetric window, CTUN off, in a phone mode, which
is most bench testing. If either is ever revisited, revisit it with a
signal: put a known carrier a few kHz off the dial, run the Carrier
reference, and see which way the tracked frequency moves.

### RADE acquisition, from 11.5 s to 1–5 s

Acquisition originally needed three consecutive full 32-pass searches
before it would declare a lock — 11.5 s for every signal however strong.
It now scores its search grid progressively at 8, 16 and 32 passes with
the threshold coming down as the integration lengthens, and confirms the
candidate by following that one timing and frequency for eight frames
instead of repeating the whole blind search. No weight is produced during
confirmation, so a false alarm costs a second and never moves the
combiner. Measured at 2.2 s on synthetic signals.

The pilot frequency is tracked once locked, from the pilot-to-pilot phase
advance, which removes the search-grid quantisation and follows the few Hz
per minute a station drifts.

### The user interface

Controls the selected reference cannot use are hidden rather than greyed
out. The status line is held to a fixed width in fixed columns so it
cannot dictate a wider window than the controls need. **Hold** and
**Invert** were added.

---

## 8. Limits and things to know

**The weight is applied full-band; the decision is not.** Restricting the
measurement to a window is what stops a strong out-of-band signal running
away with the solution — but signals outside the window are still
combined, with a weight that is arbitrary for them. Harmless for audio,
since WDSP's filter removes them, but the combined stream can peak above
either arm on its own, which eats headroom and can shift noise blanker
thresholds.

**A window away from the signal is fine, within reason.** Measuring the
channel at one frequency and applying it at another is valid while the
differential delay between the antennas keeps the phase flat over the gap.
With 30 m of feedline difference a 10 kHz offset costs 0.55°, against the
5.7° that 20 dB of cancellation needs. Only a very large feedline
difference combined with a 30 kHz offset starts to matter.

**Antenna and attenuator changes are not watched.** The analysis restarts
on a change of frequency, sample rate, mode, filter or window setting. It
does not notice an antenna or attenuator change, which also shifts the
relationship between the arms; the estimate re-converges over a few time
constants instead. **Restart averaging** if you do not want to wait.

**The automatic loop runs on the radio side only.** On a remote client the
samples are combined on the server, so the auto controls are inert; manual
gain and phase still work and are sent over the wire.

**On pre-Orion2 boards the two chains are not symmetric.** Only ADC0's
path is under software control. The relationship between the antennas is
therefore stable within a band and jumps when the band changes.

---

## 9. Where to look next

- [`diversity.md`](diversity.md) — the reference: how each part works,
  every control, measured CPU and timings, and the frequency bookkeeping
  in full
- [`diversity-rade.md`](diversity-rade.md) — the RADE modes in detail,
  including the pilot, the MVDR solution and the acquisition statistics
- [`diversity-dither-fix.md`](diversity-dither-fix.md) — the P2 dither bug
- [`diversity-auto-phasing.md`](diversity-auto-phasing.md) — design
  history, including the approaches that were tried and abandoned. Not a
  description of current behaviour
