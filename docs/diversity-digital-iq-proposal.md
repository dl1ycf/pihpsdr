# Proposal: I/Q-Space Digital Mode Diversity Correlation Routine

## Executive Summary

piHPSDR's automatic diversity combiner (`src/diversity_auto.c`) currently supports four reference modes:
1. **`DIV_REF_BAND`**: FFT wideband analysis following filter or manual window (Flat or Coherence-weighted).
2. **`DIV_REF_CARRIER`**: Carrier-bin tracker for narrow AM/SAM tones.
3. **`DIV_REF_RADE_BAND`**: FreeDV RADE passband power matching.
4. **`DIV_REF_RADE_V1`**: Pilot correlation & MVDR beamforming for FreeDV RADE V1.

This proposal introduces a dedicated **I/Q-Space Digital Mode Diversity Correlator** (`DIV_REF_DIGITAL_IQ`). This routine targets constant-envelope and continuous-phase digital modes (e.g. FSK derivatives, BPSK, QPSK, QAM) where signal quality correlates to phase-space dispersion, polar rotation stability, and envelope consistency in I/Q baseband space.

Key features:
- **Default Center Frequencies**: Automatically defaults to **-1500 Hz** in LSB and **+1500 Hz** in USB modes.
- **Selectable Modulation Width**: User-selectable bandwidth, defaulting to **500 Hz** (with an option for weighted full passband).
- **Flexible Window Placement**: Allows positioning the detection window **outside** the main passband to track and null strong adjacent-channel digital interference (`DIV_AUTO_NULL`).
- **Constant Envelope / Continuous Phase Focus**: Optimized for constant modulation modes (FSK/PSK/QAM); does not require or rely on On-Off Keying (OOK/CW keying).

---

## Technical & Mathematical Rationale in I/Q Space

```
                 Main Antenna (ADC0) -> z0(t) ---\
                                                   +--> Combined y(t) = z0(t) + w * z1(t)
Aux Antenna (ADC1) -> z1(t) -- [ Weight w ] -----/
                                     ^
                                     | (w = div_cos + j*div_sin)
                      +--------------+--------------+
                      |  I/Q Digital Correlator     |
                      |  - Polar Rotation Tracking  |
                      |  - Phase Space Dispersion   |
                      |  - Sub-band Filtering       |
                      +-----------------------------+
```

### 1. Polar Rotation ($\theta(t) = 2\pi \Delta f t$) & Frequency Offsets
In complex baseband (I/Q space), a tuning offset $\Delta f$ manifests as a continuous polar rotation:
$$z(t) = A(t) e^{j(2\pi \Delta f t + \phi(t))}$$
- **Carrier Offset Estimation**: The polar rotation rate $\frac{d\theta}{d t} = 2\pi \Delta f$ represents the center carrier or symbol rate frequency offset relative to the nominal receiver NCO.
- **Phase Trajectory Alignment**: By tracking polar rotation across the tapped I/Q buffers ($z_0$ and $z_1$), the estimator aligns the phase trajectories of both receiver channels prior to cross-spectral accumulation.

### 2. Phase Space Dispersion & SNR Correlation
For constant-envelope digital modulation modes (2-FSK, 4-FSK, 8-FSK, MSK, GFSK, BPSK, QPSK):
- **Ideal Signal Trajectory**: In the absence of fading or noise, the I/Q samples trace a well-defined constellation ring or tight phase clusters with constant envelope magnitude $|z(t)| = C$ and smooth phase derivative $\frac{d\phi}{d t}$.
- **Degraded/Faded Trajectory**: Multipath fading, destructive interference, and additive noise pull samples toward the origin ($|z(t)| \to 0$), dispersing phase trajectories across I/Q space.
- **Diversity Combining Metric**: 
  - **Phase Space Width / SNR Correlation**: Maximizing the magnitude-squared coherence $\gamma^2 = \frac{|S_{xy}|^2}{S_{xx} S_{yy}}$ over the digital sub-band co-phases the antennas such that the combined signal trajectory $y(t) = z_0(t) + w z_1(t)$ maximizes envelope stability and phase-space separation.
  - Wider, clean phase-space trajectories directly correlate to improved signal-to-noise ratio (SNR) and lower bit error rates (BER) at the decoder.

---

## Sub-band Location & Placement Rules

### Default Frequencies & Passband Inversion
In USB/LSB digital operations (e.g. FT8, RTTY, PSK31, VARA, FSK441), digital audio signals sit around audio frequencies of 1500 Hz. Because piHPSDR's tapped DDC stream is spectrally inverted:
- **USB**: Baseband digital signal is centered at **+1500 Hz**.
- **LSB**: Baseband digital signal is centered at **-1500 Hz**.

The proposed default detection window will automatically populate based on the active mode (USB $\to +1500\text{ Hz}$, LSB $\to -1500\text{ Hz}$), with a default detection width of **500 Hz**.

### Out-of-Passband Interference Nulling
Placing the detection window outside the receiver passband allows the automatic combiner to lock onto an adjacent digital signal (e.g. a strong $+3000\text{ Hz}$ interferer) and compute the exact complex weight $w = -S_{xy}/S_{yy}$ to cancel it via `DIV_AUTO_NULL`.

---

## Proposed System Architecture & Algorithm Design

### 1. Reference Mode Definition
In `src/diversity_auto.h`:
```c
enum {
  DIV_REF_BAND = 0,   // Wideband FFT window
  DIV_REF_CARRIER,    // Carrier tracking (AM/SAM)
  DIV_REF_RADE_BAND,  // FreeDV RADE passband
  DIV_REF_RADE_V1,    // FreeDV RADE V1 pilot MVDR
  DIV_REF_DIGITAL_IQ  // NEW: I/Q-Space Digital Mode Correlator (FSK/PSK/QAM)
};
```

### 2. Signal Processing Pipeline

```
[ Raw I/Q Taps z0, z1 ]
         │
         ▼
[ Sub-band Selection & Windowing ] 
   - Center: Default +/-1500 Hz (or manual)
   - Width: Default 500 Hz (or manual)
   - Supports Out-of-Passband Placement
         │
         ▼
[ Polar Rotation & Carrier Tracking ]
   - Sub-bin parabolic peak tracking of carrier/subcarrier center
   - Phase derivative estimation
         │
         ▼
[ Cross-Spectral Accumulation ]
   - Sxy = sum X0(k) * conj(X1(k))
   - Sxx = sum |X0(k)|^2,  Syy = sum |X1(k)|^2
   - Coherence weighting across modulation bandwidth
         │
         ▼
[ Complex Weight Solver & Slewer ]
   - Null Mode: w = -Sxy / Syy  (Cancel out-of-band or co-channel QRM)
   - Sum Mode : w = +Sxy / Sxx  (Maximum Ratio Combining in I/Q space)
   - Slew 15% / block into div_cos / div_sin
```

---

## Detailed Component Changes

---

### Component 1: Engine & Estimator Core
`src/diversity_auto.h`, `src/diversity_auto.c`

#### [MODIFY] [diversity_auto.h](file:///home/bminish/sdr/bm-pihpsdr/src/diversity_auto.h)
- Add `DIV_REF_DIGITAL_IQ` to `div_auto_ref` enum.
- Add global variables for digital mode parameters:
  - `div_digital_centre` (default $+1500\text{ Hz}$ for USB, $-1500\text{ Hz}$ for LSB).
  - `div_digital_width` (default $500\text{ Hz}$).
  - `div_digital_allow_outside` (boolean flag to allow out-of-passband windowing).

#### [MODIFY] [diversity_auto.c](file:///home/bminish/sdr/bm-pihpsdr/src/diversity_auto.c)
- **Sub-band Bin Calculation (`div_bin_range`)**:
  - Handle `DIV_REF_DIGITAL_IQ` mode.
  - Automatically calculate sub-band limits around `div_digital_centre` $\pm \frac{1}{2}\text{div\_digital\_width}$.
  - When `div_digital_allow_outside` is true, bypass clipping to `filter_low..filter_high`, permitting out-of-passband placement up to the Nyquist limit.
- **Polar Rotation Tracking (`div_process_block`)**:
  - For digital modes, compute the sub-band spectral peak and I/Q trajectory center offset within the selected 500 Hz window.
  - Perform coherence-weighted accumulator update ($S_{xy}, S_{xx}, S_{yy}$).
- **Default Mode Initialization**:
  - Automatically update `div_digital_centre` when switching receiver modes (USB $\to +1500\text{ Hz}$, LSB $\to -1500\text{ Hz}$).

---

### Component 2: User Interface & Panadapter Display
`src/diversity_menu.c`, `src/rx_panadapter.c`

#### [MODIFY] [diversity_menu.c](file:///home/bminish/sdr/bm-pihpsdr/src/diversity_menu.c)
- Add `"Digital I/Q (FSK/PSK)"` option to `ref_combo`.
- Add GTK controls for Digital I/Q mode:
  - **Center Frequency Spin Button** (Hz, range $-10000$ to $+10000\text{ Hz}$).
  - **Width Spin Button** (Hz, range $50$ to $5000\text{ Hz}$, default $500\text{ Hz}$).
  - **Allow Outside Passband Checkbox** (toggles out-of-passband placement).
- Update `ref_changed_cb()` to show/hide relevant controls when `DIV_REF_DIGITAL_IQ` is selected.

#### [MODIFY] [rx_panadapter.c](file:///home/bminish/sdr/bm-pihpsdr/src/rx_panadapter.c)
- Draw translucent green overlay band for the Digital I/Q analysis window.
- Draw a dashed indicator line at the digital center frequency ($\pm 1500\text{ Hz}$).

---

### Component 3: Testing & Verification
`test/diversity/`

#### [NEW] [test_digital_iq.c](file:///home/bminish/sdr/bm-pihpsdr/test/diversity/test_digital_iq.c)
- Unit test script generating synthetic 2-FSK and BPSK complex baseband signals with simulated antenna phase shift and gain difference.
- Verifies convergence of `w` under `DIV_AUTO_SUM` (MRC) and `DIV_AUTO_NULL` (Interference cancellation).
- Verifies performance with out-of-passband adjacent signal tracking.

---

## Verification Plan

### Automated Tests
1. **Compile Verification**:
   ```bash
   make clean && make -j4
   ```
2. **Diversity Test Suite**:
   ```bash
   make -C test/diversity run
   ```
3. **Synthetic Digital IQ Benchmark**:
   - Run `test_digital_iq` to verify weight accuracy ($< 0.1\text{ dB}$ gain error, $< 0.5^\circ$ phase error) on FSK, BPSK, and QPSK test vectors.

### Manual Verification
- Test `DIV_REF_DIGITAL_IQ` on live FT8/RTTY signals:
  - Set Auto to `Sum` $\to$ verify enhanced signal constellation and reduced BER.
  - Set Auto to `Null` with window parked on adjacent channel digital interferer $\to$ verify nulling of adjacent signal by $> 30\text{ dB}$.
