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
2. [The objectives: Null, Sum and Best](#2-the-objectives-null-sum-and-best)
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

## 2. The objectives: Null, Sum and Best

Null and Sum are computed from the same measurement — the cross spectrum
of the two antennas over the analysis window — and they differ only in
sign and in which power normalises them:

| | Weight | What it does |
|---|---|---|
| **Null** | `w = −Sxy / Syy` | Cancels whatever the two antennas have most in common |
| **Sum** | `w = +Sxy / Sxx` | Co-phases the antennas on it (maximum ratio combining) |

They are 180° apart. That is the useful property: if you are not sure
whether the array is pointed at the wanted signal or at the interference,
press **Invert** and listen. It swaps the objective *and* turns the weight
in force through 180° immediately, so the answer is audible at once rather
than after the loop reconverges.

**Best** does something different in kind: instead of combining the two
antennas it gives the output to whichever one is measuring better. Use it
when the antennas are not complementary but simply unequal on this band or
this signal — one hears the station and the other mostly does not, and
combining them only mixes noise into the good one.

The comparison is each arm's signal against its *own* noise floor, so it
tells a deaf antenna from a merely quiet one, and it carries 1 dB of
hysteresis so a marginal difference does not flap between the two. The
second status line always shows the comparison, whatever objective is
selected, and under Best it also shows which antenna won — so you can
leave the radio on Null or Sum and still see at a glance whether one
antenna is carrying the contact.

Two things to expect. Selecting ADC0 is exact, and the gain reads at the
bottom of its range. Selecting ADC1 cannot be exact — the combiner always
carries ADC0 at unity — so it rails the gain and tucks ADC0 in 20 dB
underneath, co-phased. That residue measurably helps rather than hurts.
And if neither arm can be measured (nothing standing clear of the noise on
both), Best **holds** rather than guessing, so a dead band leaves the
weight where it was rather than silently reverting to one antenna.

**Invert** does not apply to Best and is greyed out there; there is no
opposite answer to swap to.

The feature ships **off** — `Auto` starts at `Off (manual)` and the weight
stays where you left it. Null is offered first of the three,
cancelling a local noise source being the more common need. Selecting
the RADE V1 reference switches to Sum, since the signal the pilot
correlator is pointing at is the wanted one.

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

### RADE V1 pilot (MVDR)

The most capable and the most specialised. It correlates against RADE V1's
known pilot symbol, which separates the wanted signal from everything
else, and that in turn allows the *interference* covariance to be
estimated on its own. The result is a weight that steers a null onto the
QRM rather than onto the signal you are trying to decode.

It needs an actual RADE V1 signal and takes 1–5 s to acquire.

### Digital I/Q (occupancy MVDR)

For a narrow digital signal — FT8, RTTY, PSK31, VARA, JS8 — in a passband
that is otherwise empty. Tick **Window follows RX filter** and it works
out the rest: it finds where the signal actually is inside your passband,
measures the noise on the empty part, and combines against that.

That last bit is what makes it different. Every other reference has to
assume your two antennas are equally noisy, because it has no way to look
at the noise on its own. Yours probably are not — the aux is often a
small loop or a whip — and a lot of what both antennas hear is
common-mode hash off the feedlines, arriving on both. This mode measures
both and combines accordingly. Where that assumption *does* hold, it
gives the same answer the Window reference would.

Use it for:

- **Any digital mode, as the first thing to try.** Follow the filter,
  Auto `Sum`, and leave it. Compare against `Window` with **Hold** if you
  want to see what it is buying you.
- **A noisy second antenna.** This is where it wins biggest: it will back
  the aux antenna off on its own rather than weighting it as if it were
  as quiet as the main one.
- **A narrow filter around a strong signal.** Setting the filter snugly
  round one is fine — the mode notices the region is full rather than
  empty and falls back to plain co-phasing. A narrow filter with only
  weak signals in it is the case it does badly; see "Not for CW" below.

**Between transmissions it stops rather than drifts** — see §4. The
status line changes to `search` / `no signal` within a block and the
shaded band clears, so `track` → `search` → `track` is what you should
see across an FT8 gap.

It will *not* pull a wanted signal out from under co-channel QRM sitting
on top of it: both look equally like signal to it. Park the region on the
interferer and use `Null` for that, or use RADE V1 if it is a RADE
signal.

**Not for CW.** Measured on two 20 m CW captures with a 600 Hz filter, it
came out 1.6 to 2.0 dB *below* simply using the better antenna. The
occupancy split needs a signal that stands clear of the noise across a
region with room to spare, and a narrow filter on ordinary band CW gives
it neither: the strongest carrier is only 3.6 to 4.5 dB over the region
median, and enough noise bins clear the threshold to keep the loop
adapting on nothing. Use **Window** there. The same numbers are in
`docs/diversity-measurements.md` if you want to see the working.

The status line shows the occupied width it found, and the panadapter
shades those bins more strongly inside the search region — if the dark
band is not on your signal, the region is in the wrong place.

---

## 4. The rest of the controls

| Control | What it is for |
|---|---|
| **Window centre / width** | Where to look. Kept separately for Window, Carrier and Digital I/Q, so aiming the carrier tracker does not destroy your wideband window |
| **Resolution** | 12 / 6 / 3 Hz bins. Finer bins lift a weak signal out of the per-bin noise floor — a different thing from Averaging, which reduces the variance of an estimate rather than improving the SNR it is made from. Each step halves the update rate |
| **Weighting** | `Coherence` weights each bin by how well the two antennas agree in it. On speech it roughly halves the gain error, because the noise-only parts of a wide window stop diluting the answer. `Flat` is the older behaviour, kept for comparison |
| **Averaging** | 0.2–30 s time constant. Longer is steadier and follows fading more slowly. A weak AM carrier or an HF RADE path usually wants several seconds |
| **Min coherence** | Below this the loop holds rather than adapts, so it does not chase noise when there is nothing worth combining. Genuinely a per-path control, not a set-and-forget one: measured across recorded captures the *noise* on its own is 0.07 to 0.58 coherent between the arms, so on a path near the top of that range the default 0.30 cannot tell a signal both antennas hear from noise both antennas hear. If the loop adapts when there is plainly nothing there, raise it |
| **Hang** | 1–30 s, `RADE V1 pilot` only. How long a lock is held after the pilot goes before the correlator gives up and searches again. Long rides out a fade on one station; short is what a frequency several stations take turns on wants |
| **Restart averaging** | Throw away the statistics and start again |
| **Hold** | Stop *applying* the answer without stopping the loop |
| **Invert** | Swap Null and Sum. Greyed out under Best, which has no opposite |

### When the signal stops

Every reference now stops measuring within a block of the signal going
away, and holds the weight it had. You do not have to do anything about
it, but it is worth knowing what you are looking at: `track` dropping to
`wait` or `search` the moment a station stops sending is the loop working
properly, not losing lock.

Before this, the loop kept "tracking" for about twelve seconds after each
transmission — the averaging has to decay before the coherence figure
notices anything is wrong — and spent that time adjusting the weight on
noise.

**On CW this is the difference between the feature being useful and not.**
The signal is absent for most of a transmission, not just between them,
so the loop was previously averaging key-down and key-up together and
being pulled toward the noise on every gap. It now measures only while
the key is down.

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
Dig 12Hz  track  occ  293Hz   -2.1 dB   +38°
```

| Field | Meaning |
|---|---|
| First | The reference, and for the transform modes the bin width actually achieved. A `*` means the window ran past the Nyquist limit for this sample rate and was clamped |
| Second | `track` adapting · `wait` holding, nothing coherent enough to measure · `HOLD` your Hold · `search` looking — in Digital I/Q that means the region is empty or the signal has stopped · `confrm` confirming a RADE candidate · `LOCK` RADE tracking · `fade` RADE locked but the pilot is too weak to measure from |
| Third | Coherence, or the carrier frequency, or the RADE sideband and pilot percentage, or in Digital I/Q the occupied width found — `no signal` if the region is empty |
| Fourth | The weight. Under Hold this is the value the loop has **tracked to**, not the one being applied — the sliders show what is applied, and seeing the two apart is the point of the control |

Under it is a second line comparing the two antennas:

```
Antennas  measuring
Antennas  ADC1 better by  3.4 dB
Antennas  ADC0 better by 12.1 dB  using ADC0
```

`measuring` means there is not yet a signal standing clear of the noise
floor on both arms to compare. The trailing `using ADCn` appears only
under **Best** and is the antenna it has settled on. On a remote client
the line reads `Antennas  radio side`, because the measuring happens
there.

On the RX panadapter the analysis window is drawn as a translucent green
band under the trace. It is worth watching: it is otherwise an invisible
setting that changes what the radio does, and it can legitimately sit
outside the passband where there is nothing else to see. In Carrier mode a
brighter line marks where the tracker has settled inside the search
region, and in Digital I/Q the bins found occupied are shaded more
strongly inside it — if that darker band is not sitting on your signal,
the search region is in the wrong place.

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

Set **Hang** for the company you are keeping. Working one station on a
fading path, leave it long: a fade is when the combining weight is worth
the most, and a lock dropped in one has to be bought back on the signal
at its weakest. On a net or a roundtable, bring it down to two or three
seconds. Each station arrives over its own path with its own best gain
and phase, and until the lock on the last one is given up the new one is
being combined with the wrong weight — which on two antennas is not
neutral, it can be subtracting. At 2 s the lock is dropped about three
and a half seconds after the changeover and re-made about two seconds
after that.

**Comparing against your own settings.**
Let the loop settle, press **Hold**, and set the gain and phase by hand.
The status line's fourth field still shows what the loop makes of it.
Release Hold to snap back to the loop's answer.

---

### FT8 or RTTY, ordinary conditions

Measure on `Digital I/Q`, follow the filter, Auto `Sum`. Check the darker
shaded band on the panadapter lands on the signal. Nothing else to set.

If your aux antenna is much noisier than the main one — a whip or a small
loop — this is where it earns its keep, and comparing it against `Window`
with **Hold** is the quickest way to hear the difference.

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

Window and Carrier first, then the two RADE modes, then Digital I/Q —
which then replaced the wideband RADE passband mode, leaving four. The
carrier tracker originally used WDSP's SAM PLL, which turned out to be
about a hundred times wider than wanted for measuring a stable carrier —
25 Hz of loop bandwidth gives roughly 7 Hz of jitter. Finding the peak in
our own spectrum instead gives 0.002 Hz and works in plain AM as well.

Digital I/Q came last and is the only one that measures the noise apart
from the signal. That turned out to need a guard band around the occupied
bins: without one, a strong signal's own skirts land in the noise
estimate, MVDR reads the direction the signal arrives from as
interference, and steers the null onto it. It moved the answer 8° off on
a synthetic test before the guard existed.

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
samples are combined on the server, so the loop's own controls are inert.
Manual gain and phase are sent over the wire, but the radio takes them
only when its loop is not driving the weight — Auto off, or under Hold. If
Auto is running on the radio, the client's gain and phase sliders grey out
the same way the radio's own do, and the status line reads `Auto radio`
with the objective in use. So the sliders on the client always show what
the radio is really applying; they never accept a change that would be
quietly thrown away.

Client and server must be built from the same tree — they check a protocol
version on connect and refuse a mismatch.

**On pre-Orion2 boards the two chains are not symmetric.** Only ADC0's
path is under software control. The relationship between the antennas is
therefore stable within a band and jumps when the band changes.

---

## 9. Where to look next

- [`diversity.md`](diversity.md) — the reference: how each part works,
  every control, measured CPU and timings, and the frequency bookkeeping
  in full
- [`diversity-rade.md`](diversity-rade.md) — the RADE V1 pilot correlator in detail,
  including the pilot, the MVDR solution and the acquisition statistics
- [`diversity-dither-fix.md`](diversity-dither-fix.md) — the P2 dither bug
- [`diversity-auto-phasing.md`](diversity-auto-phasing.md) — design
  history, including the approaches that were tried and abandoned. Not a
  description of current behaviour
