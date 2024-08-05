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

#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <wdsp.h>

#include "band.h"
#include "bandstack.h"
#include "channel.h"
#include "main.h"
#include "receiver.h"
#include "meter.h"
#include "filter.h"
#include "mode.h"
#include "property.h"
#include "radio.h"
#include "vfo.h"
#include "vox.h"
#include "meter.h"
#include "toolbar.h"
#include "tx_panadapter.h"
#include "waterfall.h"
#include "receiver.h"
#include "transmitter.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "ps_menu.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "audio.h"
#include "ext.h"
#include "sliders.h"
#ifdef USBOZY
  #include "ozyio.h"
#endif
#include "sintab.h"
#include "message.h"
#include "mystring.h"

#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

//
// CW pulses are timed by the heart-beat of the mic samples.
// Other parts of the program may produce CW RF pulses by manipulating
// these global variables:
//
// cw_key_up/cw_key_down: set number of samples for next key-down/key-up sequence
//                        Any of these variable will only be set from outside if
//                        both have value 0.
// cw_not_ready:          set to 0 if transmitting in CW mode. This is used to
//                        abort pending CAT CW messages if MOX or MODE is switched
//                        manually.
int cw_key_up = 0;
int cw_key_down = 0;
int cw_not_ready = 1;

double ctcss_frequencies[CTCSS_FREQUENCIES] = {
  67.0,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,  94.8,
  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3, 131.8,
  136.5, 141.3, 146.2, 151.4, 156.7, 162.2, 167.9, 173.8, 179.9, 186.2,
  192.8, 203.5, 210.7, 218.1, 225.7, 233.6, 241.8, 250.3
};

//
// static variables for the sine tone generators
//
static int p1radio = 0, p2radio = 0; // sine tone to the radio
static int p1local = 0, p2local = 0; // sine tone to local audio

static void init_analyzer(TRANSMITTER *tx);

static gboolean close_cb() {
  // there is nothing to clean up
  return TRUE;
}

static int clear_out_of_band_warning(gpointer data) {
  //
  // One-shot timer for clearing the "Out of band" message
  // in the VFO bar
  //
  TRANSMITTER *tx = (TRANSMITTER *)data;
  tx->out_of_band = 0;
  g_idle_add(ext_vfo_update, NULL);
  return G_SOURCE_REMOVE;
}

void transmitter_set_out_of_band(TRANSMITTER *tx) {
  //
  // Print "Out of band" warning message in the VFO bar
  // and clear it after 1 second.
  //
  tx->out_of_band = 1;
  g_idle_add(ext_vfo_update, NULL);
  tx->out_of_band_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 1000,
                             clear_out_of_band_warning, tx, NULL);
}

void transmitter_set_am_carrier_level(const TRANSMITTER *tx) {
  SetTXAAMCarrierLevel(tx->id, tx->am_carrier_level);
}

void transmitter_set_ctcss(TRANSMITTER *tx, int state, int i) {
  //t_print("transmitter_set_ctcss: state=%d i=%d frequency=%0.1f\n",state,i,ctcss_frequencies[i]);
  tx->ctcss_enabled = state;
  tx->ctcss = i;
  SetTXACTCSSFreq(tx->id, ctcss_frequencies[tx->ctcss]);
  SetTXACTCSSRun(tx->id, tx->ctcss_enabled);
}

void transmitter_set_compressor_level(TRANSMITTER *tx, double level) {
  tx->compressor_level = level;
  SetTXACompressorGain(tx->id, tx->compressor_level);
}

void transmitter_set_compressor(TRANSMITTER *tx, int state) {
  //
  // The CESSB overshoot filter (on/off) automatically follows
  // the Compressor on/off setting. This we can do because we
  // do not allow low-latency filters in the TX
  //
  tx->compressor = state;
  SetTXACompressorRun(tx->id, state);
  SetTXAosctrlRun(tx->id, state);
}

static void init_audio_ramp(double *ramp, int width) {
  //
  // This is for the sidetone, we use a raised cosine ramp
  //
  for (int i = 0; i <= width; i++) {
    double y = (double) i * 3.1415926535897932 / ((double) width);  // between 0 and Pi
    ramp[i] = 0.5 * (1.0 - cos(y));
  }
}

static void init_dl1ycf_ramp(double *ramp, int width) {
  //
  // ========================================================================
  //
  // Calculate a "DL1YCF" ramp
  // -------------------------
  //
  // The "black magic" in the coefficients comes from optimizing them
  // against the spectral pollution of a string of dots,
  // namely with ramp width 7 msec for CW speed  5 - 15 wpm
  //        and  ramp width 8 msec for CW speed 16 - 32 wpm
  //        and  ramp width 9 msec for CW speed 33 - 40 wpm
  //
  // such that the spectra meet ARRL's "clean signal initiative" requirement
  // for the maximum peak strength at frequencies with a distance to the
  // carrier that is larger than an offset, namely
  //
  //  -20 dBc for offsets >   90 Hz
  //  -40 dBc for offsets >  150 Hz
  //  -60 dBc for offsets >  338 Hz
  //
  // and is also meets the extended DL1YCF criteria which restrict spectral
  // pollution at larger offsets, namely
  //
  //  -80 dBc for offsets >  600 Hz
  // -100 dBc for offsets >  900 Hz
  // -120 dBc for offsets > 1200 Hz
  //
  // ========================================================================
  //
  // Output: ramp[0] ... ramp[width] contain numbers
  // that smoothly grow from zero to one.
  // (yes, the length of the ramp is width+1)
  //
  for (int i = 0; i <= width; i++) {
    double y = (double) i / ((double) width);           // between 0 and 1
    double y2  = y * 6.2831853071795864769252867665590;  //  2 Pi y
    double y4  = y2 + y2;                                //  4 Pi y
    double y6  = y4 + y2;                                //  6 Pi y
    double y8  = y4 + y4;                                //  8 Pi y
    double y10 = y4 + y6;                                // 10 Pi y
    ramp[i] = y - 0.12182865361171612    * sin(y2)
                - 0.018557469249199286   * sin(y4)
                - 0.0009378783245428506  * sin(y6)
                + 0.0008567571519403228  * sin(y8)
                + 0.00018706912431472442 * sin(y10);
  }
}

#if 0
static void init_ve3nea_ramp(double *ramp, int width) {
  //
  // Calculate a "VE3NEA" ramp (integrated Blackman-Harris-Window)
  // Output: ramp[0] ... ramp[width] contain numbers
  // that smoothly grow from zero to one.
  // (yes, the length of the ramp is width+1)
  //
  for (int i = 0; i <= width; i++) {
    double y = (double) i / ((double) width);           // between 0 and 1
    double y2 = y * 6.2831853071795864769252867665590;  // 2 Pi y
    double y4 = y2 + y2;                                // 4 Pi y
    double y6 = y4 + y2;                                // 6 Pi y
    ramp[i] = y - 0.216623741219070588160524746528683032300505367020509  * sin(y2)
                + 0.0313385510244222620730702412393990649034542979097027 * sin(y4)
                - 0.0017272285577824274302258419105142557477932531124260 * sin(y6);
  }
}
#endif

void reconfigure_transmitter(TRANSMITTER *tx, int width, int height) {
  if (width != tx->width || height != tx->height) {
    g_mutex_lock(&tx->display_mutex);
    t_print("reconfigure_transmitter: width=%d height=%d\n", width, height);
    tx->width = width;
    tx->height = height;
    gtk_widget_set_size_request(tx->panel, width, height);
    //
    // Upon calling, width either equals display_width (non-duplex) and
    // the *shown* TX spectrum is 24 kHz wide, or width equals 1/4 display_width (duplex)
    // and the *shown* TX spectrum is 6 kHz wide. In both cases, display_width pixels
    // correspond to 24 kHz, while the width of the whole spectrum is TXIQ.
    // The mic sample rate is fixed to 48k , so ratio is TXIQ/24k.
    // The value of tx->pixels corresponds to the *full* TX spectrum in the
    // target resolution.
    //
    tx->pixels = display_width * tx->ratio * 2;
    g_free(tx->pixel_samples);
    tx->pixel_samples = g_new(float, tx->pixels);
    init_analyzer(tx);
    g_mutex_unlock(&tx->display_mutex);
  }

  gtk_widget_set_size_request(tx->panadapter, width, height);
}

void transmitterSaveState(const TRANSMITTER *tx) {
  t_print("%s: TX=%d\n", __FUNCTION__, tx->id);
  SetPropI1("transmitter.%d.fft_size",          tx->id,               tx->fft_size);
  SetPropI1("transmitter.%d.fps",               tx->id,               tx->fps);
  SetPropI1("transmitter.%d.filter_low",        tx->id,               tx->filter_low);
  SetPropI1("transmitter.%d.filter_high",       tx->id,               tx->filter_high);
  SetPropI1("transmitter.%d.use_rx_filter",     tx->id,               tx->use_rx_filter);
  SetPropI1("transmitter.%d.alex_antenna",      tx->id,               tx->alex_antenna);
  SetPropI1("transmitter.%d.panadapter_low",    tx->id,               tx->panadapter_low);
  SetPropI1("transmitter.%d.panadapter_high",   tx->id,               tx->panadapter_high);
  SetPropI1("transmitter.%d.local_microphone",  tx->id,               tx->local_microphone);
  SetPropS1("transmitter.%d.microphone_name",   tx->id,               tx->microphone_name);
  SetPropI1("transmitter.%d.puresignal",        tx->id,               tx->puresignal);
  SetPropI1("transmitter.%d.auto_on",           tx->id,               tx->auto_on);
  SetPropI1("transmitter.%d.feedback",          tx->id,               tx->feedback);
  SetPropF1("transmitter.%d.ps_ampdelay",       tx->id,               tx->ps_ampdelay);
  SetPropI1("transmitter.%d.ps_oneshot",        tx->id,               tx->ps_oneshot);
  SetPropI1("transmitter.%d.ps_ints",           tx->id,               tx->ps_ints);
  SetPropI1("transmitter.%d.ps_spi",            tx->id,               tx->ps_spi);
  SetPropI1("transmitter.%d.ps_stbl",           tx->id,               tx->ps_stbl);
  SetPropI1("transmitter.%d.ps_map",            tx->id,               tx->ps_map);
  SetPropI1("transmitter.%d.ps_pin",            tx->id,               tx->ps_pin);
  SetPropF1("transmitter.%d.ps_ptol",           tx->id,               tx->ps_ptol);
  SetPropF1("transmitter.%d.ps_moxdelay",       tx->id,               tx->ps_moxdelay);
  SetPropF1("transmitter.%d.ps_loopdelay",      tx->id,               tx->ps_loopdelay);
  SetPropI1("transmitter.%d.attenuation",       tx->id,               tx->attenuation);
  SetPropI1("transmitter.%d.ctcss_enabled",     tx->id,               tx->ctcss_enabled);
  SetPropI1("transmitter.%d.ctcss",             tx->id,               tx->ctcss);
  SetPropI1("transmitter.%d.deviation",         tx->id,               tx->deviation);
  SetPropF1("transmitter.%d.am_carrier_level",  tx->id,               tx->am_carrier_level);
  SetPropI1("transmitter.%d.drive",             tx->id,               tx->drive);
  SetPropI1("transmitter.%d.tune_drive",        tx->id,               tx->tune_drive);
  SetPropI1("transmitter.%d.tune_use_drive",    tx->id,               tx->tune_use_drive);
  SetPropI1("transmitter.%d.swr_protection",    tx->id,               tx->swr_protection);
  SetPropF1("transmitter.%d.swr_alarm",         tx->id,               tx->swr_alarm);
  SetPropI1("transmitter.%d.drive_level",       tx->id,               tx->drive_level);
  SetPropF1("transmitter.%d.drive_scale",       tx->id,               tx->drive_scale);
  SetPropF1("transmitter.%d.drive_iscal",       tx->id,               tx->drive_iscal);
  SetPropI1("transmitter.%d.do_scale",          tx->id,               tx->do_scale);
  SetPropI1("transmitter.%d.compressor",        tx->id,               tx->compressor);
  SetPropF1("transmitter.%d.compressor_level",  tx->id,               tx->compressor_level);
  SetPropI1("transmitter.%d.dialog_x",          tx->id,               tx->dialog_x);
  SetPropI1("transmitter.%d.dialog_y",          tx->id,               tx->dialog_y);
  SetPropI1("transmitter.%d.display_filled",    tx->id,               tx->display_filled);
  SetPropI1("transmitter.%d.eq_enable", tx->id,                       tx->eq_enable);
  SetPropI1("transmitter.%d.eq_tenband", tx->id,                      tx->eq_tenband);

  for (int i = 0; i < 11; i++) {
    SetPropF2("transmitter.%d.eq_freq[%d]", tx->id, i,             tx->eq_freq[i]);
    SetPropF2("transmitter.%d.eq_gain[%d]", tx->id, i,             tx->eq_gain[i]);
  }
}

static void transmitterRestoreState(TRANSMITTER *tx) {
  t_print("%s: id=%d\n", __FUNCTION__, tx->id);
  GetPropI1("transmitter.%d.fft_size",          tx->id,               tx->fft_size);
  GetPropI1("transmitter.%d.fps",               tx->id,               tx->fps);
  GetPropI1("transmitter.%d.filter_low",        tx->id,               tx->filter_low);
  GetPropI1("transmitter.%d.filter_high",       tx->id,               tx->filter_high);
  GetPropI1("transmitter.%d.use_rx_filter",     tx->id,               tx->use_rx_filter);
  GetPropI1("transmitter.%d.alex_antenna",      tx->id,               tx->alex_antenna);
  GetPropI1("transmitter.%d.panadapter_low",    tx->id,               tx->panadapter_low);
  GetPropI1("transmitter.%d.panadapter_high",   tx->id,               tx->panadapter_high);
  GetPropI1("transmitter.%d.local_microphone",  tx->id,               tx->local_microphone);
  GetPropS1("transmitter.%d.microphone_name",   tx->id,               tx->microphone_name);
  GetPropI1("transmitter.%d.puresignal",        tx->id,               tx->puresignal);
  GetPropI1("transmitter.%d.auto_on",           tx->id,               tx->auto_on);
  GetPropI1("transmitter.%d.feedback",          tx->id,               tx->feedback);
  GetPropF1("transmitter.%d.ps_ampdelay",       tx->id,               tx->ps_ampdelay);
  GetPropI1("transmitter.%d.ps_oneshot",        tx->id,               tx->ps_oneshot);
  GetPropI1("transmitter.%d.ps_ints",           tx->id,               tx->ps_ints);
  GetPropI1("transmitter.%d.ps_spi",            tx->id,               tx->ps_spi);
  GetPropI1("transmitter.%d.ps_stbl",           tx->id,               tx->ps_stbl);
  GetPropI1("transmitter.%d.ps_map",            tx->id,               tx->ps_map);
  GetPropI1("transmitter.%d.ps_pin",            tx->id,               tx->ps_pin);
  GetPropF1("transmitter.%d.ps_ptol",           tx->id,               tx->ps_ptol);
  GetPropF1("transmitter.%d.ps_moxdelay",       tx->id,               tx->ps_moxdelay);
  GetPropF1("transmitter.%d.ps_loopdelay",      tx->id,               tx->ps_loopdelay);
  GetPropI1("transmitter.%d.attenuation",       tx->id,               tx->attenuation);
  GetPropI1("transmitter.%d.ctcss_enabled",     tx->id,               tx->ctcss_enabled);
  GetPropI1("transmitter.%d.ctcss",             tx->id,               tx->ctcss);
  GetPropI1("transmitter.%d.deviation",         tx->id,               tx->deviation);
  GetPropF1("transmitter.%d.am_carrier_level",  tx->id,               tx->am_carrier_level);
  GetPropI1("transmitter.%d.drive",             tx->id,               tx->drive);
  GetPropI1("transmitter.%d.tune_drive",        tx->id,               tx->tune_drive);
  GetPropI1("transmitter.%d.tune_use_drive",    tx->id,               tx->tune_use_drive);
  GetPropI1("transmitter.%d.swr_protection",    tx->id,               tx->swr_protection);
  GetPropF1("transmitter.%d.swr_alarm",         tx->id,               tx->swr_alarm);
  GetPropI1("transmitter.%d.drive_level",       tx->id,               tx->drive_level);
  GetPropF1("transmitter.%d.drive_scale",       tx->id,               tx->drive_scale);
  GetPropF1("transmitter.%d.drive_iscal",       tx->id,               tx->drive_iscal);
  GetPropI1("transmitter.%d.do_scale",          tx->id,               tx->do_scale);
  GetPropI1("transmitter.%d.compressor",        tx->id,               tx->compressor);
  GetPropF1("transmitter.%d.compressor_level",  tx->id,               tx->compressor_level);
  GetPropI1("transmitter.%d.dialog_x",          tx->id,               tx->dialog_x);
  GetPropI1("transmitter.%d.dialog_y",          tx->id,               tx->dialog_y);
  GetPropI1("transmitter.%d.display_filled",    tx->id,               tx->display_filled);
  GetPropI1("transmitter.%d.eq_enable", tx->id,                       tx->eq_enable);
  GetPropI1("transmitter.%d.eq_tenband", tx->id,                      tx->eq_tenband);

  for (int i = 0; i < 11; i++) {
    GetPropF2("transmitter.%d.eq_freq[%d]", tx->id, i,             tx->eq_freq[i]);
    GetPropF2("transmitter.%d.eq_gain[%d]", tx->id, i,             tx->eq_gain[i]);
  }
}

static double compute_power(double p) {
  double interval = 0.1 * pa_power_list[pa_power];
  int i = 0;

  if (p > pa_trim[10]) {
    i = 9;
  } else {
    while (p > pa_trim[i]) {
      i++;
    }

    if (i > 0) { i--; }
  }

  double frac = (p - pa_trim[i]) / (pa_trim[i + 1] - pa_trim[i]);
  return interval * ((1.0 - frac) * (double)i + frac * (double)(i + 1));
}

static gboolean update_display(gpointer data) {
  TRANSMITTER *tx = (TRANSMITTER *)data;
  int rc;

  //t_print("update_display: tx id=%d\n",tx->id);
  if (tx->displaying) {
    // if "MON" button is active (tx->feedback is TRUE),
    // then obtain spectrum pixels from PS_RX_FEEDBACK,
    // that is, display the (attenuated) TX signal from the "antenna"
    //
    // POSSIBLE MISMATCH OF SAMPLE RATES IN ORIGINAL PROTOCOL:
    // TX sample rate is fixed 48 kHz, but RX sample rate can be
    // 2*, 4*, or even 8* larger. The analyzer has been set up to use
    // more pixels in this case, so we just need to copy the
    // inner part of the spectrum.
    // If both spectra have the same number of pixels, this code
    // just copies all of them
    //
    g_mutex_lock(&tx->display_mutex);

    if (tx->puresignal && tx->feedback) {
      RECEIVER *rx_feedback = receiver[PS_RX_FEEDBACK];
      g_mutex_lock(&rx_feedback->display_mutex);
      GetPixels(rx_feedback->id, 0, rx_feedback->pixel_samples, &rc);

      if (rc) {
        int full  = rx_feedback->pixels;  // number of pixels in the feedback spectrum
        int width = tx->pixels;           // number of pixels to copy from the feedback spectrum
        int start = (full - width) / 2;   // Copy from start ... (end-1)
        float *tfp = tx->pixel_samples;
        float *rfp = rx_feedback->pixel_samples + start;
        float offset;
        int i;

        //
        // The TX panadapter shows a RELATIVE signal strength. A CW or single-tone signal at
        // full drive appears at 0dBm, the two peaks of a full-drive two-tone signal appear
        // at -6 dBm each. THIS DOES NOT DEPEND ON THE POSITION OF THE DRIVE LEVEL SLIDER.
        // The strength of the feedback signal, however, depends on the drive, on the PA and
        // on the attenuation effective in the feedback path.
        // We try to shift the RX feeback signal such that is looks like a "normal" TX
        // panadapter if the feedback is optimal for PureSignal (that is, if the attenuation
        // is optimal). The correction (offset) depends on the FPGA software inside the radio
        // (diffent peak levels in the TX feedback channel).
        //
        // The (empirically) determined offset is 4.2 - 20*Log10(GetPk value), it is the larger
        // the smaller the amplitude of the RX feedback signal is.
        //
        switch (protocol) {
        case ORIGINAL_PROTOCOL:
          // TX dac feedback peak = 0.406, on HermesLite2 0.230
          offset = (device == DEVICE_HERMES_LITE2) ? 17.0 : 12.0;
          break;

        case NEW_PROTOCOL:
          // TX dac feedback peak = 0.2899, on SATURN 0.6121
          offset = (device == NEW_DEVICE_SATURN) ? 8.5 : 15.0;
          break;

        default:
          // we probably never come here
          offset = 0.0;
          break;
        }

        for (i = 0; i < width; i++) {
          *tfp++ = *rfp++ + offset;
        }
      }

      g_mutex_unlock(&rx_feedback->display_mutex);
    } else {
      GetPixels(tx->id, 0, tx->pixel_samples, &rc);
    }

    if (rc) {
      tx_panadapter_update(tx);
    }

    g_mutex_unlock(&tx->display_mutex);
    tx->alc = GetTXAMeter(tx->id, alc);
    double constant1;
    double constant2;
    double rconstant2;  // allow different C2 values for calculating fwd and ref power
    int fwd_cal_offset;
    int rev_cal_offset;
    int fwd_power;
    int rev_power;
    double v1;
    rc = get_tx_vfo();
    int is6m = (vfo[rc].band == band6);
    //
    // Updated values of constant1/2 throughout,
    // taking the values from the Thetis
    // repository.
    //
    fwd_power   = alex_forward_power;
    rev_power   = alex_reverse_power;

    switch (device) {
    default:
      //
      // This is meant to lead to tx->fwd = 0 and tx->rev = 0
      //
      constant1 = 1.0;
      constant2 = 1.0;
      rconstant2 = 1.0;
      rev_cal_offset = 0;
      fwd_cal_offset = 0;
      fwd_power = 0;
      rev_power = 0;
      break;
#ifdef USBOZY

    case DEVICE_OZY:
      if (filter_board == ALEX) {
        constant1 = 3.3;
        constant2 = 0.09;
        rconstant2 = 0.09;
        rev_cal_offset = 3;
        fwd_cal_offset = 6;
        fwd_power = penny_fp;
        rev_power = penny_rp;
      } else {
        constant1 = 3.3;
        constant2 = 18.0;
        rconstant2 = 18.0;
        rev_cal_offset = 0;
        fwd_cal_offset = 90;
        fwd_power = penny_alc;
        rev_power = 0;
      }

      break;
#endif

    case DEVICE_METIS:
    case DEVICE_HERMES:
    case DEVICE_ANGELIA:
    case NEW_DEVICE_HERMES2:
    case NEW_DEVICE_ANGELIA:
      constant1 = 3.3;
      constant2 = 0.095;
      rconstant2 = is6m ? 0.5 : 0.095;
      rev_cal_offset = 3;
      fwd_cal_offset = 6;
      break;

    case DEVICE_ORION:  // Anan200D
    case NEW_DEVICE_ORION:
      constant1 = 5.0;
      constant2 = 0.108;
      rconstant2 = is6m ? 0.5 : 0.108;
      rev_cal_offset = 2;
      fwd_cal_offset = 4;
      break;

    case DEVICE_ORION2:  // Anan7000/8000/G2
    case NEW_DEVICE_ORION2:
    case NEW_DEVICE_SATURN:
      if (pa_power == PA_100W) {
        // ANAN-7000  values.
        constant1 = 5.0;
        constant2 = 0.12;
        rconstant2 = is6m ? 0.7 : 0.15;
        rev_cal_offset = 28;
        fwd_cal_offset = 32;
      } else {
        // Anan-8000 values
        constant1 = 5.0;
        constant2 = 0.08;
        rconstant2 = 0.08;
        rev_cal_offset = 16;
        fwd_cal_offset = 18;
      }

      break;

    case DEVICE_HERMES_LITE:
    case DEVICE_HERMES_LITE2:
    case NEW_DEVICE_HERMES_LITE:
    case NEW_DEVICE_HERMES_LITE2:
      //
      // These values are a fit to the "HL2FilterE3" data in Quisk.
      // No difference in the Fwd and Rev formula.
      //
      constant1 = 3.3;
      constant2 = 1.52;
      rconstant2 = 1.52;
      rev_cal_offset = -34;
      fwd_cal_offset = -34;
      break;
    }

    //
    // Special hook for HL2s with an incorrectly wound current
    // sense transformer: Exchange fwd and rev readings
    //
    if (device == DEVICE_HERMES_LITE || device == DEVICE_HERMES_LITE2 ||
        device == NEW_DEVICE_HERMES_LITE || device == NEW_DEVICE_HERMES_LITE2) {
      if (rev_power > fwd_power) {
        fwd_power   = alex_reverse_power;
        rev_power   = alex_forward_power;
      }
    }

    fwd_power = fwd_power - fwd_cal_offset;
    rev_power = rev_power - rev_cal_offset;

    if (fwd_power < 0) { fwd_power = 0; }

    if (rev_power < 0) { rev_power = 0; }

    v1 = ((double)fwd_power / 4095.0) * constant1;
    tx->fwd = (v1 * v1) / constant2;
    v1 = ((double)rev_power / 4095.0) * constant1;
    tx->rev = (v1 * v1) / rconstant2;
    //
    // compute_power does an interpolation is user-supplied pairs of
    // data points (measured by radio, measured by external watt meter)
    // are available.
    //
    tx->rev  = compute_power(tx->rev);
    tx->fwd = compute_power(tx->fwd);

    //
    // Calculate SWR and store as tx->swr.
    // tx->swr can be used in other parts of the program to
    // implement SWR protection etc.
    // The SWR is calculated from the (time-averaged) forward and reverse voltages.
    // Take care that no division by zero can happen, since otherwise the moving
    // exponential average cannot survive.
    //
    //
    if (tx->fwd > 0.25) {
      //
      // SWR means VSWR (voltage based) but we have the forward and
      // reflected power, so correct for that
      //
      double gamma = sqrt(tx->rev / tx->fwd);

      //
      // this prevents SWR going to infinity, from which the
      // moving average cannot recover
      //
      if (gamma > 0.95) { gamma = 0.95; }

      tx->swr = 0.7 * (1.0 + gamma) / (1.0 - gamma) + 0.3 * tx->swr;
    } else {
      //
      // During RX, move towards 1.0
      //
      tx->swr = 0.7 + 0.3 * tx->swr;
    }

    //
    //  If SWR is above threshold emit a waring.
    //  If additionally  SWR protection is enabled,
    //  set the drive slider to zero. Do not do this while tuning
    //  To be sure that we do not shut down upon an artifact,
    //  it is required high SWR is seen in to subsequent calls.
    //
    static int pre_high_swr = 0;

    if (tx->swr >= tx->swr_alarm) {
      if (pre_high_swr) {
        if (tx->swr_protection && !getTune()) {
          set_drive(0.0);
        }

        high_swr_seen = 1;
      }

      pre_high_swr = 1;
    } else {
      pre_high_swr = 0;
    }

    if (!duplex) {
      meter_update(active_receiver, POWER, tx->fwd, tx->alc, tx->swr);
    }

    return TRUE; // keep going
  }

  return FALSE; // no more timer events
}

static void init_analyzer(TRANSMITTER *tx) {
  int flp[] = {0};
  const double keep_time = 0.1;
  const int n_pixout = 1;
  const int spur_elimination_ffts = 1;
  const int data_type = 1;
  const int window_type = 5;
  const double kaiser_pi = 14.0;
  const double fscLin = 0;
  const double fscHin = 0;
  const int stitches = 1;
  const int calibration_data_set = 0;
  const double span_min_freq = 0.0;
  const double span_max_freq = 0.0;
  const int clip = 0;
  int afft_size = 16384;
  int overlap;
  int pixels;
  pixels = tx->pixels;

  int max_w = afft_size + (int) min(keep_time * (double) tx->iq_output_rate,
                                    keep_time * (double) afft_size * (double) tx->fps);
  overlap = (int)max(0.0, ceil(afft_size - (double)tx->iq_output_rate / (double)tx->fps));
  t_print("SetAnalyzer id=%d buffer_size=%d overlap=%d pixels=%d\n", tx->id, tx->output_samples, overlap, tx->pixels);
  SetAnalyzer(tx->id,                // id of the TXA channel
              n_pixout,              // 1 = "use same data for scope and waterfall"
              spur_elimination_ffts, // 1 = "no spur elimination"
              data_type,             // 1 = complex input data (I & Q)
              flp,                   // vector with one elt for each LO frequency, 1 if high-side LO, 0 otherwise
              afft_size,             // size of the fft, i.e., number of input samples
              tx->output_samples,    // number of samples transferred for each OpenBuffer()/CloseBuffer()
              window_type,           // 4 = Hamming
              kaiser_pi,             // PiAlpha parameter for Kaiser window
              overlap,               // number of samples each fft (other than the first) is to re-use from the previous
              clip,                  // number of fft output bins to be clipped from EACH side of each sub-span
              fscLin,                // number of bins to clip from low end of entire span
              fscHin,                // number of bins to clip from high end of entire span
              pixels,                // number of pixel values to return.  may be either <= or > number of bins
              stitches,              // number of sub-spans to concatenate to form a complete span
              calibration_data_set,  // identifier of which set of calibration data to use
              span_min_freq,         // frequency at first pixel value8192
              span_max_freq,         // frequency at last pixel value
              max_w                  // max samples to hold in input ring buffers
             );
  //
  // This cannot be changed for the TX panel,
  // use peak mode
  //
  SetDisplayDetectorMode (tx->id,  0, DETECTOR_MODE_PEAK);
  SetDisplayAverageMode  (tx->id,  0, AVERAGE_MODE_LOG_RECURSIVE);
  SetDisplayNumAverage   (tx->id,  0, 4);
  SetDisplayAvBackmult   (tx->id,  0, 0.4000);
}

void create_dialog(TRANSMITTER *tx) {
  //t_print("create_dialog\n");
  tx->dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(tx->dialog), GTK_WINDOW(top_window));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(tx->dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), FALSE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "TX");
  g_signal_connect (tx->dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (tx->dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(tx->dialog));
  //t_print("create_dialog: add tx->panel\n");
  gtk_widget_set_size_request (tx->panel, display_width / 4, display_height / 2);
  gtk_container_add(GTK_CONTAINER(content), tx->panel);
  gtk_widget_add_events(tx->dialog, GDK_KEY_PRESS_MASK);
  g_signal_connect(tx->dialog, "key_press_event", G_CALLBACK(keypress_cb), NULL);
}

static void create_visual(TRANSMITTER *tx) {
  t_print("transmitter: create_visual: id=%d width=%d height=%d\n", tx->id, tx->width, tx->height);
  tx->dialog = NULL;
  tx->panel = gtk_fixed_new();
  gtk_widget_set_size_request (tx->panel, tx->width, tx->height);

  if (tx->display_panadapter) {
    tx_panadapter_init(tx, tx->width, tx->height);
    gtk_fixed_put(GTK_FIXED(tx->panel), tx->panadapter, 0, 0);
  }

  gtk_widget_show_all(tx->panel);
  g_object_ref((gpointer)tx->panel);

  if (duplex) {
    create_dialog(tx);
  }
}

TRANSMITTER *create_transmitter(int id, int width, int height) {
  int rc;
  TRANSMITTER *tx = g_new(TRANSMITTER, 1);
  tx->id = id;
  tx->dac = 0;
  tx->fps = 10;
  tx->display_filled = 0;
  tx->dsp_size = 2048;
  tx->fft_size = 2048;
  g_mutex_init(&tx->display_mutex);
  tx->update_timer_id = 0;
  tx->out_of_band_timer_id = 0;

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    tx->mic_sample_rate = 48000;   // sample rate of incoming audio signal
    tx->mic_dsp_rate = 48000;      // sample rate of TX signal processing within WDSP
    tx->iq_output_rate = 48000;    // output TX IQ sample rate
    tx->ratio = 1;
    break;

  case NEW_PROTOCOL:
    tx->mic_sample_rate = 48000;
    tx->mic_dsp_rate = 96000;
    tx->iq_output_rate = 192000;
    tx->ratio = 4;
    break;

  case SOAPYSDR_PROTOCOL:
    tx->mic_sample_rate = 48000;
    tx->mic_dsp_rate = 96000;
    tx->iq_output_rate = radio_sample_rate;  // MUST be a multiple of 48k
    tx->ratio = radio_sample_rate / 48000;
    break;
  }

  //
  // Adjust buffer size according to the (fixed) IQ sample rate:
  // Each mic (input) sample produces (iq_output_rate/mic_sample_rate) IQ samples,
  // therefore use smaller buffer sizer if the sample rate is larger.
  //
  // Many ANAN radios running P2 have a TX IQ FIFO which can hold about 4k samples,
  // here the buffer size should be at most 512 (producing 2048 IQ samples per
  // call)
  //
  // For PlutoSDR (TX sample rate fixed to 768000) I have done no experiments but
  // I think one should use an even smaller buffer size.
  //
  if (tx->iq_output_rate <= 96000) {
    tx->buffer_size = 1024;
  } else if (tx->iq_output_rate <= 384000) {
    tx->buffer_size = 512;
  } else {
    tx->buffer_size = 256;
  }

  tx->output_samples = tx->buffer_size * tx->ratio;
  tx->pixels = display_width * tx->ratio * 2;
  tx->width = width;
  tx->height = height;
  tx->display_panadapter = 1;
  tx->display_waterfall = 0;
  tx->panadapter_high = 0;
  tx->panadapter_low = -70;
  tx->panadapter_step = 10;
  tx->displaying = 0;
  tx->alex_antenna = 0; // default: ANT1
  t_print("create_transmitter: id=%d buffer_size=%d mic_sample_rate=%d mic_dsp_rate=%d iq_output_rate=%d output_samples=%d width=%d height=%d\n",
          tx->id, tx->buffer_size, tx->mic_sample_rate, tx->mic_dsp_rate, tx->iq_output_rate, tx->output_samples,
          tx->width, tx->height);
  tx->filter_low = tx_filter_low;
  tx->filter_high = tx_filter_high;
  tx->use_rx_filter = FALSE;
  tx->out_of_band = 0;
  tx->twotone = 0;
  tx->puresignal = 0;
  //
  // PS 2.0 default parameters
  //
  tx->ps_ampdelay = 150;      // ATTENTION: this value is in nano-seconds
  tx->ps_oneshot = 0;
  tx->ps_ints = 16;
  tx->ps_spi = 256;           // ints=16/spi=256 corresponds to "TINT=0.5 dB"
  tx->ps_stbl = 0;            // "Stbl" un-checked
  tx->ps_map = 1;             // "Map" checked
  tx->ps_pin = 1;             // "Pin" checked
  tx->ps_ptol = 0.8;          // "Relax Tolerance" un-checked
  tx->ps_moxdelay = 0.2;      // "MOX Wait" 0.2 sec
  tx->ps_loopdelay = 0.0;     // "CAL Wait" 0.0 sec
  tx->feedback = 0;
  tx->auto_on = 0;
  tx->attenuation = 0;
  tx->ctcss = 11;
  tx->ctcss_enabled = FALSE;
  tx->deviation = 2500;
  tx->am_carrier_level = 0.5;
  tx->drive = 50;
  tx->tune_drive = 10;
  tx->tune_use_drive = 0;
  tx->drive_level = 0;
  tx->drive_scale = 1.0;
  tx->drive_iscal = 1.0;
  tx->do_scale = 0;
  tx->compressor = 0;
  tx->compressor_level = 0.0;
  tx->local_microphone = 0;
  STRLCPY(tx->microphone_name, "NO MIC", 128);
  tx->dialog_x = -1;
  tx->dialog_y = -1;
  tx->swr = 1.0;
  tx->swr_protection = FALSE;
  tx->swr_alarm = 3.0;     // default value for SWR protection
  tx->alc = 0.0;
  tx->eq_enable = 0;
  tx->eq_tenband  = 0;
  tx->eq_freq[0]  =     0.0;
  tx->eq_freq[1]  =   200.0;
  tx->eq_freq[2]  =   500.0;
  tx->eq_freq[3]  =  1200.0;
  tx->eq_freq[4]  =  3000.0;
  tx->eq_freq[5]  =  5000.0;
  tx->eq_freq[6]  =  7000.0;
  tx->eq_freq[7]  =  9000.0;
  tx->eq_freq[8]  = 11000.0;
  tx->eq_freq[9]  = 13000.0;
  tx->eq_freq[10] = 15000.0;
  tx->eq_gain[0]  = 0.0;
  tx->eq_gain[1]  = 0.0;
  tx->eq_gain[2]  = 0.0;
  tx->eq_gain[3]  = 0.0;
  tx->eq_gain[4]  = 0.0;
  tx->eq_gain[5]  = 0.0;
  tx->eq_gain[6]  = 0.0;
  tx->eq_gain[7]  = 0.0;
  tx->eq_gain[8]  = 0.0;
  tx->eq_gain[9]  = 0.0;
  tx->eq_gain[10] = 0.0;
  transmitterRestoreState(tx);
  //
  // allocate buffers
  //
  t_print("transmitter: allocate buffers: mic_input_buffer=%d iq_output_buffer=%d pixels=%d\n", tx->buffer_size,
          tx->output_samples, tx->pixels);
  tx->mic_input_buffer = g_new(double, 2 * tx->buffer_size);
  tx->iq_output_buffer = g_new(double, 2 * tx->output_samples);
  tx->cw_sig_rf = g_new(double, tx->output_samples);
  tx->samples = 0;
  tx->pixel_samples = g_new(float, tx->pixels);
  g_mutex_init(&tx->cw_ramp_mutex);
  tx->cw_ramp_audio = NULL;
  tx->cw_ramp_rf    = NULL;
  tx_set_ramps(tx);
  t_print("create_transmitter: OpenChannel id=%d buffer_size=%d dsp_size=%d fft_size=%d sample_rate=%d dspRate=%d outputRate=%d\n",
          tx->id,
          tx->buffer_size,
          tx->dsp_size,
          tx->fft_size,
          tx->mic_sample_rate,
          tx->mic_dsp_rate,
          tx->iq_output_rate);
  OpenChannel(tx->id,                    // channel
              tx->buffer_size,           // in_size
              tx->dsp_size,              // dsp_size
              tx->mic_sample_rate,       // input_samplerate
              tx->mic_dsp_rate,          // dsp_rate
              tx->iq_output_rate,        // output_samplerate
              1,                         // type (1=transmit)
              0,                         // state (do not run yet)
              0.010, 0.025, 0.0, 0.010,  // DelayUp, SlewUp, DelayDown, SlewDown
              1);                        // Wait for data in fexchange0
  TXASetNC(tx->id, tx->fft_size);
  TXASetMP(tx->id, 0);  // Use "linear phase" for TX *always*
  SetTXABandpassWindow(tx->id, 1);
  SetTXABandpassRun(tx->id, 1);
  SetTXAFMEmphPosition(tx->id, pre_emphasize);
  SetTXACFIRRun(tx->id, SET(protocol == NEW_PROTOCOL)); // turned on if new protocol
  tx_set_equalizer(tx);
  transmitter_set_ctcss(tx, tx->ctcss_enabled, tx->ctcss);
  SetTXAAMSQRun(tx->id, 0);
  SetTXAosctrlRun(tx->id, 0);
  SetTXAALCAttack(tx->id, 1);
  SetTXAALCDecay(tx->id, 10);
  SetTXAALCSt(tx->id, 1); // turn it on (always on)
  SetTXALevelerAttack(tx->id, 1);
  SetTXALevelerDecay(tx->id, 500);
  SetTXALevelerTop(tx->id, 5.0);
  SetTXALevelerSt(tx->id, 0);
  SetTXAPreGenMode(tx->id, 0);
  SetTXAPreGenToneMag(tx->id, 0.0);
  SetTXAPreGenToneFreq(tx->id, 0.0);
  SetTXAPreGenRun(tx->id, 0);
  SetTXAPostGenMode(tx->id, 0);
  SetTXAPostGenToneMag(tx->id, 0.2);
  SetTXAPostGenTTMag(tx->id, 0.2, 0.2);
  SetTXAPostGenToneFreq(tx->id, 0.0);
  SetTXAPostGenRun(tx->id, 0);
  SetTXAPanelGain1(tx->id, pow(10.0, mic_gain * 0.05));
  SetTXAPanelRun(tx->id, 1);
  SetTXAFMDeviation(tx->id, (double)tx->deviation);
  SetTXAAMCarrierLevel(tx->id, tx->am_carrier_level);
  SetTXACompressorGain(tx->id, tx->compressor_level);
  SetTXACompressorRun(tx->id, tx->compressor);
  tx_set_mode(tx, get_tx_mode());
  XCreateAnalyzer(tx->id, &rc, 262144, 1, 1, "");

  if (rc != 0) {
    t_print("XCreateAnalyzer id=%d failed: %d\n", tx->id, rc);
  } else {
    init_analyzer(tx);
  }

  create_visual(tx);
  return tx;
}

void tx_set_mode(TRANSMITTER* tx, int mode) {
  if (tx != NULL) {
    if (mode == modeDIGU || mode == modeDIGL) {
      if (tx->drive > drive_digi_max + 0.5) {
        set_drive(drive_digi_max);
      }
    }

    SetTXAMode(tx->id, mode);
    tx_set_filter(tx);
  }
}

void tx_set_equalizer(TRANSMITTER *tx) {
  int numchan = tx->eq_tenband ? 11 : 5;
  SetTXAEQProfile(tx->id, numchan, tx->eq_freq, tx->eq_gain);
  SetTXAEQRun(tx->id, tx->eq_enable);
  //t_print("TX EQ enable=%d\n", tx->eq_enable);
  //for (int i = 0; i < numchan; i++) {
  //  t_print("TX EQ chan=%d freq=%f gain=%f\n", i, tx->eq_freq[i], tx->eq_gain[i]);
  //}
}

void tx_set_filter(TRANSMITTER *tx) {
  int txmode = get_tx_mode();
  // load default values
  int low  = tx_filter_low;
  int high = tx_filter_high;  // 0 < low < high
  int txvfo = get_tx_vfo();
  int rxvfo = active_receiver->id;
  tx->deviation = vfo[txvfo].deviation;

  if (tx->use_rx_filter) {
    //
    // Use only 'compatible' parts of RX filter settings
    // to change TX values (important for split operation)
    //
    int rxmode = vfo[rxvfo].mode;
    FILTER *mode_filters = filters[rxmode];
    const FILTER *filter = &mode_filters[vfo[rxvfo].filter];

    switch (rxmode) {
    case modeDSB:
    case modeAM:
    case modeSAM:
    case modeSPEC:
      high =  filter->high;
      break;

    case modeLSB:
    case modeDIGL:
      high = -filter->low;
      low  = -filter->high;
      break;

    case modeUSB:
    case modeDIGU:
      high = filter->high;
      low  = filter->low;
      break;
    }
  }

  switch (txmode) {
  case modeCWL:
  case modeCWU:
    // Our CW signal is always at zero in IQ space, but note
    // WDSP is by-passed anyway.
    tx->filter_low  = -150;
    tx->filter_high = 150;
    break;

  case modeDSB:
  case modeAM:
  case modeSAM:
  case modeSPEC:
    // disregard the "low" value and use (-high, high)
    tx->filter_low = -high;
    tx->filter_high = high;
    break;

  case modeLSB:
  case modeDIGL:
    // in IQ space, the filter edges are (-high, -low)
    tx->filter_low = -high;
    tx->filter_high = -low;
    break;

  case modeUSB:
  case modeDIGU:
    // in IQ space, the filter edges are (low, high)
    tx->filter_low = low;
    tx->filter_high = high;
    break;

  case modeFMN:

    // calculate filter size from deviation,
    // assuming that the highest AF frequency is 3000
    if (tx->deviation == 2500) {
      tx->filter_low = -5500; // Carson's rule: +/-(deviation + max_af_frequency)
      tx->filter_high = 5500; // deviation=2500, max freq = 3000
    } else {
      tx->filter_low = -8000; // deviation=5000, max freq = 3000
      tx->filter_high = 8000;
    }

    break;

  case modeDRM:
    tx->filter_low = 7000;
    tx->filter_high = 17000;
    break;
  }

  double fl = tx->filter_low;
  double fh = tx->filter_high;
  SetTXAFMDeviation(tx->id, (double)tx->deviation);
  SetTXABandpassFreqs(tx->id, fl, fh);
}

void tx_set_pre_emphasize(const TRANSMITTER *tx, int state) {
  SetTXAFMEmphPosition(tx->id, state);
}

static void full_tx_buffer(TRANSMITTER *tx) {
  long isample;
  long qsample;
  double gain;
  double *dp;
  int j;
  int error;
  int cwmode;
  int sidetone = 0;
  static int txflag = 0;
  // It is important to query the TX mode and tune only *once* within this function, to assure that
  // the two "if (cwmode)" clauses give the same result.
  // cwmode only valid in the old protocol, in the new protocol we use a different mechanism
  int txmode = get_tx_mode();
  cwmode = (txmode == modeCWL || txmode == modeCWU) && !tune && !tx->twotone;

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    gain = 32767.0; // 16 bit
    break;

  case NEW_PROTOCOL:
    gain = 8388607.0; // 24 bit
    break;

  case SOAPYSDR_PROTOCOL:
  default:
    // gain is not used, since samples are floating point
    gain = 1.0;
    break;
  }

  if (cwmode) {
    //
    // clear VOX peak level in case is it non-zero.
    //
    clear_vox();
    //
    // Note that WDSP is not needed, but we still call it (and discard the
    // results) since this  may help in correct slew-up and slew-down
    // of the TX engine. The mic input buffer is zeroed out in CW mode.
    //
    // The main reason why we do NOT constructe an artificial microphone
    // signal to generate the RF pulse is that we do not want MicGain
    // and equalizer settings to interfere.
    //
    fexchange0(tx->id, tx->mic_input_buffer, tx->iq_output_buffer, &error);
    //
    // Construct our CW TX signal in tx->iq_output_buffer for the sole
    // purpose of displaying them in the TX panadapter
    //
    dp = tx->iq_output_buffer;

    // These are the I/Q samples that describe our CW signal
    // The only use we make of it is displaying the spectrum.
    for (j = 0; j < tx->output_samples; j++) {
      *dp++ = 0.0;
      *dp++ = tx->cw_sig_rf[j];
    }
  } else {
    update_vox(tx);

    //
    // DL1YCF:
    // The FM pre-emphasis filter in WDSP has maximum unit
    // gain at about 3000 Hz, so that it attenuates at 300 Hz
    // by about 20 dB and at 1000 Hz by about 10 dB.
    // Natural speech has much energy at frequencies below 1000 Hz
    // which will therefore aquire only little energy, such that
    // FM sounds rather "thin".
    //
    // At the expense of having some distortion for the highest
    // frequencies, we amplify the mic samples here by 15 dB
    // when doing FM, such that enough "punch" remains after the
    // FM pre-emphasis filter.
    //
    // If ALC happens before FM pre-emphasis, this has little effect
    // since the additional gain applied here will most likely be
    // compensated by ALC, so it is important to have FM pre-emphasis
    // before ALC (checkbox in tx_menu checked, that is, pre_emphasis==0).
    //
    // Note that mic sample amplification has to be done after update_vox()
    //
    if (txmode == modeFMN && !tune) {
      for (int i = 0; i < 2 * tx->samples; i += 2) {
        tx->mic_input_buffer[i] *= 5.6234;  // 20*Log(5.6234) is 15
      }
    }

    fexchange0(tx->id, tx->mic_input_buffer, tx->iq_output_buffer, &error);

    if (error != 0) {
      t_print("full_tx_buffer: id=%d fexchange0: error=%d\n", tx->id, error);
    }
  }

  if (tx->displaying && !(tx->puresignal && tx->feedback)) {
    g_mutex_lock(&tx->display_mutex);
    Spectrum0(1, tx->id, 0, 0, tx->iq_output_buffer);
    g_mutex_unlock(&tx->display_mutex);
  }

  if (isTransmitting()) {
    if (tx->do_scale) {
      gain = gain * tx->drive_scale;
    }

    if (txflag == 0 && protocol == NEW_PROTOCOL) {
      //
      // this is the first time (after a pause) that we send TX samples
      // so send some "silence" to pre-fill the sample buffer to
      // suppress underflows if one of the following buckets comes
      // a little late.
      //
      for (j = 0; j < 1024; j++) {
        new_protocol_iq_samples(0, 0);
      }
    }

    txflag = 1;

    //
    //  When doing CW, we do not need WDSP since Q(t) = cw_sig_rf(t) and I(t)=0
    //  For the old protocol where the IQ and audio samples are tied together, we can
    //  easily generate a synchronous side tone
    //
    //  Note that the CW shape buffer is tied to the mic sample rate (48 kHz).
    //
    if (cwmode) {
      //
      // "pulse shape case":
      // directly produce the I/Q samples. For a continuous zero-frequency
      // carrier (as needed for CW) I(t)=1 and Q(t)=0 everywhere. We shape I(t)
      // with the pulse envelope. We also produce a side tone with same shape.
      // Note that tx->iq_output_buffer is not used. Therefore, all the
      // SetTXAPostGen functions are not needed for CW!
      //
      // "Side tone to radio" treatment:
      // old protocol: done HERE
      // new protocol: already done in add_mic_sample
      // soapy       : no audio to radio
      //
      switch (protocol) {
      case ORIGINAL_PROTOCOL: {
        //
        // An inspection of the IQ samples produced by WDSP when TUNEing shows
        // that the amplitude of the pulse is in I (in the range 0.0 - 1.0)
        // and Q should be zero
        // Note that we re-cycle the TXIQ pulse shape here to generate the
        // side tone sent to the radio.
        // Apply a minimum side tone volume for CAT CW messages.
        //
        int vol = cw_keyer_sidetone_volume;

        if (vol == 0 && CAT_cw_is_active) { vol = 12; }

        double sidevol = 64.0 * vol; // between 0.0 and 8128.0

        for (j = 0; j < tx->output_samples; j++) {
          double ramp = tx->cw_sig_rf[j];       // between 0.0 and 1.0
          isample = floor(gain * ramp + 0.5);   // always non-negative, isample is just the pulse envelope
          sidetone = sidevol * ramp * sine_generator(&p1radio, &p2radio, cw_keyer_sidetone_frequency);
          old_protocol_iq_samples(isample, 0, sidetone);
        }
      }
      break;

      case NEW_PROTOCOL:

        //
        // An inspection of the IQ samples produced by WDSP when TUNEing shows
        // that the amplitude of the pulse is in I (in the range 0.0 - 0.896)
        // and Q should be zero:
        // In the P2 WDSP TXA chain, there is a compensating FIR filter at the very end
        // that reduces the amplitude of a full-amplitude zero-frequency signal.
        //
        // This is why we apply the factor 0.896 HERE.
        //
        for (j = 0; j < tx->output_samples; j++) {
          double ramp = tx->cw_sig_rf[j];                  // between 0.0 and 1.0
          isample = floor(0.896 * gain * ramp + 0.5);      // always non-negative, isample is just the pulse envelope
          new_protocol_iq_samples(isample, 0);
        }

        break;
#ifdef SOAPYSDR

      case SOAPYSDR_PROTOCOL:

        //
        // No scaling, no audio.
        // generate audio samples to be sent to the radio
        //
        for (j = 0; j < tx->output_samples; j++) {
          double ramp = tx->cw_sig_rf[j];                   // between 0.0 and 1.0
          soapy_protocol_iq_samples(0.0F, (float)ramp);     // SOAPY: just convert double to float
        }

        break;
#endif
      }
    } else {
      //
      // Original code without pulse shaping and without side tone
      //
      for (j = 0; j < tx->output_samples; j++) {
        double is, qs;
        is = tx->iq_output_buffer[j * 2];
        qs = tx->iq_output_buffer[(j * 2) + 1];
        isample = is >= 0.0 ? (long)floor(is * gain + 0.5) : (long)ceil(is * gain - 0.5);
        qsample = qs >= 0.0 ? (long)floor(qs * gain + 0.5) : (long)ceil(qs * gain - 0.5);

        switch (protocol) {
        case ORIGINAL_PROTOCOL:
          old_protocol_iq_samples(isample, qsample, 0);
          break;

        case NEW_PROTOCOL:
          new_protocol_iq_samples(isample, qsample);
          break;
#ifdef SOAPYSDR

        case SOAPYSDR_PROTOCOL:
          // SOAPY: just convert the double IQ samples (is,qs) to float.
          soapy_protocol_iq_samples((float)is, (float)qs);
          break;
#endif
        }
      }
    }
  } else {   // isTransmitting()
    if (txflag == 1 && protocol == NEW_PROTOCOL) {
      //
      // We arrive here ONCE after a TX -> RX transition
      // and send 240 zero samples to make sure the the
      // 0...239 samples that remain in the ring buffer
      // after stopping the TX are zero and won't produce
      // an unwanted signal after the next RX -> TX transition.
      //
      for (j = 0; j < 240; j++) {
        new_protocol_iq_samples(0, 0);
      }
    }

    txflag = 0;
  }
}

void add_mic_sample(TRANSMITTER *tx, float mic_sample) {
  int txmode = get_tx_mode();
  double mic_sample_double;
  int i, j;
  mic_sample_double = (double)mic_sample;

  //
  // If there is captured data to re-play, replace incoming
  // mic samples by captured data.
  //
  if (capture_state == CAP_REPLAY) {
    if (capture_replay_pointer < capture_record_pointer) {
      mic_sample_double = capture_data[capture_replay_pointer++];
    } else {
      // switching the state to REPLAY_DONE takes care that the
      // CAPTURE switch is "pressed" only once
      capture_state = CAP_REPLAY_DONE;
      schedule_action(CAPTURE, PRESSED, 0);
    }
  }

  //
  // silence TX audio if tuning or when doing CW,
  // to prevent firing VOX
  // (perhaps not really necessary, but can do no harm)
  //
  if (tune || txmode == modeCWL || txmode == modeCWU) {
    mic_sample_double = 0.0;
  }

  //
  // shape CW pulses when doing CW and transmitting, else nullify them
  //
  if ((txmode == modeCWL || txmode == modeCWU) && isTransmitting()) {
    int updown;
    float cwsample;
    //
    //  'piHPSDR' CW sets the variables cw_key_up and cw_key_down
    //  to the number of samples for the next down/up sequence.
    //  cw_key_down can be zero, for inserting some space
    //
    //  We HAVE TO shape the signal to avoid hard clicks to be
    //  heard way beside our frequency. The envelope (ramp function)
    //  is stored in cw_ramp_rf[0::cw_ramp_rf_len], so we "move" the
    //  pointer cw_ramp_rf_ptr between the edges.
    //
    //  In the same way, cw_ramp_audio, cw_ramp_audio_len,
    //  and cw_ramp_audio_ptr are used to shape the envelope of
    //  the side tone pulse.
    //
    //  Note that usually, the pulse is much broader than the ramp,
    //  that is, cw_key_down and cw_key_up are much larger than
    //  the ramp length.
    //
    //  We arrive here once per microphone sample. This means, that
    //  we have to produce tx->ratio RF samples and one sidetone
    //  sample.
    //
    cw_not_ready = 0;

    if (cw_key_down > 0 ) {
      cw_key_down--;            // decrement key-up counter
      updown = 1;
    } else {
      if (cw_key_up > 0) {
        cw_key_up--;  // decrement key-down counter
      }

      updown = 0;
    }

    //
    // Shape RF pulse and side tone.
    //
    j = tx->ratio * tx->samples; // pointer into cw_rf_sig

    if (g_mutex_trylock(&tx->cw_ramp_mutex)) {
      double val;

      if (updown) {
        if (tx->cw_ramp_audio_ptr < tx->cw_ramp_audio_len) {
          tx->cw_ramp_audio_ptr++;
        }

        val = tx->cw_ramp_audio[tx->cw_ramp_audio_ptr];

        for (i = 0; i < tx->ratio; i++) {
          if (tx->cw_ramp_rf_ptr < tx->cw_ramp_rf_len) {
            tx->cw_ramp_rf_ptr++;
          }

          tx->cw_sig_rf[j++] = tx->cw_ramp_rf[tx->cw_ramp_rf_ptr];
        }
      } else {
        if (tx->cw_ramp_audio_ptr > 0) {
          tx->cw_ramp_audio_ptr--;
        }

        val = tx->cw_ramp_audio[tx->cw_ramp_audio_ptr];

        for (i = 0; i < tx->ratio; i++) {
          if (tx->cw_ramp_rf_ptr > 0) {
            tx->cw_ramp_rf_ptr--;
          }

          tx->cw_sig_rf[j++] = tx->cw_ramp_rf[tx->cw_ramp_rf_ptr];
        }
      }

      // Apply a minimum side tone volume for CAT CW messages.
      int vol = cw_keyer_sidetone_volume;

      if (vol == 0 && CAT_cw_is_active) { vol = 12; }

      cwsample = 0.00196 * vol * val * sine_generator(&p1local, &p2local, cw_keyer_sidetone_frequency);
      g_mutex_unlock(&tx->cw_ramp_mutex);
    } else {
      //
      // This can happen if the CW ramp width is changed while transmitting
      // Simply insert a "hard zero".
      //
      cwsample = 0.0;

      for (i = 0; i < tx->ratio; i++) {
        tx->cw_sig_rf[j++] = 0.0;
      }
    }

    //
    // cw_keyer_sidetone_volume is in the range 0...127 so cwsample is 0.00 ... 0.25
    //
    if (active_receiver->local_audio) {
      cw_audio_write(active_receiver, cwsample);
    }

    //
    // In the new protocol, we MUST maintain a constant flow of audio samples to the radio
    // (at least for ANAN-200D and ANAN-7000 internal side tone generation)
    // So we ship out audio: silence if CW is internal, side tone if CW is local.
    //
    // Furthermore, for each audio sample we have to create four TX samples. If we are at
    // the beginning of the ramp, these are four zero samples, if we are at the, it is
    // four unit samples, and in-between, we use the values from cwramp192.
    // Note that this ramp has been extended a little, such that it begins with four zeros
    // and ends with four times 1.0.
    //
    if (protocol == NEW_PROTOCOL) {
      int s = 0;

      //
      // The scaling should ensure that a piHPSDR-generated side tone
      // has the same volume than a FGPA-generated one.
      // Note cwsample = 0.00196 * level = 0.0 ... 0.25
      //
      if (!cw_keyer_internal || CAT_cw_is_active) {
        if (device == NEW_DEVICE_SATURN) {
          //
          // This comes from an analysis of the G2 sidetone
          // data path:
          // level 0...127 ==> amplitude 0...32767
          //
          s = (int) (cwsample * 131000.0);
        } else {
          //
          // This factor has been measured on my ANAN-7000 and implies
          // level 0...127 ==> amplitude 0...8191
          //
          s = (int) (cwsample * 32768.0);
        }
      }

      new_protocol_cw_audio_samples(s, s);
    }
  } else {
    //
    //  If no longer transmitting, or no longer doing CW: reset pulse shaper.
    //  This will also swallow any pending CW and wipe out the buffers
    //  In order to tell rigctl etc. that CW should be aborted, we also use the cw_not_ready flag.
    //
    cw_not_ready = 1;
    cw_key_up = 0;

    if (cw_key_down > 0) { cw_key_down--; }  // in case it occured before the RX/TX transition

    tx->cw_ramp_audio_ptr = 0;
    tx->cw_ramp_rf_ptr = 0;
    // insert "silence" in CW audio and TX IQ buffers
    j = tx->ratio * tx->samples;

    for (i = 0; i < tx->ratio; i++) {
      tx->cw_sig_rf[j++] = 0.0;
    }
  }

  tx->mic_input_buffer[tx->samples * 2] = mic_sample_double;
  tx->mic_input_buffer[(tx->samples * 2) + 1] = 0.0; //mic_sample_double;
  tx->samples++;

  if (tx->samples == tx->buffer_size) {
    full_tx_buffer(tx);
    tx->samples = 0;
  }
}

void add_ps_iq_samples(const TRANSMITTER *tx, double i_sample_tx, double q_sample_tx, double i_sample_rx,
                       double q_sample_rx) {
  RECEIVER *tx_feedback = receiver[PS_TX_FEEDBACK];
  RECEIVER *rx_feedback = receiver[PS_RX_FEEDBACK];

  //t_print("add_ps_iq_samples: samples=%d i_rx=%f q_rx=%f i_tx=%f q_tx=%f\n",rx_feedback->samples, i_sample_rx,q_sample_rx,i_sample_tx,q_sample_tx);
  if (tx->do_scale) {
    tx_feedback->iq_input_buffer[tx_feedback->samples * 2] = i_sample_tx * tx->drive_iscal;
    tx_feedback->iq_input_buffer[(tx_feedback->samples * 2) + 1] = q_sample_tx * tx->drive_iscal;
  } else {
    tx_feedback->iq_input_buffer[tx_feedback->samples * 2] = i_sample_tx;
    tx_feedback->iq_input_buffer[(tx_feedback->samples * 2) + 1] = q_sample_tx;
  }

  rx_feedback->iq_input_buffer[rx_feedback->samples * 2] = i_sample_rx;
  rx_feedback->iq_input_buffer[(rx_feedback->samples * 2) + 1] = q_sample_rx;
  tx_feedback->samples = tx_feedback->samples + 1;
  rx_feedback->samples = rx_feedback->samples + 1;

  if (rx_feedback->samples >= rx_feedback->buffer_size) {
    if (isTransmitting()) {
      int txmode = get_tx_mode();
      int cwmode = (txmode == modeCWL || txmode == modeCWU) && !tune && !tx->twotone;
#if 0
      //
      // Special code to document the amplitude of the TX IQ samples.
      // This can be used to determine the "PK" value for an unknown
      // radio.
      //
      double pkmax = 0.0, pkval;

      for (int i = 0; i < rx_feedback->buffer_size; i++) {
        pkval = tx_feedback->iq_input_buffer[2 * i] * tx_feedback->iq_input_buffer[2 * i] +
                tx_feedback->iq_input_buffer[2 * i + 1] * tx_feedback->iq_input_buffer[2 * i + 1];

        if (pkval > pkmax) { pkmax = pkval; }
      }

      t_print("PK MEASURED: %f\n", sqrt(pkmax));
#endif

      if (!cwmode) {
        //
        // Since we are not using WDSP in CW transmit, it also makes little sense to
        // deliver feedback samples
        //
        pscc(tx->id, rx_feedback->buffer_size, tx_feedback->iq_input_buffer, rx_feedback->iq_input_buffer);
      }

      if (tx->displaying && tx->feedback) {
        g_mutex_lock(&rx_feedback->display_mutex);
        Spectrum0(1, rx_feedback->id, 0, 0, rx_feedback->iq_input_buffer);
        g_mutex_unlock(&rx_feedback->display_mutex);
      }
    }

    rx_feedback->samples = 0;
    tx_feedback->samples = 0;
  }
}

void tx_set_displaying(TRANSMITTER *tx, int state) {
  tx->displaying = state;

  if (state) {
    if (tx->update_timer_id > 0) {
      g_source_remove(tx->update_timer_id);
    }

    tx->update_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 1000 / tx->fps, update_display, (gpointer)tx,
                          NULL);
  } else {
    if (tx->update_timer_id > 0) {
      g_source_remove(tx->update_timer_id);
      tx->update_timer_id = 0;
    }
  }
}

void tx_set_ps(TRANSMITTER *tx, int state) {
  //
  // Switch PureSignal on (state !=0) or off (state==0)
  //
  // The following rules must be obeyed:
  //
  // a.) do not call SetPSControl unless you know the feedback
  //     data streams are flowing. Otherwise, these calls may
  //     be have no effect (experimental observation)
  //
  // b.  in the old protocol, do not change the value of
  //     tx->puresignal unless the protocol is stopped.
  //     (to have a safe re-configuration of the number of
  //     RX streams)
  //
  if (!state) {
    // if switching off, stop PS engine first, keep feedback
    // streams flowing for a while to be sure SetPSControl works.
    SetPSControl(tx->id, 1, 0, 0, 0);
    usleep(100000);
  }

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    // stop protocol, change PS state, restart protocol
    old_protocol_stop();
    usleep(100000);
    tx->puresignal = SET(state);
    old_protocol_run();
    break;

  case NEW_PROTOCOL:
    // change PS state and tell radio about it
    tx->puresignal = SET(state);
    schedule_high_priority();
    schedule_receive_specific();
#ifdef SOAPY_SDR

  case SOAPY_PROTOCOL:
    // no feedback channels in SOAPY
    break;
#endif
  }

  if (state) {
    // if switching on: wait a while to get the feedback
    // streams flowing, then start PS engine
    usleep(100000);

    if (tx->ps_oneshot) {
      SetPSControl(tx->id, 0, 1, 0, 0);
    } else {
      SetPSControl(tx->id, 0, 0, 1, 0);
    }

    // Set PS 2.0 parameters
    SetPSIntsAndSpi(transmitter->id, transmitter->ps_ints, transmitter->ps_spi);
    SetPSStabilize(transmitter->id, transmitter->ps_stbl);
    SetPSMapMode(transmitter->id, transmitter->ps_map);
    SetPSPinMode(transmitter->id, transmitter->ps_pin);
    SetPSPtol(transmitter->id, transmitter->ps_ptol);
    SetPSMoxDelay(transmitter->id, transmitter->ps_moxdelay);
    // Note that the TXDelay is internally stored in NanoSeconds
    SetPSTXDelay(transmitter->id, 1E-9 * transmitter->ps_ampdelay);
    SetPSLoopDelay(transmitter->id, transmitter->ps_loopdelay);
  }

  // update screen
  g_idle_add(ext_vfo_update, NULL);
}

void tx_set_twotone(TRANSMITTER *tx, int state) {
  static guint timer = 0;

  if (state == tx->twotone) { return; }

  tx->twotone = state;

  //
  // During a two-tone experiment, call a function periodically
  // (every 100 msec) that calibrates the TX attenuation value
  // if PureSignal is running with AutoCalibration. The timer will
  // automatically be removed
  //
  if (state) {
    // set frequencies and levels
    switch (get_tx_mode()) {
    case modeCWL:
    case modeLSB:
    case modeDIGL:
      SetTXAPostGenTTFreq(tx->id, -700.0, -1900.0);
      break;

    default:
      SetTXAPostGenTTFreq(tx->id, 700.0, 1900.0);
      break;
    }

    SetTXAPostGenTTMag (tx->id, 0.49999, 0.49999);
    SetTXAPostGenMode(tx->id, 1);
    SetTXAPostGenRun(tx->id, 1);

    if (timer == 0) {
      timer = g_timeout_add((guint) 100, ps_calibration_timer, &timer);
    }
  } else {
    SetTXAPostGenRun(tx->id, 0);

    //
    // These radios show "tails" of the TX signal after a TX/RX transition,
    // so wait after the TwoTone signal has been removed, before
    // removing MOX.
    // The wait time required is rather long, since we must fill the TX IQ
    // FIFO completely with zeroes. 100 msec was measured on a HermesLite-2
    // to be OK.
    //
    //
    if (device == DEVICE_HERMES_LITE2 || device == DEVICE_HERMES_LITE ||
        device == DEVICE_HERMES || device == DEVICE_STEMLAB || device == DEVICE_STEMLAB_Z20) {
      usleep(100000);
    }
  }

  g_idle_add(ext_mox_update, GINT_TO_POINTER(state));
}

void tx_set_ps_sample_rate(const TRANSMITTER *tx, int rate) {
  SetPSFeedbackRate (tx->id, rate);
}

///////////////////////////////////////////////////////////////////////////
// Sine tone generator based on phase words that are
// passed as an argument. The phase (value 0-256) is encoded in
// two integers (in the range 0-255) as
//
// phase = p1 + p2/256
//
// and the sine value is obtained from the table by linear
// interpolateion
//
// sine := sintab[p1] + p2*(sintab(p1+1)-sintab(p2))/256.0
//
// and the phase word is updated, depending on the frequency f, as
//
// p1 := p1 + (256*f)/48000
// p2 := p2 + (256*f)%48000
//
///////////////////////////////////////////////////////////////////////////
// The idea of this sine generator is
// - it does not depend on an external sin function
// - it does not do much floating point
// - many sine generators can run in parallel, with their "own"
//   phase words and frequencies
// - the phase is always continuous, even if there are frequency jumps
///////////////////////////////////////////////////////////////////////////

float sine_generator(int *phase1, int *phase2, int freq) {
  register float val, s, d;
  register int p1 = *phase1;
  register int p2 = *phase2;
  register int32_t f256 = freq * 256; // so we know 256*freq won't overflow
  s = sintab[p1];
  d = sintab[p1 + 1] - s;
  val = s + p2 * d * 0.00390625; // 1/256
  p1 += f256 / 48000;
  p2 += ((f256 % 48000) * 256) / 48000;

  // correct overflows in fractional and integer phase to keep
  // p1,p2 within bounds
  if (p2 > 255) {
    p2 -= 256;
    p1++;
  }

  if (p1 > 255) {
    p1 -= 256;
  }

  *phase1 = p1;
  *phase2 = p2;
  return val;
}

void tx_set_ramps(TRANSMITTER *tx) {
  //t_print("%s: new width=%d\n", __FUNCTION__, cw_ramp_width);
  //
  // Calculate a new CW ramp
  //
  g_mutex_lock(&tx->cw_ramp_mutex);

  //
  // For the side tone, a RaisedCosine profile with a width of 5 msec
  // seems to be standard.
  //
  if (tx->cw_ramp_audio) { g_free(tx->cw_ramp_audio); }

  tx->cw_ramp_audio_ptr = 0;
  tx->cw_ramp_audio_len = 240;
  tx->cw_ramp_audio = g_new(double, tx->cw_ramp_audio_len + 1);
  init_audio_ramp(tx->cw_ramp_audio, tx->cw_ramp_audio_len);

  //
  // For the RF pulse envelope, use a BlackmanHarris ramp with a
  // user-specified width
  //
  if (tx->cw_ramp_rf) { g_free(tx->cw_ramp_rf); }

  tx->cw_ramp_rf_ptr = 0;
  tx->cw_ramp_rf_len = 48 * tx->ratio * cw_ramp_width;
  tx->cw_ramp_rf = g_new(double, tx->cw_ramp_rf_len + 1);
  init_dl1ycf_ramp(tx->cw_ramp_rf, tx->cw_ramp_rf_len);
  g_mutex_unlock(&tx->cw_ramp_mutex);
}
