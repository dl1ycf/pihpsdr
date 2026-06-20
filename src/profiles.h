/* Copyright (C)
* 2026 - Christoph van Wüllen, DL1YCF
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

#ifndef _PROFILES_H_
#define _PROFILES_H_

#include <gtk/gtk.h>

#include "receiver.h"
#include "transmitter.h"

enum {
  PROF_AUDIO = MODES,
  PROF_RAGCHEW,
  PROF_CONTEST,
  PROF_DX,
  PROF_DIGI,
  PROF_USER1,
  PROF_USER2,
  PROF_USER3
};

struct _rxprofile {
  //
  // VFO setting
  //
  int    filter;                    // actual filter used
  int    cwPeak;                    // CW peak filter on/off
  int    rit_step;                  // RIT step size
  long long step;                   // VFO step size
  //
  // RX (noise, EQ, AGC) settings
  //
  double volume;                    // in dB, -40 .. 0
  int    nb;                        // Noise blanker (0..2)
  double nb_tau;                    // NB parameters
  double nb_hang;
  double nb_advtime;
  double nb_thresh;
  int    nb2_mode;
  int    nr;                        // Noise reduction (0..4)
  int    nr_agc;                    // NR parameters
  int    nr2_gain_method;
  int    nr2_npe_method;
  int    nr2_post;
  int    nr2_post_taper;
  int    nr2_post_nlevel;
  int    nr2_post_factor;
  int    nr2_post_rate;
  double nr2_trained_threshold;
  double nr2_trained_t2;
  double nr4_reduction_amount;      // NR4 parameters
  double nr4_smoothing_factor;
  double nr4_whitening_factor;
  double nr4_noise_rescale;
  double nr4_post_threshold;
  int    nr4_noise_scaling_type;
  int squelch_enable;               // Squelch on/off
  double squelch;                   // squelch value
  int anf;                          // Automatic notch filter
  int anf_taps;
  int anf_delay;
  double anf_gain;
  double anf_leakage;
  int snb;                          // Spectral noise blanker
  int agc;                          // AGC characteristics (slow/medium/fast etc.)
  int agc_custom_attack;
  int agc_custom_decay;
  int agc_custom_hang;
  int agc_custom_slope;
  int fm_limiter;
  double fm_limiter_gain;
  int en_eq;                        // RX equaliser on/off
  double eq_freq[11];             // RX equaliser settings
  double eq_gain[11];
  //
  // Local audio settings
  //
  int audio_channel;
  int local_audio;               //  RX local audio
  char audio_name[128];          //  RX local audio device name
};

struct _txprofile {
  //
  // TX (EQ, CMPR, DEXP, CRC) settings
  //
  int en_eq;                        // TX equaliser on/off
  int compressor;                   // TX compressor on/off
  double compressor_level;          // TX compressor level
  double mic_gain;                  // TX mic gain
  int dexp;                         // Downward Expander (DEXP) on/off
  int dexp_trigger;                 // DEXP trigger level (dB)
  double dexp_tau;                  // DEXP averaging time constant
  double dexp_attack;               // DEXP OpenGate width
  double dexp_release;              // DEXP CloseGate width
  double dexp_hold;                 // DEXP "gate open with no signal" time
  int dexp_exp;                     // DEXP expansion ration (dB)
  double dexp_hyst;                 // DEXP hysteresis ratio
  int dexp_filter;                  // DEXP side channel filter on/off
  int dexp_filter_low;              // DEXP side channel filter low-cut
  int dexp_filter_high;             // DEXP side channel filter high-cut
  int cfc;                          // Continuous Frequency Compressor (CFC) on/off
  int cfc_eq;                       // CFC post-equaliser on/off
  int default_filter_low;
  int default_filter_high;
  int use_rx_filter;
  double eq_freq[11];               // TX equaliser settings
  double eq_gain[11];
  double cfc_freq[11];              // CFC corner frequencies
  double cfc_lvl[11];               // CFC compression at corner frequency
  double cfc_post[11];              // CFC post-EQ gain at corner frequency

  int phrot_enable;
  int phrot_reverse;
  int phrot_stages;
  double phrot_corner;
  //
  // Local audio settings
  //
  int local_audio;               //  TX local audio input
  char audio_name[128];          //  TX local audio device name
};

//
// Store settings on a per-mode basis for settings that
// are likely to be "bound" to a mode rather than to a band
// If a mode changes, these settings are restored to what
// has been effective the last time the "new" mode has
// been used.
//
//
struct _rxtxprofile {
  struct _rxprofile rx;
  struct _txprofile tx;
};

#define NUMPROFILES 8

extern struct _rxtxprofile   RXTXprofile[];

extern void profiles_save_state(void);
extern void profiles_restore_state(void);
extern void profiles_copy_rxtxprofile(int mode);
extern void profiles_load_rx_profile(RECEIVER *rx, int m);
extern void profiles_load_tx_profile(TRANSMITTER *tx, int m);
extern void profiles_load_rxtx_profile(RECEIVER *rx);
extern void profiles_save_rx_profile(RECEIVER *rx, int m);
extern void profiles_save_tx_profile(TRANSMITTER *tx, int m);

#endif
