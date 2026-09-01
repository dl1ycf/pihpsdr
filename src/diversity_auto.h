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
  DIV_AUTO_SUM,       // co-phase the two antennas (maximum ratio combining)
  DIV_AUTO_BEST       // use whichever antenna has the better SNR
};

//
// New modes go on the end, for the same reason new references do: the
// value is what lands in the props file.
//

//
// div_auto_ref: what part of the spectrum the decision is taken from
//
enum {
  DIV_REF_BAND = 0,   // all bins in the analysis window ("A")
  DIV_REF_CARRIER,    // the carrier bin only, found by our own tracker ("B")
  DIV_REF_RADE_V1,    // RADE V1 pilot correlation + MVDR
  DIV_REF_DIGITAL_IQ  // occupied bins in the window, split, then MVDR
};

//
// New references go on the end. The value is what is written to the
// props file, so inserting one in the middle - or removing one, as the
// RADE passband reference was - silently changes what an existing file
// means. diversity_auto_restore_state() migrates instead; see
// DIV_REF_SCHEME there.
//

extern int    div_auto_mode;
extern int    div_auto_ref;
extern int    div_auto_follow_filter;   // analysis window follows the RX filter
extern double div_auto_centre;          // window centre (Hz, rel. to tuned freq)
extern double div_auto_width;           // window width (Hz)
extern double div_auto_tau;             // adaptation time constant (seconds)
extern double div_auto_hang;            // hold a RADE lock this long after the
                                        // pilot goes away, before re-acquiring
extern double div_auto_coherence_min;   // hold below this coherence
extern int    div_auto_weighting;       // DIV_WEIGHT_FLAT / _COHERENCE
extern double div_auto_resolution;      // requested bin width, Hz

//
// The window controls are modal: the Window, Carrier and Digital I/Q
// references each keep their own pair. div_auto_centre/width are the
// active pair.
//
extern double div_band_centre;
extern double div_band_width;
extern double div_carrier_centre;
extern double div_carrier_width;
extern double div_digital_centre;
extern double div_digital_width;

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

//
// Which antenna is carrying the better signal-to-noise ratio, and by how
// much. Positive means ADC1. Every reference estimates it - it is the
// decision DIV_AUTO_BEST acts on - and it is worth showing whatever mode
// is running, because nothing else an operator can see distinguishes an
// antenna that is 12 dB down because it is deaf from one that is 12 dB
// down because it is quiet. The two want opposite weights.
//
// _valid is 0 until the reference has something to judge on: a
// measurement of the signal on both arms and a noise floor to divide it
// by. _pick is what DIV_AUTO_BEST last decided, 0 for ADC0 and 1 for
// ADC1, and is meaningless while _valid is 0.
//
extern double div_auto_arm_db;
extern int    div_auto_arm_valid;
extern int    div_auto_arm_pick;
extern int    div_auto_carrier_valid;   // 1 once the tracker has an estimate

//
// Digital I/Q: the span of the bins found occupied inside the search
// region, in the shifted frame, for the overlay and the status line.
// Not the accumulation set - that is a per-bin mask, and the quiet gap
// between an FSK mark and space lies inside this span while counting as
// noise. See div_digital_solve().
//
extern double div_auto_occ_lo;
extern double div_auto_occ_hi;
extern int    div_auto_occ_valid;

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
// Where zero is for a hand-placed window, in the shifted frame that
// div_auto_centre and the RX filter edges are both expressed in. Zero
// everywhere but CW, where the sidetone is folded into the passband and
// the zero-beat note therefore sits at +pitch (CWU) or -pitch (CWL).
// The panadapter overlay places the drawn window with it.
//
extern double div_window_zero(int mode, int sidetone);

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

//
// MVDR for a two-element array: w = R^-1 h, normalised so arm 0 carries
// unity and expressed as the weight the combiner applies to arm 1.
//
// R = [[r00, r01], [conj(r01), r11]] is the interference-plus-noise
// covariance and h = [h0, h1] the channel. Shared with the RADE V1
// correlator, which reaches the same two matrices by a different route.
//
extern void div_mvdr2(double r00, double r11, double r01re, double r01im,
                      double h0re, double h0im, double h1re, double h1im,
                      double *wr, double *wi);

extern void diversity_auto_start(void);
extern void diversity_auto_stop(void);
extern void diversity_auto_restart(void);   // stop + start if it should be running
extern void diversity_auto_reset(void);     // forget the accumulated statistics
extern void diversity_auto_sample(double i0, double q0, double i1, double q1);
extern void diversity_auto_gap(void);       // transmit gap in the sample stream
//
// The loop's settings, and what it is currently measuring, as plain data.
//
// The analysis runs on the radio only, but the operator may be sitting at
// a remote client, so both have to travel. Settings go client -> server
// ("apply this"), and server -> client on connect and whenever the
// radio's own panel changes something ("adopt this"). Status goes server
// -> client on a timer.
//
// These are the transport form. The engine still works from the div_auto_*
// globals - every consumer already reads those - so a client writes what
// arrives straight into them and its status line and panadapter overlay
// light up with no further plumbing.
//
typedef struct _div_settings {
  int    mode;              // DIV_AUTO_*
  int    ref;               // DIV_REF_*
  int    follow_filter;
  int    weighting;
  int    hold;
  double centre, width;
  double tau, hang, coherence_min, resolution;
  //
  // The per-reference window pairs travel too. They are modal state the
  // operator built up, not derived values, so a client that only sent the
  // live pair would silently flatten them on the radio.
  //
  double band_centre, band_width;
  double carrier_centre, carrier_width;
  double digital_centre, digital_width;
} DIV_SETTINGS;

typedef struct _div_status {
  int    enabled;           // the whole feature, as the radio has it
  int    indep_att;         // ADC1 keeps its own step attenuator
  int    att0, att1;        // the two step attenuators, as the radio has them
  int    running, holding, clamped;
  int    arm_valid, arm_pick, carrier_valid, occ_valid;
  int    rade_locked, rade_confirming, rade_side;
  double binhz, coherence, carrier, arm_db;
  double occ_lo, occ_hi;
  double gain, phase, track_gain, track_phase;
  double rade_quality;
} DIV_STATUS;

//
// No action, or the one control that changes no setting and so cannot be
// expressed as a difference between two settings blocks.
//
#define DIV_ACTION_NONE   0
#define DIV_ACTION_RESET  1

extern void diversity_auto_get_settings(DIV_SETTINGS *s);
extern void diversity_auto_apply_settings(const DIV_SETTINGS *s, int action);
extern void diversity_auto_get_status(DIV_STATUS *st);
extern void diversity_auto_apply_status(const DIV_STATUS *st);

//
// g_idle_add() targets for the client read loop: adopt a settings block
// the radio sent, and adopt a status push. Both take ownership of the
// message they are given.
//
extern gboolean diversity_client_set_settings(gpointer data);
extern gboolean diversity_client_set_status(gpointer data);

//
// The operator changed mode on RX0.
//
// The loop's settings are modal - one block per group of modes, so that
// the reference, window and objective set up for AM are still there when
// the operator comes back to it from SSB - and this is where one block is
// filed away and the next adopted. Called on the radio only; a client is
// sent the outcome like any other settings change.
//
extern void diversity_auto_mode_changed(int mode);

//
// One of the two ADC step attenuators is about to move by delta_db. Feeds
// the known step forward into the weight so the combined audio does not
// jump; the statistics restart by themselves on the next block, since the
// attenuations are part of the analysis context.
//
extern void diversity_auto_att_changed(int a, int delta_db);

extern void diversity_auto_save_state(void);
extern void diversity_auto_restore_state(void);

#endif
