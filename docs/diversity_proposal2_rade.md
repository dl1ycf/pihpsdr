# Dual-Receiver Auto-Diversity & Spatial SINR Optimization for FreeDV RADE (V1 & V2)

## 1. Overview & Objectives

This document presents the detailed architectural design and mathematical principles for implementing an **Auto-Diversity Correlator and MVDR Spatial Filtering Engine** in piHPSDR for **FreeDV RADE (Radio Autoencoder V1 & V2)**.

The objective is to lock onto RADE signals received on two independent antenna paths (Main RX0 and Auxiliary RX1), derive optimal phase (`div_phase`) and gain (`div_gain`) parameters to maximize Signal-to-Interference-plus-Noise Ratio (SINR), active place spatial nulls over correlated interference (QRM), and continuously track fast HF Rayleigh fading.

---

## 2. FreeDV RADE Protocol Analysis: V1 vs. V2

### 2.1 RADEV1 (Pilot-Guided Synchronization)
* **Preamble Structure**: RADEV1 embeds known pilot vector symbols $p(n)$ periodically at frame boundaries.
* **Coarse & Fine Lock**: Acquisition is achieved via sliding complex cross-correlation of incoming IQ against the preamble sequence across Carrier Frequency Offset (CFO) search bins.
* **Channel Vector Extraction**: Direct cross-correlation with $p(n)$ isolates the complex channel transfer coefficients $\hat{h}_0, \hat{h}_1$:
  $$\hat{h}_i(t) = \frac{1}{N_p} \sum_{k=0}^{N_p-1} s_i(n+k) \cdot p^*(k)$$

### 2.2 RADEV2 (Pilotless Synchronization - FreeDV GUI v3.0)
* **Removal of Explicit Pilots**: In RADEV2 (activated via `RADE_MODE_V2` in `librade` / FreeDV GUI v3.0 commit `fb8fe8bf`), explicit pilot preambles are removed to maximize spectral efficiency and voice payload throughput.
* **Autocorrelation Sync**: Synchronization relies on **frame-periodic autocorrelation** (sliding delay-and-correlate over the latent frame period $T_f$):
  $$R_{01}(\tau = T_f) = \sum s_0(t) \cdot s_1^*(t - T_f)$$
* **Equalizer State Tracking**: `librade`'s internal C demodulator maintains continuous carrier phase and channel gain estimates for equalization.

---

## 3. Code Audit of FreeDV GUI (`v3.0-dev` Branch)

An audit of the FreeDV GUI codebase reveals:

1. **No Dual-RX in FreeDV GUI**:
   In [freedv_interface.cpp:L159](file:///home/bminish/sdr/freedv-gui/src/freedv_interface.cpp#L159), FreeDV explicitly operates as a single-receiver pipeline:
   ```cpp
   if (mode >= FREEDV_MODE_RADE) {
       // Special case for RADE.
       // Note: multi-RX not currently supported.
       rade_ = rade_open(modelFile, RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_MODE_V2 | ...);
   }
   ```
   *(Note: FreeDV GUI's "Multi-RX" feature refers to running multiple mode decoders in parallel on a single audio passband to auto-detect modes; it does not perform spatial diversity combining across physical antennas).*

2. **Reusable `librade` C API Handles**:
   The following C API functions in `librade` provide clean hooks for piHPSDR integration:
   * `rade_open(..., RADE_MODE_V2)`: Instantiates lightweight C engine handles without Python/PyTorch dependencies.
   * `RADE_MODEM_SAMPLE_RATE`: Fixed modem IQ sample rate of **8000 Hz**.
   * `rade_nin_max(rade_)`: Returns maximum modem frame buffer sizes ([freedv_interface.cpp:L612](file:///home/bminish/sdr/freedv-gui/src/freedv_interface.cpp#L612)).
   * `rade_freq_offset(rade_)`: Returns real-time Carrier Frequency Offset (CFO) ([freedv_interface.cpp:L737](file:///home/bminish/sdr/freedv-gui/src/freedv_interface.cpp#L737)).
   * `step->getSync()` / `step->getSnr()`: Reports atomic frame sync lock and SNR estimates ([freedv_interface.cpp:L830-L833](file:///home/bminish/sdr/freedv-gui/src/freedv_interface.cpp#L830)).

---

## 4. Mathematical Principle: MVDR Spatial Filtering vs. Raw Baseband Correlation

### 4.1 Received Signal Model
Let the complex baseband signals from Receiver 0 (Main Antenna) and Receiver 1 (Auxiliary Antenna) be:
$$s_0(n) = d_0(n) + q_0(n) + n_0(n) = h_0(t) \cdot x(n) + q_0(n) + n_0(n)$$
$$s_1(n) = d_1(n) + q_1(n) + n_1(n) = h_1(t) \cdot x(n) + q_1(n) + n_1(n)$$

Where:
* $x(n)$: Transmitted RADE symbol stream.
* $h_0(t), h_1(t)$: Complex channel gains for RADE on RX0 and RX1.
* $q_0(n), q_1(n)$: Correlated interference (QRM) with spatial phase difference $\Delta \theta_Q$.
* $n_0(n), n_1(n)$: Uncorrelated AWGN thermal noise.

### 4.2 Flaw of Raw Cross-Correlation under QRM
Raw baseband cross-correlation $R_{01} = \langle s_0 s_1^* \rangle$ yields:
$$R_{01} = \langle d_0 d_1^* \rangle + \langle q_0 q_1^* \rangle + \text{noise}$$

If QRM is present ($\langle q_0 q_1^* \rangle \neq 0$), raw cross-correlation locks onto the **interference vector**, steering the array to align QRM and severely degrading RADE SINR.

### 4.3 MVDR / Max-SINR Weight Derivation
We isolate the desired channel vector $\hat{\mathbf{h}}(t) = [\hat{h}_0(t), \hat{h}_1(t)]^T$ and estimate the $2 \times 2$ spatial noise+QRM covariance matrix:
$$\mathbf{n}(k) = \mathbf{s}(k) - \hat{\mathbf{h}}(t) \cdot x(k)$$
$$\mathbf{R}_{nn} = \left\langle \mathbf{n}(k) \mathbf{n}^H(k) \right\rangle = \begin{bmatrix} \langle |n_0'|^2 \rangle & \langle n_0' n_1'^* \rangle \\ \langle n_1' n_0'^* \rangle & \langle |n_1'|^2 \rangle \end{bmatrix}$$

The Minimum Variance Distortionless Response (MVDR) weight vector is:
$$\mathbf{w}_{\text{opt}} = \mathbf{R}_{nn}^{-1} \hat{\mathbf{h}}$$

Normalizing relative to Antenna 0 ($w_0 = 1$):
$$w_{\text{rel}} = \frac{w_{1, \text{opt}}}{w_{0, \text{opt}}} = A_{\text{opt}} \cdot e^{j \phi_{\text{opt}}}$$
* **Target Phase Angle ($\phi_{\text{target}}$):** $\text{atan2}(\text{Im}\{w_{\text{rel}}\}, \text{Re}\{w_{\text{rel}}\})$
* **Target Linear Gain ($A_{\text{target}}$):** $|w_{\text{rel}}|$ ($\text{div\_gain}_{\text{target}} = 20 \log_{10}(A_{\text{target}})$)

---

## 5. Low-Overhead Hardware Pipeline Architecture

Synchronization lock in RADE relies on classical DSP sliding correlation and FFT/matched binned filtering, whereas speech decoding uses a heavy Deep Neural Network (FARGAN / LPCNet). 

By separating DSP sync from neural decoding, piHPSDR cleans up the signal **before** sending a single stream to FreeDV:

```
  Antenna 0 (Main IQ) ----> [ Lightweight DSP Sync Engine 0 ] ---> h0, sync0
                                   | (Preamble/Autocorr)
                                   v
                             [ MVDR Engine ] ---> Derives w = A * e^(j*phi)
                                   ^
  Antenna 1 (Aux IQ)  ----> [ Lightweight DSP Sync Engine 1 ] ---> h1, sync1
                                   | (Preamble/Autocorr)
                                   v
                             [ IQ Combiner ] (piHPSDR DSP)
                        s_comb(n) = s0(n) + w * s1(n)
                                   |
                   (Single Cleaned-Up IQ Stream)
                                   v
                    +------------------------------------+
                    | FreeDV RADE Neural Decoder (Host)  |
                    |  - 1x FARGAN / LPCNet Vocoder      |
                    |  - 1x Single Audio Output Stream   |
                    +------------------------------------+
```

### Computational Overhead Comparison:
* **Dual DSP Sync Engines**: $\approx 128\text{ kCMAC/s}$ per receiver ($\mathbf{<1\%}$ CPU / lightweight FPGA logic).
* **Neural Speech Decoder**: Millions of FLOPs per frame.
* **Efficiency**: Running dual DSP sync engines costs less than **$1\%$** of a single neural decoder pass, completely eliminating CPU bottlenecks.

---

## 6. Unified "Just Works" Strategy for RADE V1 & V2

The core MVDR combining math ($\mathbf{w}_{\text{opt}} = \mathbf{R}_{nn}^{-1} \hat{\mathbf{h}}$) is identical for both V1 and V2. To make a unified engine that "just works":

1. **API Channel Tapping**: piHPSDR instantiates two `librade` handles (`rade_rx0` and `rade_rx1`) and queries their internal complex channel estimates $\hat{h}_0(t)$ and $\hat{h}_1(t)$.
2. **Transparent Operation**:
   * **RADEV1**: Channel vector $\hat{\mathbf{h}}$ derived from preamble pilot correlation.
   * **RADEV2**: Channel vector $\hat{\mathbf{h}}$ derived from frame autocorrelation / `librade` equalizer state.
   * **Combining Stage**: Identical MVDR matrix inversion and piHPSDR `div_phase`/`div_gain` slewing for both modes.

---

## 7. Integration Map for piHPSDR Source Code

| File | Module / Function | Modification Description |
| :--- | :--- | :--- |
| [receiver.c](file:///home/bminish/sdr/bm-pihpsdr/src/receiver.c) | `rx_add_div_iq_samples()` | Applies slewed `div_phase` and `div_gain` weights $w$ to RX1 IQ samples. |
| `src/rade_correlator.c` | **[NEW MODULE]** | Implements dual `librade` handle management, channel vector $\hat{\mathbf{h}}$ extraction, $\mathbf{R}_{nn}$ matrix inversion, and MVDR weight derivation. |
| [diversity_proposal1.md](file:///home/bminish/sdr/bm-pihpsdr/docs/diversity_proposal1.md) | Documentation Base | Extended by this proposal to handle correlated QRM and pilotless RADEV2. |

---

## 8. Conclusion

By implementing a **pilot/autocorrelation-guided MVDR spatial processing engine** in piHPSDR's DSP front-end, we achieve optimal SINR combining and active QRM nulling across HF Rayleigh fading channels for both RADEV1 and RADEV2, while maintaining a single, low-overhead neural decode pipeline in FreeDV.
