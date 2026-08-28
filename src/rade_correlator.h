/* Copyright (C)
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#ifndef _RADE_CORRELATOR_H_
#define _RADE_CORRELATOR_H_

//
// Pilot-guided channel estimation and MVDR combining for FreeDV RADE V1.
//
// RADE V1 sends one known pilot symbol per modem frame. Correlating both
// antenna streams against that pilot isolates the *wanted* signal from
// everything else, which is what lets us estimate the interference
// covariance separately and steer a null onto QRM instead of onto the
// signal we are trying to receive.
//
// Waveform parameters, from the radae sources (rade_dsp.h, rade_ofdm.c):
//
//   Fs   8000 Hz     modem sample rate
//   Nc   30          OFDM carriers, 750 ... 2200 Hz (centred on 1500)
//   M    160         samples per symbol
//   Ncp  32          cyclic prefix
//   Ns   4           data symbols per modem frame
//   Nmf  960         samples per modem frame  = 120 ms
//
// The frame is one pilot symbol followed by four data symbols, so the
// pilot repeats 8.33 times a second. The pilot symbols themselves are
// Barker-13 over the carriers, scaled by sqrt(2), which is reproduced
// here rather than pulled in from librade - see rade_corr_make_pilot().
//
#define RADE_CORR_FS      8000
#define RADE_CORR_NC      30
#define RADE_CORR_M       160
#define RADE_CORR_NCP     32
#define RADE_CORR_NS      4
#define RADE_CORR_NMF     ((RADE_CORR_NS + 1) * (RADE_CORR_M + RADE_CORR_NCP))

//
// Lowest and highest audio frequency occupied by the modem, in Hz. Used
// to place the analysis window in the wideband ("stage 1") RADE mode.
//
#define RADE_CORR_FLO     750.0
#define RADE_CORR_FHI     2200.0

//
// Status, for the UI. Written by the analysis thread.
//
extern int    rade_corr_locked;      // pilot acquired, confirmed and tracking
extern int    rade_corr_confirming;  // a candidate is on probation
extern double rade_corr_freq_off;    // Hz, carrier frequency offset
extern double rade_corr_snr;         // dB, pilot SNR estimate
extern double rade_corr_quality;     // 0..1, normalised pilot correlation
extern int    rade_corr_mirrored;    // which spectral sense actually correlated

//
// ddc_rate must divide RADE_CORR_FS exactly (48k/96k/192k/384k all do).
// Returns 0 if the correlator cannot run at this rate.
//
extern int  rade_corr_start(int ddc_rate);
extern void rade_corr_stop(void);
extern void rade_corr_reset(void);

//
// Feed one block of n sample pairs at the DDC rate.
//
// expect_bank is the pilot bank the operator's sideband implies - 0 for a
// modem above the tuned frequency, 1 for below, -1 when the passband does
// not say. It is a prior, not a constraint: both banks are still searched
// and the other one is taken if it wins by RADE_BANK_MARGIN.
//
// frame_off is the offset of WDSP's shifted frame from the raw DDC one,
// in Hz - vfo[0].offset with the CW sidetone folded in. See the frequency
// bookkeeping note in diversity_auto.c.
//
// tau is the operator's averaging time in seconds, which sets how fast the
// channel and covariance estimates follow the path.
//
// Returns 1 when a new weight is available in *wr/*wi, in which case it
// is expressed in the same sense as div_cos/div_sin, i.e. ready to be
// applied as z0 + w*z1. Nothing is produced while a candidate is on
// probation.
//
extern int rade_corr_process(const float *arm0, const float *arm1, int n,
                             int expect_bank, double frame_off, double tau,
                             double *wr, double *wi);

#endif
