/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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

#ifndef _TRANSMITTER_H
#define _TRANSMITTER_H

#include <gtk/gtk.h>

#define CTCSS_FREQUENCIES 38
extern double ctcss_frequencies[CTCSS_FREQUENCIES];

typedef struct _transmitter {
  int id;
  int dac;
  int fps;
  int displaying;
  int mic_sample_rate;
  int mic_dsp_rate;
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

  int filter_low;
  int filter_high;
  gboolean use_rx_filter;

  int alex_antenna;

  int width;
  int height;

  GtkWidget *dialog;
  GtkWidget *panel;
  GtkWidget *panadapter;

  int panadapter_low;
  int panadapter_high;
  int panadapter_step;

  cairo_surface_t *panadapter_surface;

  int local_microphone;
  gchar microphone_name[128];

  int out_of_band;
  guint out_of_band_timer_id;

  int twotone;
  int puresignal;
  int feedback;
  int auto_on;

  // PS 2.0 parameters
  double ps_ampdelay;
  int ps_ints;
  int ps_spi;
  int ps_stbl;
  int ps_map;
  int ps_pin;
  double ps_ptol;
  double ps_moxdelay;
  double ps_loopdelay;
  int    ps_oneshot;

  gboolean ctcss_enabled;
  int ctcss;

  int deviation;

  double am_carrier_level;

  int attenuation;      // Attenuation in the RX ADC0 chain (!) if TX is active

  int drive;            // value of the drive slider
  int tune_use_drive;
  int tune_drive;

  int drive_level;      // amplitude (0-255) corresponding to "drive"
  int    do_scale;      // apply TX iq scaling
  double drive_scale;   // additional TX iq scaling required
  double drive_iscal;   // inverse of drive_scale

  int compressor;
  double compressor_level;

  double fwd;
  double rev;
  double alc;
  double swr;
  gboolean swr_protection;
  double swr_alarm;

  int x;
  int y;

  int dialog_x;
  int dialog_y;

  int display_filled;

  double *cw_sig_rf;      // contains the CW RF envelope

  int cw_ramp_audio_len;  // ramp width in samples
  int cw_ramp_audio_ptr;
  int cw_ramp_rf_len;     // ramp width in samples
  int cw_ramp_rf_ptr;
  double *cw_ramp_audio;  // ramp for the side tone
  double *cw_ramp_rf;     // ramp for the RF signal
  GMutex cw_ramp_mutex;   // needed when changing the ramp width during TX

  //
  // Equalizer data
  //
  int  eq_enable;
  double eq_freq[11];  // frequency in Hz
  double eq_gain[11];  // gain in dB
  int    eq_tenband;  // four or ten channels

} TRANSMITTER;

extern TRANSMITTER *tx_create_transmitter(int id, int width, int height);

void tx_create_dialog(TRANSMITTER *tx);
void tx_reconfigure(TRANSMITTER *tx, int width, int height);

//
// CW pulse shaper variables
//
extern int cw_key_up;
extern int cw_key_down;
extern int cw_not_ready;

extern void tx_set_mode(TRANSMITTER* tx, int m);
extern void tx_set_filter(TRANSMITTER *tx);
extern void tx_set_am_carrier_level(const TRANSMITTER *tx);
extern void tx_set_pre_emphasize(const TRANSMITTER *tx, int state);
extern void tx_set_ctcss(TRANSMITTER *tx, int state, int i);

extern void tx_add_mic_sample(TRANSMITTER *tx, float mic_sample);
extern void tx_add_freedv_mic_sample(TRANSMITTER *tx, float mic_sample);

extern void tx_save_state(const TRANSMITTER *tx);
extern void tx_set_out_of_band(TRANSMITTER *tx);
extern void tx_set_displaying(TRANSMITTER *tx, int state);

extern void tx_set_ps(TRANSMITTER *tx, int state);
extern void tx_set_twotone(TRANSMITTER *tx, int state);

extern void tx_set_compressor_level(TRANSMITTER *tx, double level);
extern void tx_set_compressor(TRANSMITTER *tx, int state);

extern void tx_set_ps_sample_rate(const TRANSMITTER *tx, int rate);
extern void tx_add_ps_iq_samples(const TRANSMITTER *tx, double i_sample_0, double q_sample_0, double i_sample_1,
                              double q_sample_1);

extern void tx_set_ramps(TRANSMITTER *tx);
extern void tx_set_equalizer(TRANSMITTER *tx);
extern void tx_set_framerate(TRANSMITTER *tx, int fps);
extern void cw_hold_key(int state);

#endif


