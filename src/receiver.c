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

#include <gtk/gtk.h>
#include <math.h>

#include <wdsp.h>

#include "agc.h"
#include "audio.h"
#include "band.h"
#include "bandstack.h"
#include "channel.h"
#include "client_server.h"
#include "discovered.h"
#include "ext.h"
#include "filter.h"
#include "main.h"
#include "meter.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "rx_panadapter.h"
#include "sliders.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "transmitter.h"
#include "vfo.h"
#include "waterfall.h"

#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

static int last_x;
static gboolean has_moved = FALSE;
static gboolean pressed = FALSE;
static gboolean making_active = FALSE;

//
// PART 1. Functions releated to the receiver display
//

// cppcheck-suppress constParameterCallback
static void rx_weak_notify(gpointer data, GObject  *obj) {
  const RECEIVER *rx = (RECEIVER *)data;
  t_print("%s: id=%d obj=%p\n", __FUNCTION__, rx->id, obj);
}

// cppcheck-suppress constParameterPointer
gboolean rx_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  const RECEIVER *rx = (RECEIVER *)data;

  if (rx == active_receiver) {
    if (event->button == GDK_BUTTON_PRIMARY) {
      last_x = (int)(event->x + 0.5);
      has_moved = FALSE;
      pressed = TRUE;
    } else if (event->button == GDK_BUTTON_SECONDARY) {
      g_idle_add(ext_start_rx, NULL);
    }
  } else {
    making_active = TRUE;
  }

  return TRUE;
}

void rx_set_active(RECEIVER *rx) {
  if (radio_is_remote) {
    send_rx_select(client_socket, rx->id);
  }

  //
  // Abort any frequency entering in the current receiver
  //
  vfo_num_pad(-1, active_receiver->id);
  //
  // Make rx the new active receiver
  //
  active_receiver = rx;
  g_idle_add(menu_active_receiver_changed, NULL);
  g_idle_add(ext_vfo_update, NULL);
  g_idle_add(sliders_active_receiver_changed, NULL);

  //
  // Changing the active receiver flips the TX vfo
  // and possibly the TX band
  //
  if (!radio_is_remote) {
    radio_tx_vfo_changed();
    radio_apply_band_settings(0, 0);
  }
}

// cppcheck-suppress constParameterPointer
gboolean rx_button_release_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;

  if (making_active) {
    making_active = FALSE;
    rx_set_active(rx);

    if (event->button == GDK_BUTTON_SECONDARY) {
      g_idle_add(ext_start_rx, NULL);
    }
  } else {
    if (pressed) {
      int x = (int)(event->x + 0.5);

      if (event->button == GDK_BUTTON_PRIMARY) {
        int id = active_receiver->id;

        if (has_moved) {
          // drag
          vfo_id_move(id, (long long)((x - last_x)*rx->cB), vfo_snap);
        } else {
          //
          // Calculate target frequency and move to that one
          // Add 0.5 to pixel number, so on a 800 pix screen the pix numbers
          // 0...799 are converted to 0.5, 1.5, ... 799.5
          //
          long long f = (long long) (rx->cA + rx->cB * ((double) x + 0.5));
          //
          // Add center frequency
          //
          f += vfo[id].frequency;
          vfo_id_move_to(id, f, vfo_snap);
        }

        last_x = x;
        pressed = FALSE;
      }
    }
  }

  return TRUE;
}

gboolean rx_motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  int x, y;
  GdkModifierType state;
  const RECEIVER *rx = (RECEIVER *)data;
  //
  // This solves a problem observed since with GTK about mid-2023:
  // when re-focusing a (sub-)menu window after it has lost focus,
  // it may happen that the button press event for "re-focusing"
  // the menu window also ends up in the receiver panel, and that
  // the subsequent button release event is not forwarded to the
  // receiver panal.
  // Subsequent mouse moves (without a button pressed) then led to
  // wild VFO frequency changes. The first temporary solution was to
  // ignore all button press events if a (sub-)menu window is open, but
  // this reduces piHPSDR functionality. Now we solve this problem by
  // looking HERE if the mouse button is pressed, and if not, ignore the
  // move.
  //
  int button_down = (event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK)) != 0;

  //
  // if !pressed, we may come from the destruction
  // of a menu, and should not move the VFO.
  //
  if (!making_active && pressed && button_down) {
    gdk_window_get_device_position (event->window,
                                    event->device,
                                    &x,
                                    &y,
                                    &state);
    //
    // Sometimes it turned out to be difficult to "jump" to a
    // new frequency by just clicking in the panadaper. Futher analysis
    // showed that there were "moves" with zero offset arriving between
    // pressing and releasing the mouse button.
    // Accepting such a "move" between a  "press" and the next "release" event
    // sets "has_moved" and results in a "VFO drag" instead of a "VFO set".
    //
    // So we do the following:
    // - "moves" with zero offset are always ignored
    // - the first "move" to be accepted after a "press" must lead us
    //   at least 2 pixels away from the original position.
    //
    int moved = x - last_x;

    if (moved != 0) {
      if (has_moved || moved < -1 || moved > 1) {
        int id = active_receiver->id;
        vfo_id_move(id, (long long)((float)moved * rx->cB), FALSE);
        last_x = x;
        has_moved = TRUE;
      }
    }
  }

  return TRUE;
}

// cppcheck-suppress constParameterPointer
gboolean rx_scroll_event(GtkWidget *widget, const GdkEventScroll *event, gpointer data) {
  int step = (event->state & GDK_SHIFT_MASK) ? 10 : 1;

  //
  // On one of my Macs, the shift key modifies scroll up/down to scroll left/right,
  // therefore treat BOTH down and right as "right"
  //
  if (event->direction == GDK_SCROLL_DOWN || event->direction == GDK_SCROLL_RIGHT) { step = -step; }

  vfo_step(step);
  return TRUE;
}

void rx_save_state(const RECEIVER *rx) {
  //
  // For a PS_RX_FEEDBACK, we only store/restore the ADC
  // This is currently hard-wired to ADC0, but can be changed
  // by manually editing the props file.
  //
  SetPropI1("receiver.%d.adc", rx->id,                        rx->adc);

  if (rx->id == PS_RX_FEEDBACK) { return; }

  //
  // Now save everything that  is local to the client
  //
  SetPropI1("receiver.%d.audio_channel", rx->id,                rx->audio_channel);
  SetPropI1("receiver.%d.local_audio", rx->id,                  rx->local_audio);
  SetPropS1("receiver.%d.audio_name", rx->id,                   rx->audio_name);
  SetPropI1("receiver.%d.audio_device", rx->id,                 rx->audio_device);
  SetPropI1("receiver.%d.mute_when_not_active", rx->id,         rx->mute_when_not_active);
  SetPropI1("receiver.%d.mute_radio", rx->id,                   rx->mute_radio);
  SetPropI1("receiver.%d.panadapter_low", rx->id,               rx->panadapter_low);
  SetPropI1("receiver.%d.panadapter_high", rx->id,              rx->panadapter_high);
  SetPropI1("receiver.%d.panadapter_step", rx->id,              rx->panadapter_step);
  SetPropI1("receiver.%d.panadapter_peaks_on", rx->id,          rx->panadapter_peaks_on);
  SetPropI1("receiver.%d.panadapter_num_peaks", rx->id,         rx->panadapter_num_peaks);
  SetPropI1("receiver.%d.panadapter_ignore_range_divider", rx->id, rx->panadapter_ignore_range_divider);
  SetPropI1("receiver.%d.panadapter_ignore_noise_percentile", rx->id, rx->panadapter_ignore_noise_percentile);
  SetPropI1("receiver.%d.panadapter_hide_noise_filled", rx->id, rx->panadapter_hide_noise_filled);
  SetPropI1("receiver.%d.panadapter_peaks_in_passband_filled", rx->id, rx->panadapter_peaks_in_passband_filled);
  SetPropI1("receiver.%d.display_waterfall", rx->id,            rx->display_waterfall);
  SetPropI1("receiver.%d.display_panadapter", rx->id,           rx->display_panadapter);
  SetPropI1("receiver.%d.display_filled", rx->id,               rx->display_filled);
  SetPropI1("receiver.%d.display_gradient", rx->id,             rx->display_gradient);
  SetPropI1("receiver.%d.waterfall_low", rx->id,                rx->waterfall_low);
  SetPropI1("receiver.%d.waterfall_high", rx->id,               rx->waterfall_high);
  SetPropI1("receiver.%d.waterfall_automatic", rx->id,          rx->waterfall_automatic);
  SetPropI1("receiver.%d.waterfall_percent", rx->id,            rx->waterfall_percent);

  if (!radio_is_remote) {
    SetPropI1("receiver.%d.smetermode", rx->id,                 rx->smetermode);
    SetPropI1("receiver.%d.low_latency", rx->id,                rx->low_latency);
    SetPropI1("receiver.%d.fft_size", rx->id,                   rx->fft_size);
    SetPropI1("receiver.%d.sample_rate", rx->id,                rx->sample_rate);
    SetPropI1("receiver.%d.filter_low", rx->id,                 rx->filter_low);
    SetPropI1("receiver.%d.filter_high", rx->id,                rx->filter_high);
    SetPropI1("receiver.%d.fps", rx->id,                        rx->fps);
    SetPropI1("receiver.%d.display_detector_mode", rx->id,      rx->display_detector_mode);
    SetPropI1("receiver.%d.display_average_mode", rx->id,       rx->display_average_mode);
    SetPropF1("receiver.%d.display_average_time", rx->id,       rx->display_average_time);
    SetPropF1("receiver.%d.volume", rx->id,                     rx->volume);
    SetPropI1("receiver.%d.agc", rx->id,                        rx->agc);
    SetPropF1("receiver.%d.agc_gain", rx->id,                   rx->agc_gain);
    SetPropF1("receiver.%d.agc_slope", rx->id,                  rx->agc_slope);
    SetPropF1("receiver.%d.agc_hang_threshold", rx->id,         rx->agc_hang_threshold);
    SetPropI1("receiver.%d.nb", rx->id,                         rx->nb);
    SetPropI1("receiver.%d.nr", rx->id,                         rx->nr);
    SetPropI1("receiver.%d.anf", rx->id,                        rx->anf);
    SetPropI1("receiver.%d.snb", rx->id,                        rx->snb);
    SetPropI1("receiver.%d.nr_agc", rx->id,                     rx->nr_agc);
    SetPropI1("receiver.%d.nr2_gain_method", rx->id,            rx->nr2_gain_method);
    SetPropI1("receiver.%d.nr2_npe_method", rx->id,             rx->nr2_npe_method);
    SetPropI1("receiver.%d.nr2_ae", rx->id,                     rx->nr2_ae);
    SetPropF1("receiver.%d.nr2_trained_threshold", rx->id,      rx->nr2_trained_threshold);
    SetPropF1("receiver.%d.nr2_trained_t2", rx->id,             rx->nr2_trained_t2);
    SetPropI1("receiver.%d.nb2_mode", rx->id,                   rx->nb2_mode);
    SetPropF1("receiver.%d.nb_tau", rx->id,                     rx->nb_tau);
    SetPropF1("receiver.%d.nb_advtime", rx->id,                 rx->nb_advtime);
    SetPropF1("receiver.%d.nb_hang", rx->id,                    rx->nb_hang);
    SetPropF1("receiver.%d.nb_thresh", rx->id,                  rx->nb_thresh);
#ifdef EXTNR
    SetPropF1("receiver.%d.nr4_reduction_amount", rx->id,       rx->nr4_reduction_amount);
    SetPropF1("receiver.%d.nr4_smoothing_factor", rx->id,       rx->nr4_smoothing_factor);
    SetPropF1("receiver.%d.nr4_whitening_factor", rx->id,       rx->nr4_whitening_factor);
    SetPropF1("receiver.%d.nr4_noise_rescale", rx->id,          rx->nr4_noise_rescale);
    SetPropF1("receiver.%d.nr4_post_threshold", rx->id,         rx->nr4_post_threshold);
#endif
    SetPropI1("receiver.%d.deviation", rx->id,                  rx->deviation);
    SetPropI1("receiver.%d.squelch_enable", rx->id,             rx->squelch_enable);
    SetPropF1("receiver.%d.squelch", rx->id,                    rx->squelch);
    SetPropI1("receiver.%d.binaural", rx->id,                   rx->binaural);
    SetPropI1("receiver.%d.zoom", rx->id,                       rx->zoom);
    SetPropI1("receiver.%d.pan", rx->id,                        rx->pan);
    SetPropI1("receiver.%d.eq_enable", rx->id,                  rx->eq_enable);

    for (int i = 0; i < 11; i++) {
      SetPropF2("receiver.%d.eq_freq[%d]", rx->id, i,           rx->eq_freq[i]);
      SetPropF2("receiver.%d.eq_gain[%d]", rx->id, i,           rx->eq_gain[i]);
    }
  }
}

void rx_restore_state(RECEIVER *rx) {
  //
  // For a PS_RX_FEEDBACK, we only store/restore the ADC
  // This is currently hard-wired to ADC0, but can be changed
  // by manually editing the props file.
  //
  GetPropI1("receiver.%d.adc", rx->id,                        rx->adc);

  if (rx->id == PS_RX_FEEDBACK) { return; }

  //
  // First restore data that is "local" to the client
  //
  GetPropI1("receiver.%d.audio_channel", rx->id,                rx->audio_channel);
  GetPropI1("receiver.%d.local_audio", rx->id,                  rx->local_audio);
  GetPropS1("receiver.%d.audio_name", rx->id,                   rx->audio_name);
  GetPropI1("receiver.%d.audio_device", rx->id,                 rx->audio_device);
  GetPropI1("receiver.%d.mute_when_not_active", rx->id,         rx->mute_when_not_active);
  GetPropI1("receiver.%d.mute_radio", rx->id,                   rx->mute_radio);
  GetPropI1("receiver.%d.panadapter_low", rx->id,               rx->panadapter_low);
  GetPropI1("receiver.%d.panadapter_high", rx->id,              rx->panadapter_high);
  GetPropI1("receiver.%d.panadapter_step", rx->id,              rx->panadapter_step);
  GetPropI1("receiver.%d.panadapter_peaks_on", rx->id,          rx->panadapter_peaks_on);
  GetPropI1("receiver.%d.panadapter_num_peaks", rx->id,         rx->panadapter_num_peaks);
  GetPropI1("receiver.%d.panadapter_ignore_range_divider", rx->id, rx->panadapter_ignore_range_divider);
  GetPropI1("receiver.%d.panadapter_ignore_noise_percentile", rx->id, rx->panadapter_ignore_noise_percentile);
  GetPropI1("receiver.%d.panadapter_hide_noise_filled", rx->id, rx->panadapter_hide_noise_filled);
  GetPropI1("receiver.%d.panadapter_peaks_in_passband_filled", rx->id, rx->panadapter_peaks_in_passband_filled);
  GetPropI1("receiver.%d.display_waterfall", rx->id,            rx->display_waterfall);
  GetPropI1("receiver.%d.display_panadapter", rx->id,           rx->display_panadapter);
  GetPropI1("receiver.%d.display_filled", rx->id,               rx->display_filled);
  GetPropI1("receiver.%d.display_gradient", rx->id,             rx->display_gradient);
  GetPropI1("receiver.%d.waterfall_low", rx->id,                rx->waterfall_low);
  GetPropI1("receiver.%d.waterfall_high", rx->id,               rx->waterfall_high);
  GetPropI1("receiver.%d.waterfall_automatic", rx->id,          rx->waterfall_automatic);
  GetPropI1("receiver.%d.waterfall_percent", rx->id,            rx->waterfall_percent);

  if (!radio_is_remote) {
    GetPropI1("receiver.%d.smetermode", rx->id,                 rx->smetermode);
    GetPropI1("receiver.%d.low_latency", rx->id,                rx->low_latency);
    GetPropI1("receiver.%d.fft_size", rx->id,                   rx->fft_size);
    GetPropI1("receiver.%d.sample_rate", rx->id,                rx->sample_rate);

    //
    // This may happen if the firmware was down-graded from P2 to P1
    //
    if (protocol == ORIGINAL_PROTOCOL && rx->sample_rate > 384000) {
      rx->sample_rate = 384000;
    }

    GetPropI1("receiver.%d.filter_low", rx->id,                 rx->filter_low);
    GetPropI1("receiver.%d.filter_high", rx->id,                rx->filter_high);
    GetPropI1("receiver.%d.fps", rx->id,                        rx->fps);
    GetPropI1("receiver.%d.display_detector_mode", rx->id,      rx->display_detector_mode);
    GetPropI1("receiver.%d.display_average_mode", rx->id,       rx->display_average_mode);
    GetPropF1("receiver.%d.display_average_time", rx->id,       rx->display_average_time);
    GetPropF1("receiver.%d.volume", rx->id,                     rx->volume);
    GetPropI1("receiver.%d.agc", rx->id,                        rx->agc);
    GetPropF1("receiver.%d.agc_gain", rx->id,                   rx->agc_gain);
    GetPropF1("receiver.%d.agc_slope", rx->id,                  rx->agc_slope);
    GetPropF1("receiver.%d.agc_hang_threshold", rx->id,         rx->agc_hang_threshold);
    GetPropI1("receiver.%d.nb", rx->id,                         rx->nb);
    GetPropI1("receiver.%d.nr", rx->id,                         rx->nr);
    GetPropI1("receiver.%d.anf", rx->id,                        rx->anf);
    GetPropI1("receiver.%d.snb", rx->id,                        rx->snb);
    GetPropI1("receiver.%d.nr_agc", rx->id,                     rx->nr_agc);
    GetPropI1("receiver.%d.nr2_gain_method", rx->id,            rx->nr2_gain_method);
    GetPropI1("receiver.%d.nr2_npe_method", rx->id,             rx->nr2_npe_method);
    GetPropI1("receiver.%d.nr2_ae", rx->id,                     rx->nr2_ae);
    GetPropF1("receiver.%d.nr2_trained_threshold", rx->id,      rx->nr2_trained_threshold);
    GetPropF1("receiver.%d.nr2_trained_t2", rx->id,             rx->nr2_trained_t2);
    GetPropI1("receiver.%d.nb2_mode", rx->id,                   rx->nb2_mode);
    GetPropF1("receiver.%d.nb_tau", rx->id,                     rx->nb_tau);
    GetPropF1("receiver.%d.nb_advtime", rx->id,                 rx->nb_advtime);
    GetPropF1("receiver.%d.nb_hang", rx->id,                    rx->nb_hang);
    GetPropF1("receiver.%d.nb_thresh", rx->id,                  rx->nb_thresh);
#ifdef EXTNR
    GetPropF1("receiver.%d.nr4_reduction_amount", rx->id,       rx->nr4_reduction_amount);
    GetPropF1("receiver.%d.nr4_smoothing_factor", rx->id,       rx->nr4_smoothing_factor);
    GetPropF1("receiver.%d.nr4_whitening_factor", rx->id,       rx->nr4_whitening_factor);
    GetPropF1("receiver.%d.nr4_noise_rescale", rx->id,          rx->nr4_noise_rescale);
    GetPropF1("receiver.%d.nr4_post_threshold", rx->id,         rx->nr4_post_threshold);
#endif
    GetPropI1("receiver.%d.deviation", rx->id,                  rx->deviation);
    GetPropI1("receiver.%d.squelch_enable", rx->id,             rx->squelch_enable);
    GetPropF1("receiver.%d.squelch", rx->id,                    rx->squelch);
    GetPropI1("receiver.%d.binaural", rx->id,                   rx->binaural);
    GetPropI1("receiver.%d.zoom", rx->id,                       rx->zoom);
    GetPropI1("receiver.%d.pan", rx->id,                        rx->pan);
    GetPropI1("receiver.%d.eq_enable", rx->id,                  rx->eq_enable);

    for (int i = 0; i < 11; i++) {
      GetPropF2("receiver.%d.eq_freq[%d]", rx->id, i,            rx->eq_freq[i]);
      GetPropF2("receiver.%d.eq_gain[%d]", rx->id, i,            rx->eq_gain[i]);
    }
  }

  // Sanity Checks
  if (n_adc == 1) { rx->adc = 0; }
}

void rx_reconfigure(RECEIVER *rx, int height) {
  int y = 0;
  //
  // Calculate the height of the panadapter (pheight) and the waterfall (wheight)
  // depending on whether only one or both are shown, and depending on the relative
  // waterfall height
  // CALL THIS ONLY with rx->display_mutex locked.
  //
  int pheight = height;
  int wheight = height;
  rx->height = height; // total height
  gtk_widget_set_size_request(rx->panel, rx->width, rx->height);
  t_print("%s: rx=%d width=%d height=%d\n", __FUNCTION__, rx->id, rx->width, rx->height);

  if (rx->display_panadapter && rx->display_waterfall) {
    wheight = (rx->waterfall_percent * height) / 100;
    pheight = height - wheight;
  }

  if (rx->display_panadapter) {
    if (rx->panadapter == NULL) {
      t_print("%s: panadapter_init: width:%d height:%d\n", __FUNCTION__, rx->width, pheight);
      rx_panadapter_init(rx, rx->width, pheight);
      gtk_fixed_put(GTK_FIXED(rx->panel), rx->panadapter, 0, y); // y=0 here always
    } else {
      // set the size
      gtk_widget_set_size_request(rx->panadapter, rx->width, pheight);
      // move the current one
      gtk_fixed_move(GTK_FIXED(rx->panel), rx->panadapter, 0, y);
    }

    y += pheight;
  } else {
    if (rx->panadapter != NULL) {
      gtk_container_remove(GTK_CONTAINER(rx->panel), rx->panadapter);
      rx->panadapter = NULL;
    }
  }

  if (rx->display_waterfall) {
    if (rx->waterfall == NULL) {
      t_print("%s: waterfall_init: width:%d height:%d\n", __FUNCTION__, rx->width, wheight);
      waterfall_init(rx, rx->width, wheight);
      gtk_fixed_put(GTK_FIXED(rx->panel), rx->waterfall, 0, y); // y=0 if ONLY waterfall is present
    } else {
      // set the size
      t_print("%s: waterfall set_size_request: width:%d height:%d\n", __FUNCTION__, rx->width, wheight);
      gtk_widget_set_size_request(rx->waterfall, rx->width, wheight);
      // move the current one
      gtk_fixed_move(GTK_FIXED(rx->panel), rx->waterfall, 0, y);
    }
  } else {
    if (rx->waterfall != NULL) {
      gtk_container_remove(GTK_CONTAINER(rx->panel), rx->waterfall);
      rx->waterfall = NULL;
    }
  }

  gtk_widget_show_all(rx->panel);
}

static int rx_update_display(gpointer data) {
  ASSERT_SERVER(0);
  RECEIVER *rx = (RECEIVER *)data;

  if (rx->displaying && rx->pixels > 0) {
    if (active_receiver == rx) {
      //
      // since rx->meter is used in other places as well (e.g. rigctl),
      // the value obtained from WDSP is best corrected HERE for
      // possible gain and attenuation
      //
      int id = rx->id;
      int b  = vfo[id].band;
      const BAND *band = band_get_band(b);
      int calib = rx_gain_calibration - band->gaincalib;
      double level = rx_get_smeter(rx);
      level += (double)calib + (double)adc[rx->adc].attenuation - adc[rx->adc].gain;

      if (filter_board == ALEX && rx->adc == 0) {
        level += (double)(10 * adc[0].alex_attenuation);
      }

      if (filter_board == CHARLY25 && rx->adc == 0) {
        level += (double)(12 * adc[0].alex_attenuation - 18 * (adc[0].preamp + adc[0].dither));
      }

      if (have_preamp && filter_board != CHARLY25) {
        level -= (double)(20 * adc[rx->adc].preamp);
      }

      rx->meter = level;
      meter_update(rx, SMETER, rx->meter, 0.0, 0.0);
    }

    g_mutex_lock(&rx->display_mutex);
    rx_get_pixels(rx);

    if (rx->pixels_available || rx->analyzer_initializing) {
      rx->analyzer_initializing = 0;

      if (remoteclient.running) {
        send_rxspectrum(rx->id);
      }

      if (rx->display_panadapter) {
        rx_panadapter_update(rx);
      }

      if (rx->display_waterfall) {
        waterfall_update(rx);
      }
    }

    g_mutex_unlock(&rx->display_mutex);
    return TRUE;
  }

  return FALSE;
}

void rx_set_displaying(RECEIVER *rx) {
  ASSERT_SERVER();

  if (rx->displaying) {
    if (rx->update_timer_id > 0) {
      g_source_remove(rx->update_timer_id);
    }

    rx->update_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 1000 / rx->fps, rx_update_display, rx, NULL);
  } else {
    if (rx->update_timer_id > 0) {
      g_source_remove(rx->update_timer_id);
      rx->update_timer_id = 0;
    }
  }
}

static void rx_create_visual(RECEIVER *rx) {
  int y = 0;
  rx->panel = gtk_fixed_new();
  t_print("%s: RXid=%d width=%d height=%d %p\n", __FUNCTION__, rx->id, rx->width, rx->height, rx->panel);
  g_object_weak_ref(G_OBJECT(rx->panel), rx_weak_notify, (gpointer)rx);
  gtk_widget_set_size_request (rx->panel, rx->width, rx->height);
  rx->panadapter = NULL;
  rx->waterfall = NULL;
  int height = rx->height;

  if (rx->display_waterfall) {
    height = height / 2;
  }

  rx_panadapter_init(rx, rx->width, height);
  t_print("%s: panadapter height=%d y=%d %p\n", __FUNCTION__, height, y, rx->panadapter);
  g_object_weak_ref(G_OBJECT(rx->panadapter), rx_weak_notify, (gpointer)rx);
  gtk_fixed_put(GTK_FIXED(rx->panel), rx->panadapter, 0, y);
  y += height;

  if (rx->display_waterfall) {
    waterfall_init(rx, rx->width, height);
    t_print("%s: waterfall height=%d y=%d %p\n", __FUNCTION__, height, y, rx->waterfall);
    g_object_weak_ref(G_OBJECT(rx->waterfall), rx_weak_notify, (gpointer)rx);
    gtk_fixed_put(GTK_FIXED(rx->panel), rx->waterfall, 0, y);
  }

  gtk_widget_show_all(rx->panel);
}

RECEIVER *rx_create_pure_signal_receiver(int id, int sample_rate, int width, int fps) {
  ASSERT_SERVER(NULL);
  //
  // For a PureSignal receiver, most parameters are not needed
  // so we fill the entire data with zeroes
  //
  RECEIVER *rx = malloc(sizeof(RECEIVER));

  if (!rx) {
    fatal_error("FATAL: cannot allocate PS-rx");
    return NULL;
  }

  memset (rx, 0, sizeof(RECEIVER));
  //
  // Setting the non-zero parameters.
  // a PS_TX_FEEDBACK receiver only needs and id and the iq_input_buffer (nothing else).
  // a PS_RX_FEEDBACK receiver needs an analyzer (for the MON button), and
  // it needs an alex_rx_antenna setting.
  //
  rx->id = id;
  rx->buffer_size = 1024;
  rx->iq_input_buffer = g_new(double, 2 * rx->buffer_size);

  if (id == PS_RX_FEEDBACK) {
    //
    // The analyzer is only used if
    // displaying the RX feedback samples (MON button in PS menu).
    //
    rx->adc = 0;
    rx_restore_state(rx);  // this may change the adc
    g_mutex_init(&rx->mutex);
    g_mutex_init(&rx->display_mutex);
    rx->sample_rate = sample_rate;
    rx->fps = fps;
    rx->width = width;
    rx->pixels = duplex ? 4 * tx_dialog_width : width;
    rx->pixel_samples = g_new(float, rx->pixels);
    //
    // These values (including fps) should match those of the TX display
    //
    rx->display_detector_mode = DET_PEAK;
    rx->display_average_time  = 120.0;
    rx->display_average_mode  = AVG_LOGRECURSIVE;
    rx_create_analyzer(rx);
    rx_set_detector(rx);
    rx_set_average(rx);
  }

  return rx;
}

int rx_remote_update_display(gpointer data) {
  RECEIVER *rx = (RECEIVER *) data;

  if (rx->displaying && rx->pixels > 0) {
    g_mutex_lock(&rx->display_mutex);

    if (rx->display_panadapter) {
      rx_panadapter_update(rx);
    }

    if (rx->display_waterfall) {
      waterfall_update(rx);
    }

    g_mutex_unlock(&rx->display_mutex);
  }

  if (active_receiver == rx) {
    meter_update(rx, SMETER, rx->meter, 0.0, 0.0);
  }

  return G_SOURCE_REMOVE;
}

void rx_create_remote(RECEIVER *rx) {
  //
  // receiver structure already setup via INFO_RECEIVER packet.
  // since everything is done on the "local" side, we only need
  // to set-up the panadapter
  //
  rx_create_visual(rx);
}

RECEIVER *rx_create_receiver(int id, int width, int height) {
  ASSERT_SERVER(NULL);
  t_print("%s: RXid=%d width=%d height=%d\n", __FUNCTION__, id, width, height);
  RECEIVER *rx = malloc(sizeof(RECEIVER));

  if (!rx) {
    fatal_error("FATAL: cannot allocate rx");
    return NULL;
  }

  //
  // This is to guard against programming errors
  // (missing initializations)
  //
  memset(rx, 0, sizeof(RECEIVER));
  //
  rx->id = id;
  g_mutex_init(&rx->mutex);
  g_mutex_init(&rx->display_mutex);

  switch (id) {
  case 0:
    rx->adc = 0;
    break;

  default:
    switch (device) {
    case DEVICE_METIS:
    case DEVICE_OZY:
    case DEVICE_HERMES:
    case DEVICE_HERMES_LITE:
    case DEVICE_HERMES_LITE2:
    case NEW_DEVICE_ATLAS:
    case NEW_DEVICE_HERMES:
      //
      // Assume a single adc for these devices.
      // If multiple Mecury cards are detected in old_protocol.c,
      // RX2 becomes associated with ADC2
      //
      rx->adc = 0;
      break;

    default:
      rx->adc = 1;
      break;
    }
  }

  rx->sample_rate = 48000;
  rx->resampler = NULL;
  rx->resample_input = NULL;
  rx->resample_output = NULL;

  if (device == SOAPYSDR_USB_DEVICE) {
    rx->sample_rate = radio->soapy.sample_rate;
    t_print("%s: RXid=%d sample_rate=%d\n", __FUNCTION__, rx->id, rx->sample_rate);
  }

  //
  // For larger sample rates we could use a larger buffer_size, since then
  // the number of audio samples per batch is rather small. However, the buffer
  // size is not changed when the sample RX rate is changed, so we use an
  // "average" value here.
  //
  rx->buffer_size = 1024;
  rx->dsp_size = 2048;
  rx->fft_size = 2048;
  rx->low_latency = 0;
  rx->smetermode = SMETER_AVERAGE;
  rx->fps = 10;
  rx->update_timer_id = 0;
  rx->width = width;
  rx->afft_size = 16384;
  rx->height = height;
  rx->samples = 0;
  rx->displaying = 0;
  rx->display_panadapter = 1;
  rx->display_waterfall = 1;
  rx->panadapter_high = -40;
  rx->panadapter_low = -140;
  rx->panadapter_step = 20;
  rx->panadapter_peaks_on = 0;
  rx->panadapter_num_peaks = 3;
  rx->panadapter_ignore_range_divider = 20;
  rx->panadapter_ignore_noise_percentile = 80;
  rx->panadapter_hide_noise_filled = 1;
  rx->panadapter_peaks_in_passband_filled = 0;
  rx->waterfall_high = -40;
  rx->waterfall_low = -140;
  rx->waterfall_automatic = 1;
  rx->waterfall_percent = 25;
  rx->display_filled = 1;
  rx->display_gradient = 1;
  rx->display_detector_mode = DET_AVERAGE;
  rx->display_average_mode = AVG_LOGRECURSIVE;
  rx->display_average_time = 120.0;
  rx->volume = -20.0;
  rx->nb = 0;
  rx->nr = 0;
  rx->anf = 0;
  rx->snb = 0;
  rx->nr_agc = 0;                   // NR/NR2/ANF before AGC
  rx->nr2_gain_method = 2;          // Gamma
  rx->nr2_npe_method = 0;           // OSMS
  rx->nr2_ae = 1;                   // Artifact Elimination is "on"
  rx->nr2_trained_threshold = -0.5; // Threshold if gain method is "Trained"
  rx->nr2_trained_t2 = 0.2;         // t2 value for trained threshold
  //
  // It has been reported that the piHPSDR noise blankers do not function
  // satisfactorily. I could reproduce this after building an "impulse noise source"
  // into the HPSDR simulator, and also confirmed that a popular Windows SDR program
  // has much better NB/NB2 performance.
  //
  // Digging into it, I found these SDR programs used NB default parameters *very*
  // different from those recommended in the WDSP manual: slewtime, hangtime and advtime
  // default to 0.01 msec, and the threshold to 30 (which is internally multiplied with 0.165
  // to obtain the WDSP threshold parameter).
  //
  rx->nb_tau =     0.00001;       // Slew=0.01    in the DSP menu
  rx->nb_advtime = 0.00001;       // Lead=0.01    in the DSP menu
  rx->nb_hang =    0.00001;       // Lag=0.01     in the DSP menu
  rx->nb_thresh =  4.95;          // Threshold=30 in the DSP menu
  rx->nb2_mode = 0;               // Zero mode
#ifdef EXTNR
  rx->nr4_reduction_amount = 10.0;
  rx->nr4_smoothing_factor = 0.0;
  rx->nr4_whitening_factor = 0.0;
  rx->nr4_noise_rescale = 2.0;
  rx->nr4_post_threshold = -10.0;
#endif
  rx->agc = AGC_MEDIUM;
  rx->agc_gain = 80.0;
  rx->agc_slope = 35.0;
  rx->agc_hang_threshold = 0.0;
  rx->local_audio = 0;
  g_mutex_init(&rx->local_audio_mutex);
  rx->local_audio_buffer = NULL;
  snprintf(rx->audio_name, sizeof(rx->audio_name), "NO AUDIO");
  rx->mute_when_not_active = 0;
  rx->audio_channel = STEREO;
  rx->audio_device = -1;
  rx->squelch_enable = 0;
  rx->squelch = 0;
  rx->binaural = 0;
  rx->filter_high = 525;
  rx->filter_low = 275;
  rx->deviation = 2500;
  rx->mute_radio = 0;
  rx->zoom = 1;
  rx->pan = 0;
  rx->analyzer_initializing = 0;
  rx->eq_enable = 0;
  rx->eq_freq[0]  =     0.0;
  rx->eq_freq[1]  =    50.0;
  rx->eq_freq[2]  =   100.0;
  rx->eq_freq[3]  =   200.0;
  rx->eq_freq[4]  =   500.0;
  rx->eq_freq[5]  =  1000.0;
  rx->eq_freq[6]  =  1500.0;
  rx->eq_freq[7]  =  2000.0;
  rx->eq_freq[8]  =  2500.0;
  rx->eq_freq[9]  =  3000.0;
  rx->eq_freq[10] =  5000.0;
  rx->eq_gain[0]  = 0.0;
  rx->eq_gain[1]  = 0.0;
  rx->eq_gain[2]  = 0.0;
  rx->eq_gain[3]  = 0.0;
  rx->eq_gain[4]  = 0.0;
  rx->eq_gain[5]  = 0.0;
  rx->eq_gain[6]  = 0.0;
  rx->eq_gain[7]  = 0.0;
  rx->eq_gain[8]  = 0.0;
  rx->eq_gain[9]  = 0.0;
  rx->eq_gain[10] = 0.0;
  //
  // Overwrite all these values with data from the props file
  //
  rx_restore_state(rx);

  //
  // Guard against "old" entries
  //
  if (rx->pan > 100) { rx->pan = 0; }

  //
  // If this is the second receiver in P1, over-write sample rate
  // with that of the first  receiver. Different sample rates in
  // the props file may arise due to illegal hand editing or
  // firmware downgrade from P2 to P1. The same applies to
  // a LIMESDR with 2RX where we keep both receivers
  // at the same sample rate.
  //
  if ((protocol == ORIGINAL_PROTOCOL || have_lime) && id == 1) {
    rx->sample_rate = receiver[0]->sample_rate;
  }

  //
  // allocate buffers
  //
  rx->iq_input_buffer = g_new(double, 2 * rx->buffer_size);
  rx->pixels = width;
  rx->pixel_samples = g_new(float, rx->pixels);
  t_print("%s (after restore): id=%d local_audio=%d\n", __FUNCTION__, rx->id, rx->local_audio);
  int scale = rx->sample_rate / 48000;
  rx->output_samples = rx->buffer_size / scale;
  rx->audio_output_buffer = g_new(double, 2 * rx->output_samples);
  t_print("%s: RXid=%d output_samples=%d audio_output_buffer=%p\n", __FUNCTION__, rx->id, rx->output_samples,
          rx->audio_output_buffer);
  // setup wdsp for this receiver
  t_print("%s: RXid=%d after restore adc=%d\n", __FUNCTION__, rx->id, rx->adc);
  t_print("%s: OpenChannel RXid=%d buffer_size=%d dsp_size=%d fft_size=%d sample_rate=%d\n",
          __FUNCTION__,
          rx->id,
          rx->buffer_size,
          rx->dsp_size,
          rx->fft_size,
          rx->sample_rate);
  OpenChannel(rx->id,                     // channel
              rx->buffer_size,            // in_size
              rx->dsp_size,               // dsp_size
              rx->sample_rate,            // input_samplerate
              48000,                      // dsp rate
              48000,                      // output_samplerate
              0,                          // type (0=receive)
              1,                          // state (run)
              0.010, 0.025, 0.0, 0.010,   // DelayUp, SlewUp, DelayDown, SlewDown
              1);                         // Wait for data in fexchange0
  //
  // noise blankers
  //
  create_anbEXT(rx->id, 1, rx->buffer_size, rx->sample_rate, 0.0001, 0.0001, 0.0001, 0.05, 20);
  create_nobEXT(rx->id, 1, 0, rx->buffer_size, rx->sample_rate, 0.0001, 0.0001, 0.0001, 0.05, 20);
  //
  // Some WDSP settings that are never changed
  //
  SetRXABandpassWindow(rx->id, 1);    // use 7-term BlackmanHarris Window
  SetRXABandpassRun(rx->id, 1);       // enable Bandbass
  SetRXAAMDSBMode(rx->id, 0);         // use both sidebands in SAM
  SetRXAPanelRun(rx->id, 1);          // turn on RXA panel
  SetRXAPanelSelect(rx->id, 3);       // use both I and Q input
  //
  // Apply initial settings
  //
  rx_set_noise(rx);
  rx_set_fft_size(rx);
  rx_set_fft_latency(rx);
  rx_set_offset(rx);
  rx_set_af_gain(rx);
  rx_set_af_binaural(rx);
  rx_set_equalizer(rx);
  rx_mode_changed(rx);   // this will call rx_filter_changed() as well
  rx_create_analyzer(rx);
  rx_set_detector(rx);
  rx_set_average(rx);
  rx_create_visual(rx);

  if (rx->local_audio) {
    if (audio_open_output(rx) < 0) {
      rx->local_audio = 0;
    }
  }

  // defer set_agc until here, otherwise the AGC threshold is not computed correctly
  rx_set_agc(rx);
  rx->txrxcount = 0;
  rx->txrxmax = 0;
  return rx;
}

void rx_change_adc(const RECEIVER *rx) {
  ASSERT_SERVER();
  schedule_high_priority();
  schedule_receive_specific();
}

void rx_set_frequency(const RECEIVER *rx, long long f) {
  ASSERT_SERVER();
  int id = rx->id;

  //
  // update VFO frequency, and let rx_frequency_changed do the rest
  //
  if (vfo[id].ctun) {
    vfo[id].ctun_frequency = f;
  } else {
    vfo[id].frequency = f;
  }

  rx_frequency_changed(rx);
}

void rx_frequency_changed(const RECEIVER *rx) {
  ASSERT_SERVER();
  int id = rx->id;

  if (vfo[id].ctun) {
    long long frequency = vfo[id].frequency;
    long long half = (long long)rx->sample_rate / 2LL;
    long long rx_low = vfo[id].ctun_frequency + rx->filter_low;
    long long rx_high = vfo[id].ctun_frequency + rx->filter_high;

    if (rx_low < frequency - half || rx_high > frequency + half) {
      //
      // Perhaps this is paranoia, but a "legal" VFO might turn
      // into an "illegal" when when reducing the sample rate,
      // thus narrowing the CTUN window.
      // If the "filter window" has left the CTUN range, CTUN is
      // reset such that the CTUN center frequency is placed at
      // the new frequency
      //
      t_print("%s: CTUN freq out of range\n", __FUNCTION__);
      vfo[id].frequency = vfo[id].ctun_frequency;
    }

    //
    // Compute new offset
    //
    vfo[id].offset = vfo[id].ctun_frequency - vfo[id].frequency;

    if (vfo[id].rit_enabled) {
      vfo[id].offset += vfo[id].rit;
    }
  } else {
    //
    // This may be part of a CTUN ON->OFF transition
    //
    vfo[id].offset = 0;

    if (vfo[id].rit_enabled) {
      vfo[id].offset = vfo[id].rit;
    }
  }

  //
  // To make this bullet-proof, report the (possibly new) offset to WDSP
  // and send the (possibly changed) frequency to the radio in any case.
  //
  rx_set_offset(rx);

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    // P1 does this automatically
    break;

  case NEW_PROTOCOL:
    schedule_high_priority(); // send new frequency
    break;

  case SOAPYSDR_PROTOCOL:
#if SOAPYSDR
    soapy_protocol_set_rx_frequency(id);
#endif
    break;
  }
}

void rx_filter_changed(RECEIVER *rx) {
  ASSERT_SERVER();
  rx_set_filter(rx);

  if (can_transmit) {
    if ((transmitter->use_rx_filter && rx == active_receiver) || vfo_get_tx_mode() == modeFMN) {
      tx_set_filter(transmitter);
    }
  }

  //
  // TODO: Filter window has possibly moved outside CTUN range
  //
}

void rx_mode_changed(RECEIVER *rx) {
  ASSERT_SERVER();
  rx_set_mode(rx);
  rx_filter_changed(rx);
  rx_set_offset(rx);         // CW BFO offset
}

void rx_vfo_changed(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // Called when the VFO controlling rx has changed,
  // e.g. after a "swap VFO" action
  //
  rx_frequency_changed(rx);
  rx_mode_changed(rx);
}

//////////////////////////////////////////////////////////////////////////////////////
//
// rx_add_iq_samples (rx_add_div_iq_samples),  rx_full_buffer, and rx_process_buffer
// form the "RX engine".
//
//////////////////////////////////////////////////////////////////////////////////////

static void rx_process_buffer(RECEIVER *rx) {
  ASSERT_SERVER();

  for (int i = 0; i < rx->output_samples; i++) {
    double left_sample = rx->audio_output_buffer[i * 2];
    double right_sample = rx->audio_output_buffer[(i * 2) + 1];

    if (rx == active_receiver) {
      //
      // If re-playing captured data locally, replace incoming
      // audio samples by captured data (active RX only)
      //
      if (capture_state == CAP_REPLAY) {
        if (capture_replay_pointer < capture_record_pointer) {
          left_sample = right_sample = capture_data[capture_replay_pointer++];
        } else {
          //
          // switching the state to REPLAY_DONE takes care that the
          // REPLAY switch is "pressed" only once
          capture_state = CAP_REPLAY_DONE;
          schedule_action(REPLAY, PRESSED, 0);
        }
      }

      //
      // If CAPTURing, record the audio samples *before*
      // manipulating them
      //
      if (capture_state == CAP_RECORDING) {
        if (capture_record_pointer < capture_max) {
          //
          // normalise samples:
          // when using AGC, the samples of strong s9 signals are about 0.8
          //
          double scale = 0.6 * pow(10.0, -0.05 * rx->volume);
          capture_data[capture_record_pointer++] = scale * (left_sample + right_sample);
        } else {
          // switching the state to RECORD_DONE takes care that the
          // CAPTURE switch is "pressed" only once
          capture_state = CAP_RECORD_DONE;
          schedule_action(CAPTURE, PRESSED, 0);
        }
      }
    }

    if (radio_is_transmitting() && (!duplex || mute_rx_while_transmitting)) {
      left_sample = 0.0;
      right_sample = 0.0;
    }

    if (rx->mute_radio || (rx != active_receiver && rx->mute_when_not_active)) {
      left_sample = 0.0;
      right_sample = 0.0;
    }

    switch (rx->audio_channel) {
    case STEREO:
      break;

    case LEFT:
      right_sample = 0.0;
      break;

    case RIGHT:
      left_sample = 0.0;
      break;
    }

    int left_audio_sample = (short)(left_sample * 32767.0);
    int right_audio_sample = (short)(right_sample * 32767.0);

    if (rx->local_audio) {
      audio_write(rx, (float)left_sample, (float)right_sample);
    }

    if (remoteclient.running) {
      remote_rxaudio(rx, left_audio_sample, right_audio_sample);
    }

    if (rx == active_receiver && !pre_mox) {
      switch (protocol) {
      case ORIGINAL_PROTOCOL:
        old_protocol_audio_samples(left_audio_sample, right_audio_sample);
        break;

      case NEW_PROTOCOL:
        new_protocol_audio_samples(left_audio_sample, right_audio_sample);
        break;

      case SOAPYSDR_PROTOCOL:
        break;
      }
    }
  }
}

static void rx_full_buffer(RECEIVER *rx) {
  ASSERT_SERVER();
  int error;

  //t_print("%s: rx=%p\n",__FUNCTION__,rx);
  //
  // rx->mutex is locked if a sample rate change is currently going on,
  // in this case we should not block the receiver thread
  //
  if (g_mutex_trylock(&rx->mutex)) {
    //
    // noise blanker works on original IQ samples with input sample rate
    //
    switch (rx->nb) {
    case 1:
      xanbEXT (rx->id, rx->iq_input_buffer, rx->iq_input_buffer);
      break;

    case 2:
      xnobEXT (rx->id, rx->iq_input_buffer, rx->iq_input_buffer);
      break;

    default:
      // do nothing
      break;
    }

    fexchange0(rx->id, rx->iq_input_buffer, rx->audio_output_buffer, &error);

    if (error != 0) {
      t_print("%s: id=%d fexchange0: error=%d\n", __FUNCTION__, rx->id, error);
    }

    if (rx->displaying) {
      g_mutex_lock(&rx->display_mutex);
      Spectrum0(1, rx->id, 0, 0, rx->iq_input_buffer);
      g_mutex_unlock(&rx->display_mutex);
    }

    rx_process_buffer(rx);
    g_mutex_unlock(&rx->mutex);
  }
}

void rx_add_iq_samples(RECEIVER *rx, double i_sample, double q_sample) {
  ASSERT_SERVER();

  //
  // At the end of a TX/RX transition, txrxcount is set to zero,
  // and txrxmax to some suitable value.
  // Then, the first txrxmax RXIQ samples are "silenced"
  // This is necessary on systems where RX feedback samples
  // from cross-talk at the TRX relay arrive with some delay.
  //
  // If txrxmax is zero, no "silencing" takes place here,
  // this is the case for radios not showing this problem,
  // and generally if in CW mode or using duplex.
  //
  if (rx->txrxcount < rx->txrxmax) {
    i_sample = 0.0;
    q_sample = 0.0;
    rx->txrxcount++;
  }

  rx->iq_input_buffer[rx->samples * 2] = i_sample;
  rx->iq_input_buffer[(rx->samples * 2) + 1] = q_sample;
  rx->samples = rx->samples + 1;

  if (rx->samples >= rx->buffer_size) {
    rx_full_buffer(rx);
    rx->samples = 0;
  }
}

void rx_add_div_iq_samples(RECEIVER *rx, double i0, double q0, double i1, double q1) {
  ASSERT_SERVER();
  //
  // Note that we sum the second channel onto the first one
  // and then simply pass to add_iq_samples
  //
  double i_sample = i0 + (div_cos * i1 - div_sin * q1);
  double q_sample = q0 + (div_sin * i1 + div_cos * q1);
  rx_add_iq_samples(rx, i_sample, q_sample);
}

void rx_update_width(RECEIVER *rx) {
  //
  // This is called when the display width changes
  // CALL THIS ONLY with rx->display_mutex locked.
  //
  rx->pixels = rx->width;

  if (!radio_is_remote) {
    if (rx->pixel_samples != NULL) {
      g_free(rx->pixel_samples);
    }

    rx->pixel_samples = g_new(float, rx->pixels);
    rx_set_analyzer(rx);
  }
}

void rx_update_pan(RECEIVER *rx) {
  //
  // This is called when the pan value changes
  //
  if (radio_is_remote) {
    send_pan(client_socket, rx);
  } else {
    rx_set_analyzer(rx);
  }
}

static void rx_adjust_pan(RECEIVER * rx) {
  //
  // This adjusts the pan value such that the
  // current RX frequency (both with and without CTUN) is
  // in the centre of the screen.
  //
  int id = rx->id;

  if (vfo[id].ctun && rx->zoom != 1) {
    int offset = (vfo[id].ctun_frequency - vfo[id].frequency) + rx->sample_rate / 2;
    double z = ((double)(offset * rx->zoom) / (double)rx->sample_rate - 0.5) / (double)(rx->zoom - 1);
    rx->pan = (int) (200.0 * z + 0.5) - 100;
  } else {
    rx->pan = 0;
  }

  g_idle_add(sliders_pan, GINT_TO_POINTER(100 + rx->id));

  if (radio_is_remote) {
    send_pan(client_socket, rx);
  } else {
    rx_set_analyzer(rx);
  }
}

void rx_update_zoom(RECEIVER *rx) {
  //
  // This is called whenever rx->zoom changes,
  //
  g_idle_add(sliders_zoom, GINT_TO_POINTER(100  + rx->id));
  g_idle_add(ext_vfo_update, NULL);
  rx_adjust_pan(rx);

  if (radio_is_remote) {
    send_zoom(client_socket, rx);
    return;
  }

  rx_set_analyzer(rx);
  //
  // re-calculate AGC line for panadapter since it depends on afft_size
  //
  GetRXAAGCThresh(rx->id, &rx->agc_thresh, (double)rx->afft_size, (double)rx->sample_rate);
}

void rx_set_filter(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // - set filter edges and deviation in rx
  // - determine on the use of the CW peak filter
  // - re-caloc AGC since this depends on the filter width
  //
  int id = rx->id;
  int m = vfo[id].mode;
  FILTER *mode_filters = filters[m];
  const FILTER *filter = &mode_filters[vfo[id].filter]; // ignored in FMN
  int have_peak = 0;

  switch (m) {
  case modeCWL:
    rx->filter_low = -cw_keyer_sidetone_frequency + filter->low;
    rx->filter_high = -cw_keyer_sidetone_frequency + filter->high;
    have_peak = vfo[id].cwAudioPeakFilter;
    break;

  case modeCWU:
    rx->filter_low = cw_keyer_sidetone_frequency + filter->low;
    rx->filter_high = cw_keyer_sidetone_frequency + filter->high;
    have_peak = vfo[id].cwAudioPeakFilter;
    break;

  case  modeFMN:
    //
    // for FM filter edges are calculated from the deviation,
    // Using Carson's rule and assuming max. audio  freq  of 3000 Hz
    //
    rx->deviation = vfo[id].deviation;
    rx->filter_low = -(3000 + rx->deviation);
    rx->filter_high = (3000 + rx->deviation);
    break;

  default:
    rx->filter_low = filter->low;
    rx->filter_high = filter->high;
    break;
  }

  rx_set_deviation(rx);
  rx_set_bandpass(rx);
  rx_set_cw_peak(rx, have_peak, (double) cw_keyer_sidetone_frequency);
  rx_set_agc(rx);
}

void rx_set_framerate(RECEIVER *rx) {
  ASSERT_SERVER();

  //
  // When changing the frame rate, the RX display update timer needs
  // be restarted, the averaging re-calculated, and the analyzer
  // parameter re-set
  //
  if (radio_is_remote) {
    return;
  }

  rx_set_displaying(rx);
  rx_set_average(rx);
  rx_set_analyzer(rx);
}

///////////////////////////////////////////////////////
//
// WDSP wrappers.
// Calls to WDSP functions above this line should be
// kept to a minimum, and in general a call to a WDSP
// function should only occur *once* in the program,
// at best in a wrapper.
//
////////////////////////////////////////////////////////

void rx_change_sample_rate(RECEIVER *rx, int sample_rate) {
  //
  // If the sample rate decreases, a valid CTUN offset may become invalid
  //
  if (rx->sample_rate > sample_rate) {
    vfo_id_ctun_update(rx->id, 0);
    rx_adjust_pan(rx);
  }

  g_mutex_lock(&rx->mutex);
  rx->sample_rate = sample_rate;
  int scale = rx->sample_rate / 48000;
  rx->output_samples = rx->buffer_size / scale;
  t_print("%s: id=%d rate=%d scale=%d buffer_size=%d output_samples=%d\n", __FUNCTION__, rx->id, sample_rate, scale,
          rx->buffer_size, rx->output_samples);

  if (!radio_is_remote) {
    schedule_receive_specific();

    //
    // In the old protocol, the RX_FEEDBACK sample rate is tied
    // to the radio's sample rate and therefore may vary.
    // Since there is no downstream WDSP receiver her, the only thing
    // we have to do here is to adapt the spectrum display of the
    // feedback and *must* then return (rx->id is not a WDSP channel!)
    //
    if (rx->id == PS_RX_FEEDBACK && protocol == ORIGINAL_PROTOCOL) {
      //rx->pixels = duplex ? 4 * tx_dialog_width : rx->width;
      //g_free(rx->pixel_samples);
      //rx->pixel_samples = g_new(float, rx->pixels);
      rx_set_analyzer(rx);
      t_print("%s: PS RX FEEDBACK: id=%d rate=%d buffer_size=%d output_samples=%d\n",
              __FUNCTION__, rx->id, rx->sample_rate, rx->buffer_size, rx->output_samples);
      g_mutex_unlock(&rx->mutex);
      return;
    }

    //
    // re-calculate AGC line for panadapter since it depends on sample rate
    //
    GetRXAAGCThresh(rx->id, &rx->agc_thresh, (double)rx->afft_size, (double)rx->sample_rate);

    //
    // If the sample rate is reduced, the size of the audio output buffer must ber increased
    //
    if (rx->audio_output_buffer != NULL) {
      g_free(rx->audio_output_buffer);
    }

    rx->audio_output_buffer = g_new(double, 2 * rx->output_samples);
    rx_off(rx);
    rx_set_analyzer(rx);
    SetInputSamplerate(rx->id, sample_rate);
    SetEXTANBSamplerate (rx->id, sample_rate);
    SetEXTNOBSamplerate (rx->id, sample_rate);

    if (protocol == SOAPYSDR_PROTOCOL) {
#ifdef SOAPYSDR
      soapy_protocol_change_rx_sample_rate(rx);
#endif
    }

    rx_on(rx);
  }

  g_mutex_unlock(&rx->mutex);
  t_print("%s: RXid=%d rate=%d buffer_size=%d output_samples=%d\n", __FUNCTION__, rx->id, rx->sample_rate,
          rx->buffer_size, rx->output_samples);
}

void rx_close(const RECEIVER *rx) {
  ASSERT_SERVER();
  CloseChannel(rx->id);
}

void rx_get_pixels(RECEIVER *rx) {
  ASSERT_SERVER();
  int rc;
  GetPixels(rx->id, 0, rx->pixel_samples, &rc);
  rx->pixels_available = rc;
}

double rx_get_smeter(const RECEIVER *rx) {
  ASSERT_SERVER(0.0);
  double level;

  switch (rx->smetermode) {
  case SMETER_PEAK:
    level = GetRXAMeter(rx->id, RXA_S_PK);
    break;

  case SMETER_AVERAGE:
  default:
    level = GetRXAMeter(rx->id, RXA_S_AV);
    break;
  }

  return level;
}

void rx_create_analyzer(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // After the analyzer has been created, its parameters
  // are set via rx_set_analyzer
  //
  int rc;
  XCreateAnalyzer(rx->id, &rc, 262144, 1, 1, NULL);

  if (rc != 0) {
    t_print("CreateAnalyzer failed for RXid=%d\n", rx->id);
  } else {
    rx_set_analyzer(rx);
  }
}

void rx_set_analyzer(RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // The analyzer depends on the framerate (fps), the
  // number of pixels, and the sample rate, as well as the
  // buffer size (this is constant).
  // So rx_set_analyzer() has to be called whenever fps, pixels,
  // or sample_rate change in rx
  //
  int flp[] = {0};
  const double keep_time = 0.1;
  const int n_pixout = 1;
  const int spur_elimination_ffts = 1;
  const int data_type = 1;
  const double kaiser_pi = 14.0;
  double fscLin = 0;
  double fscHin = 0;
  const int stitches = 1;
  const int calibration_data_set = 0;
  const double span_min_freq = 0.0;
  const double span_max_freq = 0.0;
  const int clip = 0;
  const int window_type = 5;
  const int pixels = rx->pixels;
  int overlap;
  int max_w;

  if (rx->id == PS_RX_FEEDBACK) {
    //
    // RX FEEDBACK receiver:
    // Here we use a hard-wired zoom factor. We display exactly 24 kHz of the
    // spectrum thus have to clip off. The sample rate of this rx will
    // be about 192k (zoom = 8), so a fixed width of 16k is fine.
    //
    rx->afft_size = 16384;
    fscLin = rx->afft_size * (0.5 - 12000.0 / rx->sample_rate);
    fscHin = rx->afft_size * (0.5 - 12000.0 / rx->sample_rate);
  } else {
    //
    // determine clippings accordint to the Zoom/Pan values
    //
    rx->afft_size = rx->width * rx->zoom;

    //
    // For a screen width of 4k pixels, and zoom factor of 32,
    // this can go up to 128k. The program limits this to 256k
    //
    if (rx->afft_size <= 16384) {
      rx->afft_size = 16384;
    } else if (rx->afft_size <= 32768) {
      rx->afft_size = 32768;
    } else if (rx->afft_size <= 65536) {
      rx->afft_size = 65536;
    } else if (rx->afft_size <= 131072) {
      rx->afft_size = 131072;
    } else {
      rx->afft_size = 262144;
    }

    double zz = rx->afft_size * (1.0 - 1.0 / rx->zoom);
    double pl = 0.005 * (rx->pan + 100);
    double pr = 1.0 - pl;
    fscLin = pl * zz;
    fscHin = pr * zz;
    rx->cA  = rx->sample_rate * ((0.005 - 0.005 / rx->zoom) * (rx->pan + 100) - 0.5);
    rx->cB  = (double)rx->sample_rate / (double)(rx->width * rx->zoom); // Hz per pixel
    rx->cAp = 1.0 / rx->cB;
    rx->cBp = -rx->cA / rx->cB;
  }

  max_w = rx->afft_size + (int) min(keep_time * (double) rx->sample_rate,
                                    keep_time * (double) rx->afft_size * (double) rx->fps);
  overlap = (int)fmax(0.0, ceil(rx->afft_size - (double)rx->sample_rate / (double)rx->fps));
  SetAnalyzer(rx->id,
              n_pixout,
              spur_elimination_ffts,                // number of LO frequencies = number of ffts used in elimination
              data_type,                            // 0 for real input data (I only); 1 for complex input data (I & Q)
              flp,                                  // vector with one element for each LO frequency, 1 if high-side LO, 0 otherwise
              rx->afft_size,                        // size of the fft, i.e., number of input samples
              rx->buffer_size,                      // number of samples transferred for each OpenBuffer()/CloseBuffer()
              window_type,                          // integer specifying which window function to use
              kaiser_pi,                            // PiAlpha parameter for Kaiser window
              overlap,                              // number of samples each fft (other than the first) is to re-use from the previous
              clip,                                 // number of fft output bins to be clipped from EACH side of each sub-span
              fscLin,                               // number of bins to clip from low end of entire span
              fscHin,                               // number of bins to clip from high end of entire span
              pixels,                               // number of pixel values to return.  may be either <= or > number of bins
              stitches,                             // number of sub-spans to concatenate to form a complete span
              calibration_data_set,                 // identifier of which set of calibration data to use
              span_min_freq,                        // frequency at first pixel value
              span_max_freq,                        // frequency at last pixel value
              max_w                                 // max samples to hold in input ring buffers
             );

  //
  // The spectrum is normalized to a "bin width" of sample_rate / afft_size,
  // which is smaller than the frequency width of one pixel which is sample_rate / (width * zoom).
  //
  // A normalization to "1 pixel" is accomplished with the following two calls. Note the noise
  // floor then depends on the zoom factor (that is, the frequency width of one pixel)
  //
  // In effect, this "lifts" the spectrum (in dB) by 10*log10(afft_size/(width*zoom)).
  //
  // One can also normalise to 1 Hz,in the case the second parameter to SetDisplaySampleRate
  // must be (the true) rx->sample_rate, then WDSP adds 10*log10(afft_size/sample_rate) which
  // normally means the spectrum is down-shifted quite a bit.
  //
  if (rx->id != PS_RX_FEEDBACK) {
    SetDisplayNormOneHz(rx->id, 0, 1);
    SetDisplaySampleRate(rx->id, rx->width * rx->zoom);
  }

  rx->analyzer_initializing = 1;
}

void rx_off(const RECEIVER *rx) {
  ASSERT_SERVER();
  // switch receiver OFF, wait until slew-down completet
  SetChannelState(rx->id, 0, 1);
}

void rx_on(const RECEIVER *rx) {
  ASSERT_SERVER();
  // switch receiver ON
  SetChannelState(rx->id, 1, 0);
}

void rx_set_af_binaural(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_afbinaural(client_socket, rx);
    return;
  }

  SetRXAPanelBinaural(rx->id, rx->binaural);
}

void rx_set_af_gain(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_volume(client_socket, rx->id, rx->volume);
    return;
  }

  //
  // volume is in dB from 0 ... -40 and this is
  // converted to  an amplitude from 0 ... 1.
  //
  // volume < -39.5  ==> amplitude = 0
  // volume >   0.0  ==> amplitude = 1
  //
  double volume = rx->volume;
  double amplitude;

  if (volume <= -39.5) {
    amplitude = 0.0;
  } else if (volume > 0.0) {
    amplitude = 1.0;
  } else {
    amplitude = pow(10.0, 0.05 * volume);
  }

  SetRXAPanelGain1 (rx->id, amplitude);
}

void rx_set_agc(RECEIVER *rx) {
  if (radio_is_remote) {
    send_agc_gain(client_socket, rx);
    return;
  }

  //
  // Apply the AGC settings stored in rx.
  // Calculate new AGC and "hang" line levels
  // and store these in rx.
  //
  int id = rx->id;
  SetRXAAGCMode(id, rx->agc);
  SetRXAAGCSlope(id, rx->agc_slope);
  SetRXAAGCTop(id, rx->agc_gain);

  switch (rx->agc) {
  case AGC_OFF:
    break;

  case AGC_LONG:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 2000);
    SetRXAAGCDecay(id, 2000);
    SetRXAAGCHangThreshold(id, (int)rx->agc_hang_threshold);
    break;

  case AGC_SLOW:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 1000);
    SetRXAAGCDecay(id, 500);
    SetRXAAGCHangThreshold(id, (int)rx->agc_hang_threshold);
    break;

  case AGC_MEDIUM:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 0);
    SetRXAAGCDecay(id, 250);
    SetRXAAGCHangThreshold(id, 100);
    break;

  case AGC_FAST:
    SetRXAAGCAttack(id, 2);
    SetRXAAGCHang(id, 0);
    SetRXAAGCDecay(id, 50);
    SetRXAAGCHangThreshold(id, 100);
    break;
  }

  //
  // Recalculate the "panadapter" AGC line positions.
  //
  GetRXAAGCHangLevel(id, &rx->agc_hang);
  GetRXAAGCThresh(id, &rx->agc_thresh, (double)rx->afft_size, (double)rx->sample_rate);

  //
  // Update mode settings, if this is RX1
  //
  if (id == 0) {
    int mode = vfo[id].mode;
    mode_settings[mode].agc = rx->agc;
    copy_mode_settings(mode);
  }
}

void rx_set_average(const RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // avgmode refers to the _display_enum, while
  // wdspmode reflects the internal encoding in WDSP
  //
  int wdspmode;
  double t = 0.001 * rx->display_average_time;
  double display_avb = exp(-1.0 / ((double)rx->fps * t));
  int display_average = max(2, (int)fmin(60, (double)rx->fps * t));
  SetDisplayAvBackmult(rx->id, 0, display_avb);
  SetDisplayNumAverage(rx->id, 0, display_average);

  switch (rx->display_average_mode) {
  case AVG_NONE:
    wdspmode = AVERAGE_MODE_NONE;
    break;

  case AVG_RECURSIVE:
    wdspmode = AVERAGE_MODE_RECURSIVE;
    break;

  case AVG_LOGRECURSIVE:
  default:
    wdspmode = AVERAGE_MODE_LOG_RECURSIVE;
    break;

  case AVG_TIMEWINDOW:
    wdspmode = AVERAGE_MODE_TIME_WINDOW;
    break;
  }

  //
  // I observed artifacts when changing the mode from "Log Recursive"
  // to "Time Window", so I generally switch to NONE first, and then
  // to the target averaging mode
  //
  SetDisplayAverageMode(rx->id, 0, AVERAGE_MODE_NONE);
  usleep(50000);
  SetDisplayAverageMode(rx->id, 0, wdspmode);
}

void rx_set_bandpass(const RECEIVER *rx) {
  ASSERT_SERVER();
  RXASetPassband(rx->id, (double)rx->filter_low, (double)rx->filter_high);
}

void rx_set_cw_peak(const RECEIVER *rx, int state, double freq) {
  ASSERT_SERVER();

  if (state) {
    double w = 0.25 * (rx->filter_high - rx->filter_low);

    if (w < 0.0) { w = -w; }      // This happens with CWL

    if (w < 25.0) { w = 25.0; }   // Do not go below 25 Hz

    SetRXASPCWFreq(rx->id, freq);
    SetRXASPCWBandwidth(rx->id, w);
    SetRXASPCWGain(rx->id, 1.50);
  }

  SetRXASPCWRun(rx->id, state);
}

void rx_set_detector(const RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // Apply display detector mode stored in rx
  //
  int wdspmode;

  switch (rx->display_detector_mode) {
  case DET_PEAK:
    wdspmode = DETECTOR_MODE_PEAK;
    break;

  case DET_AVERAGE:
  default:
    wdspmode = DETECTOR_MODE_AVERAGE;
    break;

  case DET_ROSENFELL:
    wdspmode = DETECTOR_MODE_ROSENFELL;
    break;

  case DET_SAMPLEHOLD:
    wdspmode = DETECTOR_MODE_SAMPLE;
    break;
  }

  SetDisplayDetectorMode(rx->id, 0, wdspmode);
}

void rx_set_deviation(const RECEIVER *rx) {
  ASSERT_SERVER();
  SetRXAFMDeviation(rx->id, (double)rx->deviation);
}

void rx_capture_start(const RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // Turn OFF equalizer, but leave mode_settings and RX
  // data structure unaffected.
  //
  SetRXAEQRun(rx->id, 0);
}

void rx_capture_end(const RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // Restore equalizer setting from RX data
  //
  SetRXAEQRun(rx->id, rx->eq_enable);
}

void rx_set_equalizer(RECEIVER *rx) {
  if (radio_is_remote) {
    send_eq(client_socket, rx->id);
    return;
  }

  if (rx->id == 0) {
    int mode = vfo[rx->id].mode;
    mode_settings[mode].en_rxeq = rx->eq_enable;

    for (int i = 0; i < 11; i++) {
      mode_settings[mode].rx_eq_freq[i] = rx->eq_freq[i];
      mode_settings[mode].rx_eq_gain[i] = rx->eq_gain[i];
    }

    copy_mode_settings(mode);
  }

  g_idle_add(ext_vfo_update, NULL);
  //
  // Apply the equaliser parameters stored in rx
  //
  SetRXAEQProfile(rx->id, 10, rx->eq_freq, rx->eq_gain);
  SetRXAEQRun(rx->id, rx->eq_enable);
}

void rx_set_fft_latency(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_rx_fft(client_socket, rx);
    return;
  }

  RXASetMP(rx->id, rx->low_latency);
}

void rx_set_fft_size(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_rx_fft(client_socket, rx);
    return;
  }

  RXASetNC(rx->id, rx->fft_size);
}

void rx_set_mode(const RECEIVER *rx) {
  ASSERT_SERVER();
  //
  // Change the  mode of a running receiver according to what it stored
  // in its controlling VFO.
  //
  SetRXAMode(rx->id, vfo[rx->id].mode);
  rx_set_squelch(rx);
}

void rx_set_noise(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_noise(client_socket, rx);
    return;
  }

  if (rx->id == 0) {
    int mode = vfo[rx->id].mode;
    mode_settings[mode].nr = rx->nr;
    mode_settings[mode].nb = rx->nb;
    mode_settings[mode].anf = rx->anf;
    mode_settings[mode].snb = rx->snb;
    mode_settings[mode].nr2_ae = rx->nr2_ae;
    mode_settings[mode].nr_agc = rx->nr_agc;
    mode_settings[mode].nb2_mode = rx->nb2_mode;
    mode_settings[mode].nr2_gain_method = rx->nr2_gain_method;
    mode_settings[mode].nr2_npe_method = rx->nr2_npe_method;
    mode_settings[mode].nr2_trained_threshold = rx->nr2_trained_threshold;
    mode_settings[mode].nr2_trained_t2 = rx->nr2_trained_t2;
    mode_settings[mode].nb_tau = rx->nb_tau;
    mode_settings[mode].nb_advtime = rx->nb_advtime;
    mode_settings[mode].nb_hang = rx->nb_hang;
    mode_settings[mode].nb_thresh = rx->nb_thresh;
#ifdef EXTNR
    mode_settings[mode].nr4_reduction_amount = rx->nr4_reduction_amount;
    mode_settings[mode].nr4_smoothing_factor = rx->nr4_smoothing_factor;
    mode_settings[mode].nr4_whitening_factor = rx->nr4_whitening_factor;
    mode_settings[mode].nr4_noise_rescale = rx->nr4_noise_rescale;
    mode_settings[mode].nr4_post_threshold = rx->nr4_post_threshold;
#endif
    copy_mode_settings(mode);
  }

  g_idle_add(ext_vfo_update, NULL);
  //
  // Set/Update all parameters stored  in rx
  // that areassociated with the "QRM fighters"
  //
  // a) NB
  //
  SetEXTANBTau(rx->id, rx->nb_tau);
  SetEXTANBHangtime(rx->id, rx->nb_hang);
  SetEXTANBAdvtime(rx->id, rx->nb_advtime);
  SetEXTANBThreshold(rx->id, rx->nb_thresh);
  SetEXTANBRun(rx->id, (rx->nb == 1));
  //
  // b) NB2
  //
  SetEXTNOBMode(rx->id, rx->nb2_mode);
  SetEXTNOBTau(rx->id, rx->nb_tau);
  SetEXTNOBHangtime(rx->id, rx->nb_hang);
  SetEXTNOBAdvtime(rx->id, rx->nb_advtime);
  SetEXTNOBThreshold(rx->id, rx->nb_thresh);
  SetEXTNOBRun(rx->id, (rx->nb == 2));
  //
  // c) NR
  //
  SetRXAANRVals(rx->id, 64, 16, 16e-4, 10e-7);
  SetRXAANRPosition(rx->id, rx->nr_agc);
  SetRXAANRRun(rx->id, (rx->nr == 1));
  //
  // d) NR2
  //
  SetRXAEMNRPosition(rx->id, rx->nr_agc);
  SetRXAEMNRgainMethod(rx->id, rx->nr2_gain_method);
  SetRXAEMNRnpeMethod(rx->id, rx->nr2_npe_method);
  SetRXAEMNRtrainZetaThresh(rx->id, rx->nr2_trained_threshold);
  SetRXAEMNRtrainT2(rx->id, rx->nr2_trained_t2);
  SetRXAEMNRaeRun(rx->id, rx->nr2_ae);
  SetRXAEMNRRun(rx->id, (rx->nr == 2));
  //
  // e) ANF
  //
  SetRXAANFRun(rx->id, rx->anf);
  SetRXAANFPosition(rx->id, rx->nr_agc);
  //
  // f) SNB
  //
  SetRXASNBARun(rx->id, rx->snb);
#ifdef EXTNR
  //
  // These WDSP functions only exist in a special, non-official version
  //
  // g) NR3
  //
  SetRXARNNRRun(rx->id, (rx->nr == 3));
  //
  // NR4
  //
  SetRXASBNRreductionAmount(rx->id,     rx->nr4_reduction_amount);
  SetRXASBNRsmoothingFactor(rx->id,     rx->nr4_smoothing_factor);
  SetRXASBNRwhiteningFactor(rx->id,     rx->nr4_whitening_factor);
  SetRXASBNRnoiseRescale(rx->id,        rx->nr4_noise_rescale);
  SetRXASBNRpostFilterThreshold(rx->id, rx->nr4_post_threshold);
  SetRXASBNRRun(rx->id, (rx->nr == 4));
#endif
}

void rx_set_offset(const RECEIVER *rx) {
  ASSERT_SERVER();
  int id = rx->id;
  int mode = vfo[id].mode;
  long long offset = vfo[id].offset;

  //
  // CW BFO offset is done HERE.
  //
  if (mode == modeCWU) {
    offset -= cw_keyer_sidetone_frequency;
  } else if (mode == modeCWL) {
    offset += cw_keyer_sidetone_frequency;
  }

  //
  // CW mode, RIT, and CTUN all lead to non-zero offset
  //
  if (offset == 0) {
    SetRXAShiftFreq(rx->id, (double)offset);
    RXANBPSetShiftFrequency(rx->id, (double)offset);
    SetRXAShiftRun(rx->id, 0);
  } else {
    SetRXAShiftFreq(rx->id, (double)offset);
    RXANBPSetShiftFrequency(rx->id, (double)offset);
    SetRXAShiftRun(rx->id, 1);
  }
}

void rx_set_squelch(const RECEIVER *rx) {
  if (radio_is_remote) {
    send_squelch(client_socket, rx->id, rx->squelch_enable, rx->squelch);
    return;
  }

  int mode = vfo[rx->id].mode;

  if (rx->id == 0) {
    mode_settings[mode].squelch_enable = rx->squelch_enable;
    mode_settings[mode].squelch        = rx->squelch;
    copy_mode_settings(mode);
  }

  //
  // This applies the squelch mode stored in rx
  //
  double value;
  int    fm_squelch = 0;
  int    am_squelch = 0;
  int    voice_squelch = 0;

  //
  // the "slider" value goes from 0 (no squelch) to 100 (fully engaged)
  // and has to be mapped to
  //
  // AM    squelch:   -160.0 ... 0.00 dBm  linear interpolation
  // FM    squelch:      1.0 ... 0.01      expon. interpolation
  // Voice squelch:      0.0 ... 0.75      linear interpolation
  //
  switch (mode) {
  case modeAM:
  case modeSAM:

  // My personal experience is that "Voice squelch" is of very
  // little use  when doing CW (this may apply to "AM squelch", too).
  case modeCWU:
  case modeCWL:
    //
    // Use AM squelch
    //
    value = ((rx->squelch / 100.0) * 160.0) - 160.0;
    SetRXAAMSQThreshold(rx->id, value);
    am_squelch = rx->squelch_enable;
    break;

  case modeLSB:
  case modeUSB:
  case modeDSB:
    //
    // Use Voice squelch (new in WDSP 1.21)
    //
    value = 0.0075 * rx->squelch;
    voice_squelch = rx->squelch_enable;
    SetRXASSQLThreshold(rx->id, value);
    SetRXASSQLTauMute(rx->id, 0.1);
    SetRXASSQLTauUnMute(rx->id, 0.1);
    break;

  case modeFMN:
    //
    // Use FM squelch
    //
    value = pow(10.0, -2.0 * rx->squelch / 100.0);
    SetRXAFMSQThreshold(rx->id, value);
    fm_squelch = rx->squelch_enable;
    break;

  default:
    // no squelch for digital and other modes
    // (this can be discussed).
    break;
  }

  //
  // activate the desired squelch, and deactivate
  // all others
  //
  SetRXAAMSQRun(rx->id, am_squelch);
  SetRXAFMSQRun(rx->id, fm_squelch);
  SetRXASSQLRun(rx->id, voice_squelch);
}

