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

#include "alex.h"
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
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "audio.h"
#include "ext.h"
#include "sliders.h"
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

//
// In the old protocol, the CW signal is generated within pihpsdr,
// and the pulses must be shaped. This is done via "cw_shape_buffer".
// The TX mic samples buffer could possibly be used for this as well.
//
static double *cw_shape_buffer48 = NULL;
static double *cw_shape_buffer192 = NULL;
static int cw_shape = 0;
//
// cwramp is the function defining the "ramp" of the CW pulse.
// an array with RAMPLEN+1 entries. To change the ramp width,
// new arrays cwramp48[] and cwramp192[] have to be provided
// in cwramp.c
//
#define RAMPLEN 250         // 200: 4 msec ramp width, 250: 5 msec ramp width
extern double cwramp48[];       // see cwramp.c, for 48 kHz sample rate
extern double cwramp192[];      // see cwramp.c, for 192 kHz sample rate

double ctcss_frequencies[CTCSS_FREQUENCIES] = {
  67.0, 71.9, 74.4, 77.0, 79.7, 82.5, 85.4, 88.5, 91.5, 94.8,
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

static gint update_out_of_band(gpointer data) {
  TRANSMITTER *tx = (TRANSMITTER *)data;
  tx->out_of_band = 0;
  g_idle_add(ext_vfo_update, NULL);
  return G_SOURCE_REMOVE;
}

void transmitter_set_out_of_band(TRANSMITTER *tx) {
  tx->out_of_band = 1;
  g_idle_add(ext_vfo_update, NULL);
  tx->out_of_band_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 1000, update_out_of_band, tx, NULL);
}

void transmitter_set_deviation(TRANSMITTER *tx) {
  SetTXAFMDeviation(tx->id, (double)tx->deviation);
}

void transmitter_set_am_carrier_level(TRANSMITTER *tx) {
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
  tx->compressor = state;
  SetTXACompressorRun(tx->id, tx->compressor);
}

void reconfigure_transmitter(TRANSMITTER *tx, int width, int height) {
  if (width != tx->width || height != tx->height) {
    g_mutex_lock(&tx->display_mutex);
    t_print("reconfigure_transmitter: width=%d height=%d\n", width, height);
    tx->width = width;
    tx->height = height;
    gtk_widget_set_size_request(tx->panel, width, height);
    int ratio = tx->iq_output_rate / tx->mic_sample_rate;
    //
    // Upon calling, width either equals display_width (non-duplex) and
    // the *shown* TX spectrum is 24 kHz wide, or width equals 1/4 display_width (duplex)
    // and the *shown* TX spectrum is 6 kHz wide. In both cases, display_width pixels
    // correspond to 24 kHz, while the width of the whole spectrum is TXIQ.
    // The mic sample rate is fixed to 48k , so ratio is TXIQ/24k.
    // The value of tx->pixels corresponds to the *full* TX spectrum in the
    // target resolution.
    //
    tx->pixels = display_width * ratio * 2;
    g_free(tx->pixel_samples);
    tx->pixel_samples = g_new(float, tx->pixels);
    init_analyzer(tx);
    g_mutex_unlock(&tx->display_mutex);
  }

  gtk_widget_set_size_request(tx->panadapter, width, height);
}

void transmitterSaveState(const TRANSMITTER *tx) {
  char name[128];
  char value[128];
  t_print("%s: TX=%d\n", __FUNCTION__, tx->id);
  SetPropI1("transmitter.%d.low_latency",       tx->id,               tx->low_latency);
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
  SetPropI1("transmitter.%d.single_on",         tx->id,               tx->single_on);
  SetPropI1("transmitter.%d.feedback",          tx->id,               tx->feedback);
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
}

static void transmitterRestoreState(TRANSMITTER *tx) {
  char name[128];
  char *value;
  t_print("%s: id=%d\n", __FUNCTION__, tx->id);
  GetPropI1("transmitter.%d.low_latency",       tx->id,               tx->low_latency);
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
  GetPropI1("transmitter.%d.single_on",         tx->id,               tx->single_on);
  GetPropI1("transmitter.%d.feedback",          tx->id,               tx->feedback);
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
  static int pre_high_swr = 0;

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
    switch (device) {
    default:
      // This includes SOAPY (where these numbers are not used)
      constant1 = 3.3;
      constant2 = 0.09;
      rconstant2 = 0.09;
      rev_cal_offset = 3;
      fwd_cal_offset = 6;
      break;

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
      constant1 = 3.3;
      constant2 = 1.5;
      rconstant2 = 1.8;
      rev_cal_offset = 3;
      fwd_cal_offset = 6;
      break;
    }

    switch (protocol) {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
      fwd_power   = alex_forward_power;
      rev_power   = alex_reverse_power;

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

      break;

    case SOAPYSDR_PROTOCOL:
    default:
      tx->rev = 0.0;
      tx->fwd = 0.0;
      break;
    }

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
    if (tx->fwd > 0.25) {
      //
      // SWR means VSWR (voltage based) but we have the forward and
      // reflected power, so correct for that
      //

      double gamma = sqrt(tx->rev/tx->fwd);

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
  double keep_time = 0.1;
  int n_pixout = 1;
  int spur_elimination_ffts = 1;
  int data_type = 1;
  int afft_size = 8192;
  int window_type = 4;
  double kaiser_pi = 14.0;
  int overlap = 2048;
  int clip = 0;
  double span_clip_l = 0;
  double span_clip_h = 0;
  int pixels = tx->pixels;
  int stitches = 1;
  int calibration_data_set = 0;
  double span_min_freq = 0.0;
  double span_max_freq = 0.0;
  int max_w = afft_size + (int) min(keep_time * (double) tx->fps, keep_time * (double) afft_size * (double) tx->fps);
  overlap = (int)max(0.0, ceil(afft_size - (double)tx->mic_sample_rate / (double)tx->fps));
  t_print("SetAnalyzer id=%d buffer_size=%d overlap=%d pixels=%d\n", tx->id, tx->output_samples, overlap, tx->pixels);
  SetAnalyzer(tx->id,
              n_pixout,
              spur_elimination_ffts, // number of LO frequencies = number of ffts used in elimination
              data_type,             // 0 for real input data (I only); 1 for complex input data (I & Q)
              flp,                   // vector with one elt for each LO frequency, 1 if high-side LO, 0 otherwise
              afft_size,             // size of the fft, i.e., number of input samples
              tx->output_samples,    // number of samples transferred for each OpenBuffer()/CloseBuffer()
              window_type,           // integer specifying which window function to use
              kaiser_pi,             // PiAlpha parameter for Kaiser window
              overlap,               // number of samples each fft (other than the first) is to re-use from the previous
              clip,                  // number of fft output bins to be clipped from EACH side of each sub-span
              span_clip_l,           // number of bins to clip from low end of entire span
              span_clip_h,           // number of bins to clip from high end of entire span
              pixels,                // number of pixel values to return.  may be either <= or > number of bins
              stitches,              // number of sub-spans to concatenate to form a complete span
              calibration_data_set,  // identifier of which set of calibration data to use
              span_min_freq,         // frequency at first pixel value8192
              span_max_freq,         // frequency at last pixel value
              max_w                  //max samples to hold in input ring buffers
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
  tx->low_latency = 0;
  tx->fft_size = 2048;
  g_mutex_init(&tx->display_mutex);
  tx->update_timer_id = 0;
  tx->out_of_band_timer_id = 0;

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    tx->mic_sample_rate = 48000;   // sample rate of incoming audio signal
    tx->mic_dsp_rate = 48000;      // sample rate of TX signal processing within WDSP
    tx->iq_output_rate = 48000;    // output TX IQ sample rate
    break;

  case NEW_PROTOCOL:
    tx->mic_sample_rate = 48000;
    tx->mic_dsp_rate = 96000;
    tx->iq_output_rate = 192000;
    break;

  case SOAPYSDR_PROTOCOL:
    tx->mic_sample_rate = 48000;
    tx->mic_dsp_rate = 96000;
    tx->iq_output_rate = radio_sample_rate;
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

  int ratio = tx->iq_output_rate / tx->mic_sample_rate;
  tx->output_samples = tx->buffer_size * ratio;
  tx->pixels = display_width * ratio * 2;
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
  tx->feedback = 0;
  tx->auto_on = 0;
  tx->single_on = 0;
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
  transmitterRestoreState(tx);
  // allocate buffers
  t_print("transmitter: allocate buffers: mic_input_buffer=%d iq_output_buffer=%d pixels=%d\n", tx->buffer_size,
          tx->output_samples, tx->pixels);
  tx->mic_input_buffer = g_new(double, 2 * tx->buffer_size);
  tx->iq_output_buffer = g_new(double, 2 * tx->output_samples);
  tx->samples = 0;
  tx->pixel_samples = g_new(float, tx->pixels);

  if (cw_shape_buffer48) { g_free(cw_shape_buffer48); }

  if (cw_shape_buffer192) { g_free(cw_shape_buffer192); }

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    //
    // We need no buffer for the IQ sample amplitudes because
    // we make dual use of the buffer for the audio amplitudes
    // (TX sample rate ==  mic sample rate)
    //
    cw_shape_buffer48 = g_new(double, tx->buffer_size);
    break;

  case NEW_PROTOCOL:
  case SOAPYSDR_PROTOCOL:
    //
    // We need two buffers: one for the audio sample amplitudes
    // and another one for the TX IQ amplitudes
    // (TX and mic sample rate are usually different).
    //
    cw_shape_buffer48 = g_new(double, tx->buffer_size);
    cw_shape_buffer192 = g_new(double, tx->output_samples);
    break;
  }

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
  TXASetMP(tx->id, tx->low_latency);
  SetTXABandpassWindow(tx->id, 1);
  SetTXABandpassRun(tx->id, 1);
  SetTXAFMEmphPosition(tx->id, pre_emphasize);
  SetTXACFIRRun(tx->id, SET(protocol == NEW_PROTOCOL)); // turned on if new protocol

  //
  // enable_tx_equalizer and tx_equalizer should be part of TX
  //
  if (enable_tx_equalizer) {
    SetTXAGrphEQ(tx->id, tx_equalizer);
    SetTXAEQRun(tx->id, 1);
  } else {
    SetTXAEQRun(tx->id, 0);
  }

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

void tx_set_pre_emphasize(TRANSMITTER *tx, int state) {
  SetTXAFMEmphPosition(tx->id, state);
}

static void full_tx_buffer(TRANSMITTER *tx) {
  long isample;
  long qsample;
  double gain, sidevol, ramp;
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
    switch (protocol) {
    case ORIGINAL_PROTOCOL:
      for (j = 0; j < tx->output_samples; j++) {
        *dp++ = 0.0;
        *dp++ = cw_shape_buffer48[j];
      }

      break;

    case NEW_PROTOCOL:
    case SOAPYSDR_PROTOCOL:
      for (j = 0; j < tx->output_samples; j++) {
        *dp++ = 0.0;
        *dp++ = cw_shape_buffer192[j];
      }

      break;
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
      // so send some "silence" to prevent FIFO underflows
      //
      for (j = 0; j < 480; j++) {
        new_protocol_iq_samples(0, 0);
      }
    }

    txflag = 1;

    //
    //  When doing CW, we do not need WDSP since Q(t) = cw_shape_buffer(t) and I(t)=0
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
      case ORIGINAL_PROTOCOL:
        //
        // tx->output_samples equals tx->buffer_size
        // Take TX envelope from the 48kHz shape buffer
        //
        // An inspection of the IQ samples produced by WDSP when TUNEing shows
        // that the amplitude of the pulse is in I (in the range 0.0 - 1.0)
        // and Q should be zero
        //
        sidevol = 64.0 * cw_keyer_sidetone_volume; // between 0.0 and 8128.0

        for (j = 0; j < tx->output_samples; j++) {
          ramp = cw_shape_buffer48[j];              // between 0.0 and 1.0
          isample = floor(gain * ramp + 0.5);   // always non-negative, isample is just the pulse envelope
          sidetone = sidevol * ramp * sine_generator(&p1radio, &p2radio, cw_keyer_sidetone_frequency);
          old_protocol_iq_samples(isample, 0, sidetone);
        }

        break;

      case NEW_PROTOCOL:

        //
        // tx->output_samples is four times tx->buffer_size
        // Take TX envelope from the 192kHz shape buffer
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
          ramp = cw_shape_buffer192[j];                    // between 0.0 and 1.0
          isample = floor(0.896 * gain * ramp + 0.5);      // always non-negative, isample is just the pulse envelope
          new_protocol_iq_samples(isample, 0);
        }

        break;
#ifdef SOAPYSDR

      case SOAPYSDR_PROTOCOL:

        //
        // the only difference to the P2 treatment is that we do not
        // generate audio samples to be sent to the radio
        //
        for (j = 0; j < tx->output_samples; j++) {
          ramp = cw_shape_buffer192[j];             // between 0.0 and 1.0
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
      // and send 240 zero samples to make sure everything
      // will be sent out (some silence may remain but just
      // adds to the silence sent in the next RX->TX transition
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

  //
  // silence TX audio if tuning, or when doing CW.
  // (in order not to fire VOX)
  //

  if (tune || txmode == modeCWL || txmode == modeCWU) {
    mic_sample_double = 0.0;
  } else {
    mic_sample_double = (double)mic_sample;
  }

  //
  // shape CW pulses when doing CW and transmitting, else nullify them
  //
  if ((txmode == modeCWL || txmode == modeCWU) && isTransmitting()) {
    int updown;
    //
    //  RigCtl CW sets the variables cw_key_up and cw_key_down
    //  to the number of samples for the next down/up sequence.
    //  cw_key_down can be zero, for inserting some space
    //
    //  We HAVE TO shape the signal to avoid hard clicks to be
    //  heard way beside our frequency. The envelope (ramp function)
    //      is stored in cwramp48[0::RAMPLEN], so we "move" cw_shape between these
    //      values. The ramp width is RAMPLEN/48000 seconds.
    //
    //      In the new protocol, we use this ramp for the side tone, but
    //      must use values from cwramp192 for the TX iq signal.
    //
    //      Note that usually, the pulse is much broader than the ramp,
    //      that is, cw_key_down and cw_key_up are much larger than RAMPLEN.
    //
    cw_not_ready = 0;

    if (cw_key_down > 0 ) {
      if (cw_shape < RAMPLEN) { cw_shape++; }   // walk up the ramp

      cw_key_down--;            // decrement key-up counter
      updown = 1;
    } else {
      // dig into this even if cw_key_up is already zero, to ensure
      // that we reach the bottom of the ramp for very small pauses
      if (cw_shape > 0) { cw_shape--; } // walk down the ramp

      if (cw_key_up > 0) { cw_key_up--; } // decrement key-down counter

      updown = 0;
    }

    //
    // store the ramp value in cw_shape_buffer, but also use it for shaping the "local"
    // side tone
    double ramp = cwramp48[cw_shape];
    float cwsample = 0.00196 * cw_keyer_sidetone_volume * ramp * sine_generator(&p1local, &p2local,
                     cw_keyer_sidetone_frequency);

    //
    // cw_keyer_sidetone_volume is in the range 0...127 so cwsample is 0.00 ... 0.25
    //
    if (active_receiver->local_audio && cw_keyer_sidetone_volume > 0) { cw_audio_write(active_receiver, cwsample); }

    cw_shape_buffer48[tx->samples] = ramp;

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
          // Match found experimentally on my ANAN-7000 and *implies*
          // level 0...127 ==> amplitude 0...16300
          //
          s = (int) (cwsample * 65000.0);
        }
      }

      new_protocol_cw_audio_samples(s, s);
      s = 4 * cw_shape;
      i = 4 * tx->samples;

      // The 192kHz-ramp is constructed such that for cw_shape==0 or cw_shape==RAMPLEN,
      // the two following cases create the same shape.
      if (updown) {
        // climbing up...
        cw_shape_buffer192[i + 0] = cwramp192[s + 0];
        cw_shape_buffer192[i + 1] = cwramp192[s + 1];
        cw_shape_buffer192[i + 2] = cwramp192[s + 2];
        cw_shape_buffer192[i + 3] = cwramp192[s + 3];
      } else {
        // descending...
        cw_shape_buffer192[i + 0] = cwramp192[s + 3];
        cw_shape_buffer192[i + 1] = cwramp192[s + 2];
        cw_shape_buffer192[i + 2] = cwramp192[s + 1];
        cw_shape_buffer192[i + 3] = cwramp192[s + 0];
      }
    }

    if (protocol == SOAPYSDR_PROTOCOL) {
      //
      // The ratio between the TX and microphone sample rate can be any value, so
      // it is difficult to construct a general ramp here. We may at least *assume*
      // that the ratio is integral. We can extrapolate from the shapes calculated
      // for 48 and 192 kHz sample rate.
      //
      // At any rate, we *must* produce tx->outputsamples IQ samples from an input
      // buffer of size tx->buffer_size.
      //
      int ratio = tx->output_samples / tx->buffer_size;
      i = ratio * tx->samples; // current position in TX IQ buffer

      if (updown) {
        //
        // Climb up the ramp
        //
        if (ratio % 4 == 0) {
          // simple adaptation from the 192 kHz ramp
          ratio = ratio / 4;
          int s = 4 * cw_shape;

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 0]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 1]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 2]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 3]; }
        } else {
          // simple adaptation from the 48 kHz ramp
          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp48[cw_shape]; }
        }
      } else {
        //
        // Walk down the ramp
        //
        if (ratio % 4 == 0) {
          // simple adaptation from the 192 kHz ramp
          ratio = ratio / 4;
          int s = 4 * cw_shape;

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 3]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 2]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 1]; }

          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp192[s + 0]; }
        } else {
          // simple adaptation from the 48 kHz ramp
          for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = cwramp48[cw_shape]; }
        }
      }
    }
  } else {
    //
    //  If no longer transmitting, or no longer doing CW: reset pulse shaper.
    //  This will also swallow any pending CW in rigtl CAT CW and wipe out
    //      cw_shape_buffer very quickly. In order to tell rigctl etc. that CW should be
    //  aborted, we also use the cw_not_ready flag.
    //
    cw_not_ready = 1;
    cw_key_up = 0;

    if (cw_key_down > 0) { cw_key_down--; }  // in case it occured before the RX/TX transition

    cw_shape = 0;
    // insert "silence" in CW audio and TX IQ buffers
    cw_shape_buffer48[tx->samples] = 0.0;

    if (protocol == NEW_PROTOCOL) {
      cw_shape_buffer192[4 * tx->samples + 0] = 0.0;
      cw_shape_buffer192[4 * tx->samples + 1] = 0.0;
      cw_shape_buffer192[4 * tx->samples + 2] = 0.0;
      cw_shape_buffer192[4 * tx->samples + 3] = 0.0;
    }

    if (protocol == SOAPYSDR_PROTOCOL) {
      //
      // this essentially the P2 code, where the ratio
      // is fixed to 4
      //
      int ratio = tx->output_samples / tx->buffer_size;
      i = ratio * tx->samples;

      for (j = 0; j < ratio; j++) { cw_shape_buffer192[i++] = 0.0; }
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

void add_ps_iq_samples(TRANSMITTER *tx, double i_sample_tx, double q_sample_tx, double i_sample_rx,
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
    SetPSControl(tx->id, 0, 0, 1, 0);
  }

  // update screen
  g_idle_add(ext_vfo_update, NULL);
}

void tx_set_twotone(TRANSMITTER *tx, int state) {
  tx->twotone = state;

  if (state) {
    // set frequencies and levels
    switch (get_tx_mode()) {
    case modeCWL:
    case modeLSB:
    case modeDIGL:
      SetTXAPostGenTTFreq(tx->id, -900.0, -1700.0);
      break;

    default:
      SetTXAPostGenTTFreq(tx->id, 900.0, 1700.0);
      break;
    }

    SetTXAPostGenTTMag (tx->id, 0.49999, 0.49999);
    SetTXAPostGenMode(tx->id, 1);
    SetTXAPostGenRun(tx->id, 1);
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

void tx_set_ps_sample_rate(TRANSMITTER *tx, int rate) {
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
