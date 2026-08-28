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

#ifndef _DIVERSITY_AUTO_H_
#define _DIVERSITY_AUTO_H_

//
// Automatic determination of the DIVERSITY gain/phase.
//
// The combination performed in rx_add_div_iq_samples() is
//
//     z = z0 + w * z1,     w = div_cos + j*div_sin
//
// with z0 taken from ADC0 (main antenna) and z1 from ADC1 (aux antenna).
// This module estimates a "good" w by looking at the cross spectrum of
// the two raw streams, and writes the result back into div_cos/div_sin
// (and, back-computed, div_gain/div_phase).
//
// The weight applied to the samples is a single complex number valid for
// the whole DDC passband, but the *decision* is taken from a narrow
// analysis window only, so that a strong signal outside the window
// cannot run away with the solution.
//

//
// div_auto_mode
//
enum {
  DIV_AUTO_OFF = 0,   // manual gain/phase only
  DIV_AUTO_NULL,      // minimise the correlated component (noise cancelling)
  DIV_AUTO_SUM        // co-phase the two antennas (maximum ratio combining)
};

//
// div_auto_ref: what part of the spectrum the decision is taken from
//
enum {
  DIV_REF_BAND = 0,   // all bins in the analysis window ("A")
  DIV_REF_CARRIER,    // the carrier bin only, found by our own tracker ("B")
  DIV_REF_RADE_BAND,  // window auto-placed on the FreeDV RADE passband
  DIV_REF_RADE_V1     // RADE V1 pilot correlation + MVDR
};

//
// True for the reference modes that place themselves on the RADE
// passband. Which side of the carrier that is gets measured, not derived
// from the mode - see div_mode_is_lsb() in diversity_auto.c.
//
#define DIV_REF_IS_RADE(r)  ((r) == DIV_REF_RADE_BAND || (r) == DIV_REF_RADE_V1)

extern int    div_auto_mode;
extern int    div_auto_ref;
extern int    div_auto_follow_filter;   // analysis window follows the RX filter
extern double div_auto_centre;          // window centre (Hz, rel. to tuned freq)
extern double div_auto_width;           // window width (Hz)
extern double div_auto_tau;             // adaptation time constant (seconds)
extern double div_auto_coherence_min;   // hold below this coherence
extern int    div_auto_weighting;       // DIV_WEIGHT_FLAT / _COHERENCE
extern double div_auto_resolution;      // requested bin width, Hz

//
// The window controls are modal: the Window and Carrier references each
// keep their own pair. div_auto_centre/width are the active pair.
//
extern double div_band_centre;
extern double div_band_width;
extern double div_carrier_centre;
extern double div_carrier_width;

//
// Status: the window had to be clamped to the Nyquist limit, and the bin
// width actually achieved.
//
extern int    div_auto_clamped;
extern double div_auto_binhz;

//
// Bin weighting for the wideband window.
//
enum {
  DIV_WEIGHT_FLAT = 0,
  DIV_WEIGHT_COHERENCE
};

//
// Read-only status, updated by the analysis thread
//
extern double div_auto_coherence;       // 0 ... 1, last estimate
extern int    div_auto_holding;         // 1 if the loop is holding (no update)
extern double div_auto_carrier;         // Hz, smoothed carrier estimate
extern int    div_auto_carrier_valid;   // 1 once the tracker has an estimate

//
// div_auto_running is read once per sample by rx_add_div_iq_samples(),
// so it is kept as a plain int that is only ever set by the functions below.
//
extern int    div_auto_running;

//
// +1 if the RADE modem was found above the tuned carrier, -1 below.
//
extern int  div_rade_side_get(void);

//
// Operator hold: the analysis keeps running and keeps updating
// div_track_gain/div_track_phase, but stops writing the weight, so the
// manual gain and phase controls have it. Releasing applies the tracked
// answer in one step.
//
extern int    div_auto_hold;
extern double div_track_gain;           // dB, where the loop has got to
extern double div_track_phase;          // degrees
extern void   diversity_auto_set_hold(int on);

//
// Swap Null for Sum. Turns the weight in force through 180 degrees at
// once - whether or not the loop is currently applying anything, and
// whether or not Hold is set - as well as changing which answer the loop
// computes from here on. The next update from the loop is then applied in
// one step rather than slewed to.
//
extern void diversity_auto_invert(void);

extern void diversity_auto_start(void);
extern void diversity_auto_stop(void);
extern void diversity_auto_restart(void);   // stop + start if it should be running
extern void diversity_auto_reset(void);     // forget the accumulated statistics
extern void diversity_auto_sample(double i0, double q0, double i1, double q1);
extern void diversity_auto_save_state(void);
extern void diversity_auto_restore_state(void);

#endif
