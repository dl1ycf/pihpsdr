/* Copyright (C)
*  2026 - Christoph van Wüllen, DL1YCF
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

#include <gtk/gtk.h>

#include "agc.h"
#include "audio.h"
#include "ext.h"
#include "filter.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "profiles.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"

struct _rxtxprofile RXTXprofile[MODES + NUMPROFILES];

void profiles_save_state(void) {
  for (int i = 0; i < MODES; i++) {
    //
    // only local audio settings are relevant on the client side
    //
    SetPropI1("modeset.%d.rx_audio_channel", i,       RXTXprofile[i].rx.audio_channel);
    SetPropI1("modeset.%d.rx_local_audio", i,         RXTXprofile[i].rx.local_audio);
    SetPropS1("modeset.%d.rx_audio_name", i,          RXTXprofile[i].rx.audio_name);
    SetPropI1("modeset.%d.tx_local_audio", i,         RXTXprofile[i].tx.local_audio);
    SetPropS1("modeset.%d.tx_audio_name", i,          RXTXprofile[i].tx.audio_name);
    if (!radio_is_remote) {
      SetPropI1("modeset.%d.filter", i,                 RXTXprofile[i].rx.filter);
      SetPropI1("modeset.%d.cwPeak", i,                 RXTXprofile[i].rx.cwPeak);
      SetPropI1("modeset.%d.step", i,                   RXTXprofile[i].rx.step);
      SetPropI1("modeset.%d.rit_step", i,               RXTXprofile[i].rx.rit_step);
      //
      SetPropF1("modeset.%d.rxvolume", i,               RXTXprofile[i].rx.volume);
      SetPropI1("modeset.%d.nb", i,                     RXTXprofile[i].rx.nb);
      SetPropF1("modeset.%d.nb_tau", i,                 RXTXprofile[i].rx.nb_tau);
      SetPropF1("modeset.%d.nb_hang", i,                RXTXprofile[i].rx.nb_hang);
      SetPropF1("modeset.%d.nb_advtime", i,             RXTXprofile[i].rx.nb_advtime);
      SetPropF1("modeset.%d.nb_thresh", i,              RXTXprofile[i].rx.nb_thresh);
      SetPropI1("modeset.%d.nb2_mode", i,               RXTXprofile[i].rx.nb2_mode);
      SetPropI1("modeset.%d.nr", i,                     RXTXprofile[i].rx.nr);
      SetPropI1("modeset.%d.nr_agc", i,                 RXTXprofile[i].rx.nr_agc);
      SetPropI1("modeset.%d.nr2_gain_method", i,        RXTXprofile[i].rx.nr2_gain_method);
      SetPropI1("modeset.%d.nr2_npe_method", i,         RXTXprofile[i].rx.nr2_npe_method);
      SetPropI1("modeset.%d.nr2_post", i,               RXTXprofile[i].rx.nr2_post);
      SetPropI1("modeset.%d.nr2_post_taper", i,         RXTXprofile[i].rx.nr2_post_taper);
      SetPropI1("modeset.%d.nr2_post_nlevel", i,        RXTXprofile[i].rx.nr2_post_nlevel);
      SetPropI1("modeset.%d.nr2_post_factor", i,        RXTXprofile[i].rx.nr2_post_factor);
      SetPropI1("modeset.%d.nr2_post_rate", i,          RXTXprofile[i].rx.nr2_post_rate);
      SetPropF1("modeset.%d.nr2_trained_threshold", i,  RXTXprofile[i].rx.nr2_trained_threshold);
      SetPropF1("modeset.%d.nr2_trained_t2", i,         RXTXprofile[i].rx.nr2_trained_t2);
      SetPropF1("modeset.%d.nr4_reduction_amount", i,   RXTXprofile[i].rx.nr4_reduction_amount);
      SetPropF1("modeset.%d.nr4_smoothing_factor", i,   RXTXprofile[i].rx.nr4_smoothing_factor);
      SetPropF1("modeset.%d.nr4_whitening_factor", i,   RXTXprofile[i].rx.nr4_whitening_factor);
      SetPropF1("modeset.%d.nr4_noise_rescale", i,      RXTXprofile[i].rx.nr4_noise_rescale);
      SetPropF1("modeset.%d.nr4_post_threshold", i,     RXTXprofile[i].rx.nr4_post_threshold);
      SetPropI1("modeset.%d.nr4_noise_scaling_type", i, RXTXprofile[i].rx.nr4_noise_scaling_type);
      SetPropI1("modeset.%d.en_squelch", i,             RXTXprofile[i].rx.squelch_enable);
      SetPropF1("modeset.%d.squelch", i,                RXTXprofile[i].rx.squelch);
      SetPropI1("modeset.%d.anf", i,                    RXTXprofile[i].rx.anf);
      SetPropI1("modeset.%d.anf_taps", i,               RXTXprofile[i].rx.anf_taps);
      SetPropI1("modeset.%d.anf_delay", i,              RXTXprofile[i].rx.anf_delay);
      SetPropF1("modeset.%d.anf_gain", i,               RXTXprofile[i].rx.anf_gain);
      SetPropF1("modeset.%d.anf_leakage", i,            RXTXprofile[i].rx.anf_leakage);
      SetPropI1("modeset.%d.snb", i,                    RXTXprofile[i].rx.snb);
      SetPropI1("modeset.%d.agc", i,                    RXTXprofile[i].rx.agc);
      SetPropI1("modeset.%d.agc_custom_attack", i,      RXTXprofile[i].rx.agc_custom_attack);
      SetPropI1("modeset.%d.agc_custom_decay", i,       RXTXprofile[i].rx.agc_custom_decay);
      SetPropI1("modeset.%d.agc_custom_hang", i,        RXTXprofile[i].rx.agc_custom_hang);
      SetPropI1("modeset.%d.agc_custom_slope", i,       RXTXprofile[i].rx.agc_custom_slope);
      SetPropI1("modeset.%d.fm_limiter", i,             RXTXprofile[i].rx.fm_limiter);
      SetPropF1("modeset.%d.fm_limiter_gain", i,        RXTXprofile[i].rx.fm_limiter_gain);
      SetPropI1("modeset.%d.en_rxeq", i,                RXTXprofile[i].rx.en_eq);
      for (int j = 0; j < 11; j++) {
        SetPropF2("modeset.%d.rxeq.%d", i, j,           RXTXprofile[i].rx.eq_gain[j]);
        SetPropF2("modeset.%d.rxeqfrq.%d", i, j,        RXTXprofile[i].rx.eq_freq[j]);
      }
      //
      // TX profile
      //
      SetPropI1("modeset.%d.ptt_delay", i,              RXTXprofile[i].tx.ptt_delay);
      SetPropI1("modeset.%d.en_txeq", i,                RXTXprofile[i].tx.en_eq);
      SetPropI1("modeset.%d.compressor", i,             RXTXprofile[i].tx.compressor);
      SetPropF1("modeset.%d.compressor_level", i,       RXTXprofile[i].tx.compressor_level);
      SetPropF1("modeset.%d.mic_gain", i,               RXTXprofile[i].tx.mic_gain);
      SetPropI1("modeset.%d.dexp", i,                   RXTXprofile[i].tx.dexp);
      SetPropI1("modeset.%d.dexp_trigger", i,           RXTXprofile[i].tx.dexp_trigger);
      SetPropF1("modeset.%d.dexp_tau", i,               RXTXprofile[i].tx.dexp_tau);
      SetPropF1("modeset.%d.dexp_attack", i,            RXTXprofile[i].tx.dexp_attack);
      SetPropF1("modeset.%d.dexp_release", i,           RXTXprofile[i].tx.dexp_release);
      SetPropF1("modeset.%d.dexp_hold", i,              RXTXprofile[i].tx.dexp_hold);
      SetPropI1("modeset.%d.dexp_exp", i,               RXTXprofile[i].tx.dexp_exp);
      SetPropF1("modeset.%d.dexp_hyst", i,              RXTXprofile[i].tx.dexp_hyst);
      SetPropI1("modeset.%d.dexp_filter", i,            RXTXprofile[i].tx.dexp_filter);
      SetPropI1("modeset.%d.dexp_filter_low", i,        RXTXprofile[i].tx.dexp_filter_low);
      SetPropI1("modeset.%d.dexp_filter_high", i,       RXTXprofile[i].tx.dexp_filter_high);
      SetPropI1("modeset.%d.cfc", i,                    RXTXprofile[i].tx.cfc);
      SetPropI1("modeset.%d.cfc_eq", i,                 RXTXprofile[i].tx.cfc_eq);
      SetPropI1("modeset.%d.tx_default_filter_low", i,  RXTXprofile[i].tx.default_filter_low);
      SetPropI1("modeset.%d.tx_default_filter_high", i, RXTXprofile[i].tx.default_filter_high);
      SetPropI1("modeset.%d.use_rx_filter", i,          RXTXprofile[i].tx.use_rx_filter);
      SetPropI1("modeset.%d.tx_phrot_enable", i,        RXTXprofile[i].tx.phrot_enable);
      SetPropI1("modeset.%d.tx_phrot_stages", i,        RXTXprofile[i].tx.phrot_stages);
      SetPropF1("modeset.%d.tx_phrot_corner", i,        RXTXprofile[i].tx.phrot_corner);
      SetPropI1("modeset.%d.tx_phrot_reverse", i,       RXTXprofile[i].tx.phrot_reverse);
      for (int j = 0; j < 11; j++) {
        SetPropF2("modeset.%d.txeq.%d", i, j,           RXTXprofile[i].tx.eq_gain[j]);
        SetPropF2("modeset.%d.txeqfrq.%d", i, j,        RXTXprofile[i].tx.eq_freq[j]);
        SetPropF2("modeset.%d.cfc_frq.%d", i, j,        RXTXprofile[i].tx.cfc_freq[j]);
        SetPropF2("modeset.%d.cfc_lvl.%d", i, j,        RXTXprofile[i].tx.cfc_lvl[j]);
        SetPropF2("modeset.%d.cfc_post.%d", i, j,       RXTXprofile[i].tx.cfc_post[j]);
      }
    }
  }
}

void profiles_restore_state(void) {
  for (int i = 0; i < MODES + NUMPROFILES; i++) {
    //
    // Restore only local audio settings on the client side
    //
    RXTXprofile[i].rx.audio_channel =  STEREO;
    RXTXprofile[i].rx.local_audio =  0;
    snprintf(RXTXprofile[i].rx.audio_name, sizeof(RXTXprofile[i].rx.audio_name), "%s", "NO AUDIO");
    RXTXprofile[i].tx.local_audio =  0;
    snprintf(RXTXprofile[i].tx.audio_name, sizeof(RXTXprofile[i].tx.audio_name), "%s", "NO AUDIO");
    GetPropI1("modeset.%d.rx_audio_channel", i,       RXTXprofile[i].rx.audio_channel);
    GetPropI1("modeset.%d.rx_local_audio", i,         RXTXprofile[i].rx.local_audio);
    GetPropI1("modeset.%d.tx_local_audio", i,         RXTXprofile[i].tx.local_audio);
    GetPropS1("modeset.%d.rx_audio_name", i,          RXTXprofile[i].rx.audio_name);
    GetPropS1("modeset.%d.tx_audio_name", i,          RXTXprofile[i].tx.audio_name);
    if (!radio_is_remote) {
      //
      // set defaults that are the same  for all modes
      //
      RXTXprofile[i].rx.filter = filterF3;
      RXTXprofile[i].rx.cwPeak = 0;
      RXTXprofile[i].rx.rit_step =  10;
      RXTXprofile[i].rx.step     = 100;
      //
      RXTXprofile[i].rx.volume = -20.0;
      RXTXprofile[i].rx.nb = 0;
      RXTXprofile[i].rx.nb_tau = 0.00001;
      RXTXprofile[i].rx.nb_advtime = 0.00001;
      RXTXprofile[i].rx.nb_hang = 0.00001;
      RXTXprofile[i].rx.nb_thresh = 4.95;
      RXTXprofile[i].rx.nb2_mode = 0;
      RXTXprofile[i].rx.nr = 0;
      RXTXprofile[i].rx.nr_agc = 0;
      RXTXprofile[i].rx.nr2_gain_method = 0;
      RXTXprofile[i].rx.nr2_npe_method = 0;
      RXTXprofile[i].rx.nr2_post = 0;
      RXTXprofile[i].rx.nr2_post_taper = 12;
      RXTXprofile[i].rx.nr2_post_nlevel = 15;
      RXTXprofile[i].rx.nr2_post_factor = 15;
      RXTXprofile[i].rx.nr2_post_rate = 5;
      RXTXprofile[i].rx.nr2_trained_threshold = -0.5;
      RXTXprofile[i].rx.nr2_trained_t2 = 0.2;
      RXTXprofile[i].rx.nr4_reduction_amount = 10.0;
      RXTXprofile[i].rx.nr4_smoothing_factor = 20.0;
      RXTXprofile[i].rx.nr4_whitening_factor = 0.0;
      RXTXprofile[i].rx.nr4_noise_rescale = 2.0;
      RXTXprofile[i].rx.nr4_post_threshold = -3.0;
      RXTXprofile[i].rx.nr4_noise_scaling_type = 0;
      RXTXprofile[i].rx.squelch_enable = 0;
      RXTXprofile[i].rx.squelch = 0.0;
      RXTXprofile[i].rx.anf = 0;
      RXTXprofile[i].rx.anf_taps = 64;
      RXTXprofile[i].rx.anf_delay = 16;
      RXTXprofile[i].rx.anf_gain = -80.0;
      RXTXprofile[i].rx.anf_leakage = -20.0;
      RXTXprofile[i].rx.snb = 0;
      RXTXprofile[i].rx.agc      = AGC_MEDIUM;
      RXTXprofile[i].rx.agc_custom_attack = 1;
      RXTXprofile[i].rx.agc_custom_decay  = 250;
      RXTXprofile[i].rx.agc_custom_hang   = 250;
      RXTXprofile[i].rx.agc_custom_slope  = 250;
      RXTXprofile[i].rx.fm_limiter = 0;
      RXTXprofile[i].rx.fm_limiter_gain = 10.0;
      RXTXprofile[i].rx.en_eq = 0;
      RXTXprofile[i].rx.eq_freq[0]  =     0.0;
      RXTXprofile[i].rx.eq_freq[1]  =    50.0;
      RXTXprofile[i].rx.eq_freq[3]  =   200.0;
      RXTXprofile[i].rx.eq_freq[4]  =   500.0;
      RXTXprofile[i].rx.eq_freq[5]  =  1000.0;
      RXTXprofile[i].rx.eq_freq[6]  =  1500.0;
      RXTXprofile[i].rx.eq_freq[7]  =  2000.0;
      RXTXprofile[i].rx.eq_freq[8]  =  2500.0;
      RXTXprofile[i].rx.eq_freq[9]  =  3000.0;
      RXTXprofile[i].rx.eq_freq[10] =  5000.0;
      for (int j = 0; j < 11; j++) {
        RXTXprofile[i].rx.eq_gain[j] = 0;
      }
      //
      // TX defaults
      //
      RXTXprofile[i].tx.ptt_delay = 0;
      RXTXprofile[i].tx.en_eq = 0;
      RXTXprofile[i].tx.compressor = 0;
      RXTXprofile[i].tx.compressor_level = 0.0;
      RXTXprofile[i].tx.mic_gain = 0.0;
      RXTXprofile[i].tx.dexp = 0;
      RXTXprofile[i].tx.dexp_trigger = -25;
      RXTXprofile[i].tx.dexp_tau = 0.01;
      RXTXprofile[i].tx.dexp_attack = 0.025;
      RXTXprofile[i].tx.dexp_release = 0.100;
      RXTXprofile[i].tx.dexp_hold = 0.800;
      RXTXprofile[i].tx.dexp_exp = 20;
      RXTXprofile[i].tx.dexp_hyst = 0.75;
      RXTXprofile[i].tx.dexp_filter = 0;
      RXTXprofile[i].tx.dexp_filter_low = 1000;
      RXTXprofile[i].tx.dexp_filter_high = 2000;
      RXTXprofile[i].tx.cfc = 0;
      RXTXprofile[i].tx.cfc_eq = 0;
      RXTXprofile[i].tx.default_filter_low = 150;
      RXTXprofile[i].tx.default_filter_high = 2850;
      RXTXprofile[i].tx.use_rx_filter  = 0;
      RXTXprofile[i].tx.phrot_enable = 0;
      RXTXprofile[i].tx.phrot_reverse = 0;
      RXTXprofile[i].tx.phrot_stages = 2;
      RXTXprofile[i].tx.phrot_corner = 600.0;
      RXTXprofile[i].tx.eq_freq[0]  =     0.0;
      RXTXprofile[i].tx.eq_freq[1]  =    50.0;
      RXTXprofile[i].tx.eq_freq[3]  =   200.0;
      RXTXprofile[i].tx.eq_freq[4]  =   500.0;
      RXTXprofile[i].tx.eq_freq[5]  =  1000.0;
      RXTXprofile[i].tx.eq_freq[6]  =  1500.0;
      RXTXprofile[i].tx.eq_freq[7]  =  2000.0;
      RXTXprofile[i].tx.eq_freq[8]  =  2500.0;
      RXTXprofile[i].tx.eq_freq[9]  =  3000.0;
      RXTXprofile[i].tx.eq_freq[10] =  5000.0;
      RXTXprofile[i].tx.cfc_freq[0] =     0.0;
      RXTXprofile[i].tx.cfc_freq[1] =    50.0;
      RXTXprofile[i].tx.cfc_freq[3] =   200.0;
      RXTXprofile[i].tx.cfc_freq[4] =   500.0;
      RXTXprofile[i].tx.cfc_freq[5] =  1000.0;
      RXTXprofile[i].tx.cfc_freq[6] =  1500.0;
      RXTXprofile[i].tx.cfc_freq[7] =  2000.0;
      RXTXprofile[i].tx.cfc_freq[8] =  2500.0;
      RXTXprofile[i].tx.cfc_freq[9] =  3000.0;
      RXTXprofile[i].tx.cfc_freq[10] =  5000.0;
      for (int j = 0; j < 11; j++) {
        RXTXprofile[i].tx.eq_gain[j] = 0;
        RXTXprofile[i].tx.cfc_lvl   [j] = 0;
        RXTXprofile[i].tx.cfc_post  [j] = 0;
      }
      //
      // Mode- or Profile-specific dfaults
      //
      switch (i) {
      case modeLSB:
      case modeUSB:
      case modeDSB:
      default:
        RXTXprofile[i].rx.agc      = AGC_MEDIUM;
        RXTXprofile[i].rx.filter   = filterF5; //  2700 Hz
        RXTXprofile[i].rx.step     = 100;
        RXTXprofile[i].rx.rit_step = 100;
        RXTXprofile[i].tx.ptt_delay = 250;
        break;
      case modeDIGL:
      case modeDIGU:
        RXTXprofile[i].rx.agc      = AGC_FAST;
        RXTXprofile[i].rx.filter   = filterF6; //  1000 Hz
        RXTXprofile[i].rx.step     = 50;
        RXTXprofile[i].rx.rit_step = 100;
        RXTXprofile[i].tx.ptt_delay = 50;
        break;
      case modeCWL:
      case modeCWU:
        RXTXprofile[i].rx.agc      = AGC_FAST;
        RXTXprofile[i].rx.filter   = filterF4; //   500 Hz
        RXTXprofile[i].rx.step     = 25;
        RXTXprofile[i].rx.rit_step = 10;
        RXTXprofile[i].tx.ptt_delay  = 0;
        break;
      case modeAM:
      case modeSAM:
      case modeSPEC:
      case modeDRM:
      case modeFMN:
        RXTXprofile[i].rx.agc      = AGC_MEDIUM;
        RXTXprofile[i].rx.filter   = filterF3; //  8000 Hz
        RXTXprofile[i].rx.step     = 100;
        RXTXprofile[i].rx.rit_step = 100;
        RXTXprofile[i].tx.ptt_delay   = 250;
        break;
      case PROF_AUDIO:
        RXTXprofile[i].rx.agc      = AGC_MEDIUM;
        RXTXprofile[i].rx.filter   = filterF0; //  The widest one
        RXTXprofile[i].rx.step     = 100;
        RXTXprofile[i].rx.rit_step = 100;
        RXTXprofile[i].tx.ptt_delay = 50;
        break;
      case PROF_CONTEST:
      case PROF_DX:
        //
        // Activate Compression (10 dB) and slightly boost medium frequencies in TX
        //
        RXTXprofile[i].rx.agc      = AGC_MEDIUM;
        RXTXprofile[i].rx.filter   = filterF5; //  2700 Hz
        RXTXprofile[i].rx.step     = 100;
        RXTXprofile[i].rx.rit_step =  10;
        RXTXprofile[i].tx.en_eq = 1;
        RXTXprofile[i].tx.compressor = 1;
        RXTXprofile[i].tx.compressor_level = 10.0;
        RXTXprofile[i].tx.eq_gain[0]  =     0.0;
        RXTXprofile[i].tx.eq_gain[1]  =     1.0;
        RXTXprofile[i].tx.eq_gain[3]  =     3.0;
        RXTXprofile[i].tx.eq_gain[4]  =     5.0;
        RXTXprofile[i].tx.eq_gain[5]  =     4.0;
        RXTXprofile[i].tx.eq_gain[6]  =     3.0;
        RXTXprofile[i].tx.eq_gain[7]  =     1.0;
        RXTXprofile[i].tx.eq_gain[8]  =     0.0;
        RXTXprofile[i].tx.eq_gain[9]  =    -1.0;
        RXTXprofile[i].tx.eq_gain[10] =    -1.0;
        RXTXprofile[i].tx.ptt_delay = 0;
        break;
      }
      //
      // Overwrite with data from props file
      //
      GetPropI1("modeset.%d.filter", i,                 RXTXprofile[i].rx.filter);
      GetPropI1("modeset.%d.cwPeak", i,                 RXTXprofile[i].rx.cwPeak);
      GetPropI1("modeset.%d.step", i,                   RXTXprofile[i].rx.step);
      GetPropI1("modeset.%d.rit_step", i,               RXTXprofile[i].rx.rit_step);
      GetPropF1("modeset.%d.rxvolume", i,               RXTXprofile[i].rx.volume);
      GetPropI1("modeset.%d.nb", i,                     RXTXprofile[i].rx.nb);
      GetPropF1("modeset.%d.nb_tau", i,                 RXTXprofile[i].rx.nb_tau);
      GetPropF1("modeset.%d.nb_hang", i,                RXTXprofile[i].rx.nb_hang);
      GetPropF1("modeset.%d.nb_advtime", i,             RXTXprofile[i].rx.nb_advtime);
      GetPropF1("modeset.%d.nb_thresh", i,              RXTXprofile[i].rx.nb_thresh);
      GetPropI1("modeset.%d.nb2_mode", i,               RXTXprofile[i].rx.nb2_mode);
      GetPropI1("modeset.%d.nr", i,                     RXTXprofile[i].rx.nr);
      GetPropI1("modeset.%d.nr_agc", i,                 RXTXprofile[i].rx.nr_agc);
      GetPropI1("modeset.%d.nr2_gain_method", i,        RXTXprofile[i].rx.nr2_gain_method);
      GetPropI1("modeset.%d.nr2_npe_method", i,         RXTXprofile[i].rx.nr2_npe_method);
      GetPropF1("modeset.%d.nr2_trained_threshold", i,  RXTXprofile[i].rx.nr2_trained_threshold);
      GetPropF1("modeset.%d.nr2_trained_t2", i,         RXTXprofile[i].rx.nr2_trained_t2);
      GetPropI1("modeset.%d.nr2_post", i,               RXTXprofile[i].rx.nr2_post);
      GetPropI1("modeset.%d.nr2_post_taper", i,         RXTXprofile[i].rx.nr2_post_taper);
      GetPropI1("modeset.%d.nr2_post_nlevel", i,        RXTXprofile[i].rx.nr2_post_nlevel);
      GetPropI1("modeset.%d.nr2_post_factor", i,        RXTXprofile[i].rx.nr2_post_factor);
      GetPropI1("modeset.%d.nr2_post_rate", i,          RXTXprofile[i].rx.nr2_post_rate);
      GetPropF1("modeset.%d.nr4_reduction_amount", i,   RXTXprofile[i].rx.nr4_reduction_amount);
      GetPropF1("modeset.%d.nr4_smoothing_factor", i,   RXTXprofile[i].rx.nr4_smoothing_factor);
      GetPropF1("modeset.%d.nr4_whitening_factor", i,   RXTXprofile[i].rx.nr4_whitening_factor);
      GetPropF1("modeset.%d.nr4_noise_rescale", i,      RXTXprofile[i].rx.nr4_noise_rescale);
      GetPropF1("modeset.%d.nr4_post_threshold", i,     RXTXprofile[i].rx.nr4_post_threshold);
      GetPropI1("modeset.%d.nr4_noise_scaling_type", i, RXTXprofile[i].rx.nr4_noise_scaling_type);
      GetPropI1("modeset.%d.en_squelch", i,             RXTXprofile[i].rx.squelch_enable);
      GetPropF1("modeset.%d.squelch", i,                RXTXprofile[i].rx.squelch);
      GetPropI1("modeset.%d.anf", i,                    RXTXprofile[i].rx.anf);
      GetPropI1("modeset.%d.anf_taps", i,               RXTXprofile[i].rx.anf_taps);
      GetPropI1("modeset.%d.anf_delay", i,              RXTXprofile[i].rx.anf_delay);
      GetPropF1("modeset.%d.anf_gain", i,               RXTXprofile[i].rx.anf_gain);
      GetPropF1("modeset.%d.anf_leakage", i,            RXTXprofile[i].rx.anf_leakage);
      GetPropI1("modeset.%d.fm_limiter", i,             RXTXprofile[i].rx.fm_limiter);
      GetPropF1("modeset.%d.fm_limiter_gain", i,        RXTXprofile[i].rx.fm_limiter_gain);
      GetPropI1("modeset.%d.snb", i,                    RXTXprofile[i].rx.snb);
      GetPropI1("modeset.%d.agc", i,                    RXTXprofile[i].rx.agc);
      GetPropI1("modeset.%d.agc_custom_attack", i,      RXTXprofile[i].rx.agc_custom_attack);
      GetPropI1("modeset.%d.agc_custom_decay", i,       RXTXprofile[i].rx.agc_custom_decay);
      GetPropI1("modeset.%d.agc_custom_hang", i,        RXTXprofile[i].rx.agc_custom_hang);
      GetPropI1("modeset.%d.agc_custom_slope", i,       RXTXprofile[i].rx.agc_custom_slope);
      GetPropI1("modeset.%d.en_rxeq", i,                RXTXprofile[i].rx.en_eq);
      GetPropI1("modeset.%d.ptt_delay", i,              RXTXprofile[i].tx.ptt_delay);
      GetPropI1("modeset.%d.en_txeq", i,                RXTXprofile[i].tx.en_eq);
      GetPropI1("modeset.%d.compressor", i,             RXTXprofile[i].tx.compressor);
      GetPropF1("modeset.%d.compressor_level", i,       RXTXprofile[i].tx.compressor_level);
      GetPropF1("modeset.%d.mic_gain", i,               RXTXprofile[i].tx.mic_gain);
      GetPropI1("modeset.%d.dexp", i,                   RXTXprofile[i].tx.dexp);
      GetPropI1("modeset.%d.dexp_trigger", i,           RXTXprofile[i].tx.dexp_trigger);
      GetPropF1("modeset.%d.dexp_tau", i,               RXTXprofile[i].tx.dexp_tau);
      GetPropF1("modeset.%d.dexp_attack", i,            RXTXprofile[i].tx.dexp_attack);
      GetPropF1("modeset.%d.dexp_release", i,           RXTXprofile[i].tx.dexp_release);
      GetPropF1("modeset.%d.dexp_hold", i,              RXTXprofile[i].tx.dexp_hold);
      GetPropI1("modeset.%d.dexp_exp", i,               RXTXprofile[i].tx.dexp_exp);
      GetPropF1("modeset.%d.dexp_hyst", i,              RXTXprofile[i].tx.dexp_hyst);
      GetPropI1("modeset.%d.dexp_filter", i,            RXTXprofile[i].tx.dexp_filter);
      GetPropI1("modeset.%d.dexp_filter_low", i,        RXTXprofile[i].tx.dexp_filter_low);
      GetPropI1("modeset.%d.dexp_filter_high", i,       RXTXprofile[i].tx.dexp_filter_high);
      GetPropI1("modeset.%d.cfc", i,                    RXTXprofile[i].tx.cfc);
      GetPropI1("modeset.%d.cfc_eq", i,                 RXTXprofile[i].tx.cfc_eq);
      GetPropI1("modeset.%d.tx_default_filter_low", i,  RXTXprofile[i].tx.default_filter_low);
      GetPropI1("modeset.%d.tx_default_filter_high", i, RXTXprofile[i].tx.default_filter_high);
      GetPropI1("modeset.%d.use_rx_filter", i,          RXTXprofile[i].tx.use_rx_filter);
      GetPropI1("modeset.%d.tx_phrot_enable", i,        RXTXprofile[i].tx.phrot_enable);
      GetPropI1("modeset.%d.tx_phrot_stages", i,        RXTXprofile[i].tx.phrot_stages);
      GetPropF1("modeset.%d.tx_phrot_corner", i,        RXTXprofile[i].tx.phrot_corner);
      GetPropI1("modeset.%d.tx_phrot_reverse", i,       RXTXprofile[i].tx.phrot_reverse);
      for (int j = 0; j < 11; j++) {
        GetPropF2("modeset.%d.txeq.%d", i, j,           RXTXprofile[i].tx.eq_gain[j]);
        GetPropF2("modeset.%d.txeqfrq.%d", i, j,        RXTXprofile[i].tx.eq_freq[j]);
        GetPropF2("modeset.%d.rxeq.%d", i, j,           RXTXprofile[i].rx.eq_gain[j]);
        GetPropF2("modeset.%d.rxeqfrq.%d", i, j,        RXTXprofile[i].rx.eq_freq[j]);
        GetPropF2("modeset.%d.cfc_frq.%d", i, j,        RXTXprofile[i].tx.cfc_freq[j]);
        GetPropF2("modeset.%d.cfc_lvl.%d", i, j,        RXTXprofile[i].tx.cfc_lvl[j]);
        GetPropF2("modeset.%d.cfc_post.%d", i, j,       RXTXprofile[i].tx.cfc_post[j]);
      }
      GetPropI1("modeset.%d.rx_audio_channel", i,       RXTXprofile[i].rx.audio_channel);
      GetPropI1("modeset.%d.rx_local_audio", i,         RXTXprofile[i].rx.local_audio);
      GetPropI1("modeset.%d.tx_local_audio", i,         RXTXprofile[i].tx.local_audio);
      GetPropS1("modeset.%d.rx_audio_name", i,          RXTXprofile[i].rx.audio_name);
      GetPropS1("modeset.%d.tx_audio_name", i,          RXTXprofile[i].tx.audio_name);
    }
  }
}

void profiles_copy_rxtxprofile(int mode) {
  //
  // The client may call this, if local audio settings have been changed
  //
  //
  // If mode is USB or LSB or DSB, copy settings of that mode to USB and LSB and DSB
  // If mode is CWU or CWL       , copy settings of that mode to CWL and CWU
  // If mode is DIGU or DIGL     , copy settings of that mode to DIGL and DIGU
  //
  switch (mode) {
  case modeCWU:
  case modeCWL:
    RXTXprofile[modeCWU] = RXTXprofile[mode];
    RXTXprofile[modeCWL] = RXTXprofile[mode];
    break;
  case modeDIGU:
  case modeDIGL:
    RXTXprofile[modeDIGU] = RXTXprofile[mode];
    RXTXprofile[modeDIGL] = RXTXprofile[mode];
    break;
  case modeLSB:
  case modeUSB:
  case modeDSB:
    RXTXprofile[modeLSB] = RXTXprofile[mode];
    RXTXprofile[modeUSB] = RXTXprofile[mode];
    RXTXprofile[modeDSB] = RXTXprofile[mode];
    break;
  }
}

void profiles_load_rx_profile(RECEIVER *rx, int m) {
  int id = rx->id;
  if (radio_is_remote) {
    send_rxprofile(cl_sock_tcp, rx->id, 0, m);
  } else {
    suppress_popup_sliders++;
    //
    // Apply VFO settings to VFO controlling the receiver
    //
    vfo[id].filter                = RXTXprofile[m].rx.filter;
    vfo[id].cwAudioPeakFilter     = RXTXprofile[m].rx.cwPeak;
    vfo[id].rit_step              = RXTXprofile[m].rx.rit_step;
    vfo[id].step                  = RXTXprofile[m].rx.step;
    //
    // Apply noise and EQ settings to the receiver
    //
    radio_set_af_gain(id, RXTXprofile[m].rx.volume);
    rx->nb                        = RXTXprofile[m].rx.nb;
    rx->nb_tau                    = RXTXprofile[m].rx.nb_tau;
    rx->nb_hang                   = RXTXprofile[m].rx.nb_hang;
    rx->nb_advtime                = RXTXprofile[m].rx.nb_advtime;
    rx->nb_thresh                 = RXTXprofile[m].rx.nb_thresh;
    rx->nb2_mode                  = RXTXprofile[m].rx.nb2_mode;
    //
    rx->nr                        = RXTXprofile[m].rx.nr;
    rx->nr_agc                    = RXTXprofile[m].rx.nr_agc;
    rx->nr2_gain_method           = RXTXprofile[m].rx.nr2_gain_method;
    rx->nr2_npe_method            = RXTXprofile[m].rx.nr2_npe_method;
    rx->nr2_post                  = RXTXprofile[m].rx.nr2_post;
    rx->nr2_post_taper            = RXTXprofile[m].rx.nr2_post_taper;
    rx->nr2_post_nlevel           = RXTXprofile[m].rx.nr2_post_nlevel;
    rx->nr2_post_factor           = RXTXprofile[m].rx.nr2_post_factor;
    rx->nr2_post_rate             = RXTXprofile[m].rx.nr2_post_rate;
    rx->nr2_trained_threshold     = RXTXprofile[m].rx.nr2_trained_threshold;
    rx->nr2_trained_t2            = RXTXprofile[m].rx.nr2_trained_t2;
    rx->nr4_reduction_amount      = RXTXprofile[m].rx.nr4_reduction_amount;
    rx->nr4_smoothing_factor      = RXTXprofile[m].rx.nr4_smoothing_factor;
    rx->nr4_whitening_factor      = RXTXprofile[m].rx.nr4_whitening_factor;
    rx->nr4_noise_rescale         = RXTXprofile[m].rx.nr4_noise_rescale;
    rx->nr4_post_threshold        = RXTXprofile[m].rx.nr4_post_threshold;
    rx->nr4_noise_scaling_type    = RXTXprofile[m].rx.nr4_noise_scaling_type;
    radio_set_squelch_enable(rx->id, RXTXprofile[m].rx.squelch_enable);
    radio_set_squelch       (rx->id, RXTXprofile[m].rx.squelch);
    rx->anf                       = RXTXprofile[m].rx.anf;
    rx->anf_taps                  = RXTXprofile[m].rx.anf_taps;
    rx->anf_delay                 = RXTXprofile[m].rx.anf_delay;
    rx->anf_gain                  = RXTXprofile[m].rx.anf_gain;
    rx->anf_leakage               = RXTXprofile[m].rx.anf_leakage;
    rx->snb                       = RXTXprofile[m].rx.snb;
    rx->agc                       = RXTXprofile[m].rx.agc;
    rx->agc_custom_attack         = RXTXprofile[m].rx.agc_custom_attack;
    rx->agc_custom_decay          = RXTXprofile[m].rx.agc_custom_decay;
    rx->agc_custom_hang           = RXTXprofile[m].rx.agc_custom_hang;
    rx->agc_custom_slope          = RXTXprofile[m].rx.agc_custom_slope;
    rx->fm_limiter                = RXTXprofile[m].rx.fm_limiter;
    rx->fm_limiter_gain           = RXTXprofile[m].rx.fm_limiter_gain;
    rx->eq_enable                 = RXTXprofile[m].rx.en_eq;
    for (int i = 0; i < 11; i++) {
      rx->eq_gain[i] = RXTXprofile[m].rx.eq_gain[i];
      rx->eq_freq[i] = RXTXprofile[m].rx.eq_freq[i];
    }
    rx_set_agc(rx);
    rx_set_equalizer(rx);
    rx_set_noise(rx);
    rx_filter_changed(rx);
    suppress_popup_sliders++;
    g_idle_add(ext_vfo_update, NULL);
  }
  //
  // Local audio stuff is done on both sides
  //
  if (id == 0) {
    rx->audio_channel = RXTXprofile[m].rx.audio_channel;
    if (rx->local_audio != RXTXprofile[m].rx.local_audio
        || strncmp(rx->audio_name, RXTXprofile[m].rx.audio_name, sizeof(rx->audio_name))) {
      //
      // This is RX1 and local audio settings in RXTXprofile differ from actual settings
      //
      if (rx->local_audio) {
        rx->local_audio = 0;
        audio_close_output(rx);
      }
      if (RXTXprofile[m].rx.local_audio) {
        snprintf(rx->audio_name, sizeof(rx->audio_name), "%s", RXTXprofile[m].rx.audio_name);
        if (audio_open_output(rx) < 0) {
          rx->local_audio = 0;
          t_print("%s: Open audio output failed\n", __func__);
        } else {
          rx->local_audio = 1;
        }
      }
    }
  }
}

void profiles_load_tx_profile(TRANSMITTER *tx, int m) {
  if (!can_transmit) { return; }
  if (radio_is_remote) {
    send_txprofile(cl_sock_tcp, 0, m);
  } else {
    suppress_popup_sliders++;
    ptt_delay            = RXTXprofile[m].tx.ptt_delay;
    tx->eq_enable        = RXTXprofile[m].tx.en_eq;
    tx->compressor       = RXTXprofile[m].tx.compressor;
    tx->compressor_level = RXTXprofile[m].tx.compressor_level;
    radio_set_mic_gain(RXTXprofile[m].tx.mic_gain);
    tx->dexp                = RXTXprofile[m].tx.dexp;
    tx->dexp_trigger        = RXTXprofile[m].tx.dexp_trigger;
    tx->dexp_tau            = RXTXprofile[m].tx.dexp_tau;
    tx->dexp_attack         = RXTXprofile[m].tx.dexp_attack;
    tx->dexp_release        = RXTXprofile[m].tx.dexp_release;
    tx->dexp_hold           = RXTXprofile[m].tx.dexp_hold;
    tx->dexp_exp            = RXTXprofile[m].tx.dexp_exp;
    tx->dexp_hyst           = RXTXprofile[m].tx.dexp_hyst;
    tx->dexp_filter         = RXTXprofile[m].tx.dexp_filter;
    tx->dexp_filter_low     = RXTXprofile[m].tx.dexp_filter_low;
    tx->dexp_filter_high    = RXTXprofile[m].tx.dexp_filter_high;
    tx->cfc                 = RXTXprofile[m].tx.cfc;
    tx->cfc_eq              = RXTXprofile[m].tx.cfc_eq;
    tx->default_filter_low  = RXTXprofile[m].tx.default_filter_low;
    tx->default_filter_high = RXTXprofile[m].tx.default_filter_high;
    tx->use_rx_filter       = RXTXprofile[m].tx.use_rx_filter;
    tx->phrot_enable        = RXTXprofile[m].tx.phrot_enable;
    tx->phrot_reverse       = RXTXprofile[m].tx.phrot_reverse;
    tx->phrot_stages        = RXTXprofile[m].tx.phrot_stages;
    tx->phrot_corner        = RXTXprofile[m].tx.phrot_corner;
    for (int i = 0; i < 11; i++) {
      tx->eq_gain[i]  = RXTXprofile[m].tx.eq_gain[i];
      tx->eq_freq[i]  = RXTXprofile[m].tx.eq_freq[i];
      tx->cfc_freq[i] = RXTXprofile[m].tx.cfc_freq[i];
      tx->cfc_lvl[i]  = RXTXprofile[m].tx.cfc_lvl[i];
      tx->cfc_post[i] = RXTXprofile[m].tx.cfc_post[i];
    }
    tx_set_filter(tx);
    tx_set_compressor(tx);
    tx_set_dexp(tx);
    tx_set_equalizer(tx);
    tx_set_phrot(tx);
  }
  //
  // local audio is done on both sides
  //
  if (tx->local_audio != RXTXprofile[m].tx.local_audio ||
      strncmp(tx->audio_name, RXTXprofile[m].tx.audio_name, sizeof(tx->audio_name))) {
    //
    // TX local audio settings in RXTXprofile differ from local settings:
    //
    if (tx->local_audio) {
      tx->local_audio = 0;
      audio_close_input(tx);
    }
    if (RXTXprofile[m].tx.local_audio) {
      snprintf(tx->audio_name, sizeof(tx->audio_name), "%s", RXTXprofile[m].tx.audio_name);
      if (audio_open_input(tx) < 0) {
        tx->local_audio = 0;
        t_print("%s: Open audio input failed\n", __func__);
      } else {
        tx->local_audio = 1;
      }
    }
  }
  g_idle_add(ext_vfo_update, NULL);
  suppress_popup_sliders--;
}

void profiles_load_rxtx_profile(RECEIVER *rx) {
  ASSERT_SERVER();
  int id, m;
  id = rx->id;
  m = vfo[id].mode;
  profiles_load_rx_profile(rx, m);
  if (can_transmit && id == vfo_get_tx_vfo()) {
    profiles_load_tx_profile(transmitter, m);
  }
}

void profiles_save_rx_profile(RECEIVER *rx, int m) {
  //
  // Save actual settings of the receiver into slot
  //
  int id = rx->id;
  if (radio_is_remote) {
    send_rxprofile(cl_sock_tcp, id, 1, m);
  } else {
    RXTXprofile[m].rx.filter   = vfo[id].filter;
    RXTXprofile[m].rx.cwPeak   = vfo[id].cwAudioPeakFilter;
    RXTXprofile[m].rx.rit_step = vfo[id].rit_step;
    RXTXprofile[m].rx.step     = vfo[id].step ;
    //
    RXTXprofile[m].rx.volume                 = rx->volume;
    RXTXprofile[m].rx.nb                     = rx->nb;
    RXTXprofile[m].rx.nb_tau                 = rx->nb_tau;
    RXTXprofile[m].rx.nb_hang                = rx->nb_hang;
    RXTXprofile[m].rx.nb_advtime             = rx->nb_advtime;
    RXTXprofile[m].rx.nb_thresh              = rx->nb_thresh;
    RXTXprofile[m].rx.nb2_mode               = rx->nb2_mode;
    //
    RXTXprofile[m].rx.nr                     = rx->nr;
    RXTXprofile[m].rx.nr_agc                 = rx->nr_agc;
    RXTXprofile[m].rx.nr2_gain_method        = rx->nr2_gain_method;
    RXTXprofile[m].rx.nr2_npe_method         = rx->nr2_npe_method;
    RXTXprofile[m].rx.nr2_post               = rx->nr2_post;
    RXTXprofile[m].rx.nr2_post_taper         = rx->nr2_post_taper;
    RXTXprofile[m].rx.nr2_post_nlevel        = rx->nr2_post_nlevel;
    RXTXprofile[m].rx.nr2_post_factor        = rx->nr2_post_factor;
    RXTXprofile[m].rx.nr2_post_rate          = rx->nr2_post_rate;
    RXTXprofile[m].rx.nr2_trained_threshold  = rx->nr2_trained_threshold;
    RXTXprofile[m].rx.nr2_trained_t2         = rx->nr2_trained_t2;
    RXTXprofile[m].rx.nr4_reduction_amount   = rx->nr4_reduction_amount;
    RXTXprofile[m].rx.nr4_smoothing_factor   = rx->nr4_smoothing_factor;
    RXTXprofile[m].rx.nr4_whitening_factor   = rx->nr4_whitening_factor;
    RXTXprofile[m].rx.nr4_noise_rescale      = rx->nr4_noise_rescale;
    RXTXprofile[m].rx.nr4_post_threshold     = rx->nr4_post_threshold;
    RXTXprofile[m].rx.nr4_noise_scaling_type = rx->nr4_noise_scaling_type;
    RXTXprofile[m].rx.squelch_enable         = rx->squelch_enable;
    RXTXprofile[m].rx.squelch                = rx->squelch;
    RXTXprofile[m].rx.anf                    = rx->anf;
    RXTXprofile[m].rx.anf_taps               = rx->anf_taps;
    RXTXprofile[m].rx.anf_delay              = rx->anf_delay;
    RXTXprofile[m].rx.anf_gain               = rx->anf_gain;
    RXTXprofile[m].rx.anf_leakage            = rx->anf_leakage;
    RXTXprofile[m].rx.snb                    = rx->snb;
    RXTXprofile[m].rx.agc                    = rx->agc;
    RXTXprofile[m].rx.agc_custom_attack      = rx->agc_custom_attack;
    RXTXprofile[m].rx.agc_custom_decay       = rx->agc_custom_decay;
    RXTXprofile[m].rx.agc_custom_hang        = rx->agc_custom_hang;
    RXTXprofile[m].rx.agc_custom_slope       = rx->agc_custom_slope;
    RXTXprofile[m].rx.fm_limiter             = rx->fm_limiter;
    RXTXprofile[m].rx.fm_limiter_gain        = rx->fm_limiter_gain;
    RXTXprofile[m].rx.en_eq                  = rx->eq_enable;
    for (int i = 0; i < 11; i++) {
      RXTXprofile[m].rx.eq_gain[i]           = rx->eq_gain[i];
      RXTXprofile[m].rx.eq_freq[i]           = rx->eq_freq[i];
    }
  }
  //
  // local audio stuff is stored on both sides
  //
  RXTXprofile[m].rx.audio_channel          = rx->audio_channel;
  RXTXprofile[m].rx.local_audio            = rx->local_audio;
  snprintf(RXTXprofile[m].rx.audio_name, sizeof(RXTXprofile[m].rx.audio_name), "%s", rx->audio_name);
}

void profiles_save_tx_profile(TRANSMITTER *tx, int m) {
  //
  // Save actual settings of the transmitter into slot
  //
  if (!can_transmit) { return; }
  if (radio_is_remote) {
    send_txprofile(cl_sock_tcp, 1, m);
  } else {
    RXTXprofile[m].tx.ptt_delay           = ptt_delay;
    RXTXprofile[m].tx.en_eq               = tx->eq_enable;
    RXTXprofile[m].tx.compressor          = tx->compressor;
    RXTXprofile[m].tx.compressor_level    = tx->compressor_level;
    RXTXprofile[m].tx.mic_gain            = tx->mic_gain;
    RXTXprofile[m].tx.dexp                = tx->dexp;
    RXTXprofile[m].tx.dexp_trigger        = tx->dexp_trigger;
    RXTXprofile[m].tx.dexp_tau            = tx->dexp_tau;
    RXTXprofile[m].tx.dexp_attack         = tx->dexp_attack;
    RXTXprofile[m].tx.dexp_release        = tx->dexp_release;
    RXTXprofile[m].tx.dexp_hold           = tx->dexp_hold;
    RXTXprofile[m].tx.dexp_exp            = tx->dexp_exp;
    RXTXprofile[m].tx.dexp_hyst           = tx->dexp_hyst;
    RXTXprofile[m].tx.dexp_filter         = tx->dexp_filter;
    RXTXprofile[m].tx.dexp_filter_low     = tx->dexp_filter_low;
    RXTXprofile[m].tx.dexp_filter_high    = tx->dexp_filter_high;
    RXTXprofile[m].tx.cfc                 = tx->cfc;
    RXTXprofile[m].tx.cfc_eq              = tx->cfc_eq;
    RXTXprofile[m].tx.default_filter_low  = tx->default_filter_low;
    RXTXprofile[m].tx.default_filter_high = tx->default_filter_high;
    RXTXprofile[m].tx.use_rx_filter       = tx->use_rx_filter;
    RXTXprofile[m].tx.phrot_enable        = tx->phrot_enable;
    RXTXprofile[m].tx.phrot_reverse       = tx->phrot_reverse;
    RXTXprofile[m].tx.phrot_stages        = tx->phrot_stages;
    RXTXprofile[m].tx.phrot_corner        = tx->phrot_corner;
    for (int i = 0; i < 11; i++) {
      RXTXprofile[m].tx.eq_gain[i]        = tx->eq_gain[i];
      RXTXprofile[m].tx.eq_freq[i]        = tx->eq_freq[i];
      RXTXprofile[m].tx.cfc_freq[i]       = tx->cfc_freq[i];
      RXTXprofile[m].tx.cfc_lvl[i]        = tx->cfc_lvl[i];
      RXTXprofile[m].tx.cfc_post[i]       = tx->cfc_post[i];
    }
  }
  //
  // Local audio is stored on both sides
  //
  RXTXprofile[m].tx.local_audio         = tx->local_audio;
  snprintf(RXTXprofile[m].tx.audio_name, sizeof(RXTXprofile[m].tx.audio_name), "%s", tx->audio_name);
}
