/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
* 2025 - Christoph van Wüllen, DL1YCF
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

#ifndef _TRANSMITTER_H_
#define _TRANSMITTER_H_

#include <gtk/gtk.h>

#define CTCSS_FREQUENCIES 38
extern double ctcss_frequencies[CTCSS_FREQUENCIES];

typedef struct _transmitter {
  int id;
  int dac;
  int fps;
  int displaying;
  int dsp_rate;
  int iq_output_rate;
  int buffer_size;
  int dsp_size;
  int fft_size;
  int pixels;
  int samples;
  int output_samples;
  int ratio;
  double *mic_input_buffer;
  double *iq_output_buffer;

  float *pixel_samples;
  int display_panadapter;
  int display_waterfall;
  guint update_timer_id;
  GMutex display_mutex;
  int display_detector_mode;
  int display_average_mode;
  double display_average_time;

  int default_filter_low;
  int default_filter_high;
  int filter_low;
  int filter_high;
  int use_rx_filter;

  int antenna;

  int width;
  int height;

  GtkWidget *dialog;
  GtkWidget *panel;
  GtkWidget *panadapter;

  int panadapter_low;
  int panadapter_high;
  int panadapter_step;
  int panadapter_peaks_on;
  int panadapter_num_peaks;
  int panadapter_ignore_range_divider;
  int panadapter_ignore_noise_percentile;
  int panadapter_hide_noise_filled;
  int panadapter_peaks_in_passband_filled;

  cairo_surface_t *panadapter_surface;

  int local_microphone;
  gchar microphone_name[128];

  int out_of_band;
  guint out_of_band_timer_id;

  int tune;
  int swrtune;            // produce SWR-dependent side tone while TUNE-ing
  double swrtune_volume;  // Volume of SWR-dependent side tone (1-127)
  double tt1_freq;        // Two-Tone frequencies
  double tt2_freq;        // Two-Tone frequencies
  int twotone;
  int puresignal;
  int feedback;
  int auto_on;

  int psinfo[16];
  double ps_getmx;
  double ps_setpk;
  // PS 2.0 parameters
  double ps_ampdelay;
  int    ps_ints;
  int    ps_spi;
  int    ps_stbl;
  int    ps_map;
  int    ps_pin;
  int    ps_ptol;
  double ps_moxdelay;
  double ps_loopdelay;
  int    ps_oneshot;

  int ctcss_enabled;
  int ctcss;

  int deviation;
  int pre_emphasize;

  double am_carrier_level;

  int attenuation;      // Attenuation in the RX ADC0 chain (!) if TX is active

  int drive;
  int tune_use_drive;
  int tune_drive;

  int drive_level;      // amplitude (0-255) corresponding to "drive"
  int do_scale;         // apply TX iq scaling
  double drive_scale;   // additional TX iq scaling required
  double drive_iscal;   // inverse of drive_scale
  double mic_gain;

  int compressor;
  double compressor_level;

  int cfc;                  // use Continuous Frequency Compressor (CFC)
  int cfc_eq;               // use post-equaliser of the CFC
  //
  // cfc_freq[0] is NOT USED
  // cfc_lvl [0] contains the added frequency-independent compression
  // cfc_post[0] contains the added frequency-independent gain
  //
  double cfc_freq[11];      // CFC corner frequencies
  double cfc_lvl[11];       // compression level for corner frequencies
  double cfc_post[11];      // EQ gain for corner frequencies

  int dexp;                 // use downward expander (DEXP)
  int    dexp_trigger;      // threshold for the "noise gate" in dB (!)
  double dexp_tau;          // time constant for low-pass filtering
  double dexp_attack;       // DEXP attack time
  double dexp_release;      // DEXP decay  time
  double dexp_hold;         // DEXP hold time
  int    dexp_exp;          // DEXP expansion ratio in dB (!)
  double dexp_hyst;         // DEXP hysteresis ratio
  int    dexp_filter;       // Do side channel filtering
  int    dexp_filter_low;   // low-cut of side channel filter
  int    dexp_filter_high;  // high-cut of side channel filter

  double fwd;
  double rev;
  double alc;
  int    alcmode;
  double swr;
  int    swr_protection;
  double swr_alarm;

  int x;
  int y;

  int dialog_x;
  int dialog_y;

  int display_filled;

  double *cw_sig_rf;      // contains the CW RF envelope
  int    *p1stone;        // contains side tone (CW or TUNE), only used in P1

  int cw_ramp_audio_len;  // ramp width in samples
  int cw_ramp_audio_ptr;
  int cw_ramp_rf_len;     // ramp width in samples
  int cw_ramp_rf_ptr;
  double *cw_ramp_audio;  // ramp for the side tone
  double *cw_ramp_rf;     // ramp for the RF signal
  GMutex cw_ramp_mutex;   // needed when changing the ramp width during TX

  //
  // Equaliser data
  //
  int    eq_enable;
  double eq_freq[11];  // frequency in Hz
  double eq_gain[11];  // gain in dB

} TRANSMITTER;

extern TRANSMITTER *tx_create_transmitter(int id, int pixels, int width, int height);

void tx_create_dialog(TRANSMITTER *tx);
void tx_reconfigure(TRANSMITTER *tx, int pixels, int width, int height);

extern void   tx_add_mic_sample(TRANSMITTER *tx, short next_mic_sample);
extern void   tx_add_ps_iq_samples(const TRANSMITTER *tx, double i_sample_0, double q_sample_0, double i_sample_1,
                                   double q_sample_1);

extern void   tx_close(const TRANSMITTER *tx);
extern void   tx_create_analyzer(const TRANSMITTER *tx);
extern double tx_get_alc(const TRANSMITTER *tx);
extern int    tx_get_pixels(TRANSMITTER *tx);
extern void   tx_off(const TRANSMITTER *tx);
extern void   tx_on(const TRANSMITTER *tx);

extern void   tx_playback_start(const TRANSMITTER *tx);
extern void   tx_playback_end(const TRANSMITTER *tx);

extern void   tx_ps_getinfo(TRANSMITTER *tx);
extern void   tx_ps_getmx(TRANSMITTER *tx);
extern void   tx_ps_getpk(TRANSMITTER *tx);
extern void   tx_ps_mox(const TRANSMITTER *tx, int state);
extern void   tx_ps_onoff(TRANSMITTER *tx, int state);
extern void   tx_ps_reset(const TRANSMITTER *tx);
extern void   tx_ps_resume(const TRANSMITTER *tx);
extern void   tx_ps_set_sample_rate(const TRANSMITTER *tx, int rate);
extern void   tx_ps_setparams(const TRANSMITTER *tx);

extern void   tx_save_state(const TRANSMITTER *tx);
extern void   tx_restore_state(TRANSMITTER *tx);

extern void   tx_set_am_carrier_level(const TRANSMITTER *tx);
extern void   tx_set_analyzer(const TRANSMITTER *tx);
extern void   tx_set_average(const TRANSMITTER *tx);
extern void   tx_set_bandpass(const TRANSMITTER *tx);
extern void   tx_set_compressor(TRANSMITTER *tx);
extern void   tx_set_ctcss(const TRANSMITTER *tx);
extern void   tx_set_detector(const TRANSMITTER *tx);
extern void   tx_set_deviation(const TRANSMITTER *tx);
extern void   tx_set_dexp(const TRANSMITTER *tx);
extern void   tx_set_displaying(TRANSMITTER *tx);
extern void   tx_set_equalizer(TRANSMITTER *tx);
extern void   tx_set_fft_size(const TRANSMITTER *tx);
extern void   tx_set_filter(TRANSMITTER *tx);
extern void   tx_set_framerate(TRANSMITTER *tx);
extern void   tx_set_mic_gain(const TRANSMITTER *tx);
extern void   tx_set_mode(TRANSMITTER* tx, int m);
extern void   tx_set_out_of_band(TRANSMITTER *tx);
extern void   tx_set_pre_emphasize(const TRANSMITTER *tx);
extern void   tx_set_ramps(TRANSMITTER *tx);
extern void   tx_set_singletone(const TRANSMITTER *tx, int state, double freq);
extern void   tx_set_twotone(TRANSMITTER *tx, int state);
extern void   tx_queue_cw_event(int state, int wait);

extern void tx_create_remote(TRANSMITTER *rx);
extern int  tx_remote_update_display(gpointer data);

#endif


