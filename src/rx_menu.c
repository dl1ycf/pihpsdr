/* Copyright (C)
*  2015 - John Melton, G0ORX/N6LYT
*  2025 - Christoph van Wüllen, DL1YCF
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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "band.h"
#include "client_server.h"
#include "discovered.h"
#include "filter.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "profiles.h"
#include "radio.h"
#include "receiver.h"
#include "rx_menu.h"
#include "sliders.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;
static GtkWidget *output = NULL;
static RECEIVER *myrx;
static int myadc;
static int myid;

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void dither_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_dither(myid, val);
}

static void random_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_random(myid, val);
}

static void preamp_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_preamp(myid, val);
}

static void alex_att_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  radio_set_alex_attenuation(val);
}

static void sample_rate_cb(GtkToggleButton *widget, gpointer data) {
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int samplerate;
  //
  // There are so many different possibilities for sample rates, so
  // we just "scanf" from the combobox text entry
  //
  if (sscanf(p, "%d", &samplerate) != 1) { return; }
  if (radio_is_remote) {
    if (protocol == NEW_PROTOCOL) {
      // change sample rate for current RX only
      send_sample_rate(cl_sock_tcp, myid, samplerate);
    } else {
      // change sample rate for all receivers
      for (int id = 0; id < RECEIVERS; id++) {
        send_sample_rate(cl_sock_tcp, id, samplerate);
      }
    }
    return;
  }
  if (protocol == NEW_PROTOCOL) {
    // change sample rate for THIS receiver only
    rx_change_sample_rate(myrx, samplerate);
  } else {
    // change sample rate for everything
    radio_change_sample_rate(samplerate);
  }
}

static void adc_cb(GtkToggleButton *widget, gpointer data) {
  myadc = myrx->adc = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if (radio_is_remote) {
    send_adc(cl_sock_tcp, myid, myadc);
    return;
  }
  rx_change_adc(myrx);
}

static void sam_sb_cb(GtkWidget *widget, gpointer data) {
  myrx->sam_sb_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  rx_set_sam_mode(myrx);
}

static void fm_limiter_cb(GtkWidget *widget, gpointer data) {
  myrx->fm_limiter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  rx_set_fm_limiter(myrx);
}

static void fm_lim_gain_cb(GtkWidget *widget, gpointer data) {
  myrx->fm_limiter_gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  rx_set_fm_limiter(myrx);
}

static void squelch_value_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  suppress_popup_sliders++;
  radio_set_squelch(myid, value);
  suppress_popup_sliders--;
}

static void squelch_enable_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  radio_set_squelch_enable(myid, val);
}

static void mute_audio_cb(GtkWidget *widget, gpointer data) {
  myrx->mute_when_not_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void mute_radio_cb(GtkWidget *widget, gpointer data) {
  myrx->mute_radio = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void adc_filter_bypass_cb(GtkWidget *widget, gpointer data) {
  int id = GPOINTER_TO_INT(data);
  adc[id].filter_bypass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (radio_is_remote) {
    send_rxmenu(cl_sock_tcp, myadc);
    return;
  }
  schedule_high_priority();
}

//
// possible the device has been changed:
// call audo_close_output with old device, audio_open_output with new one
//
static void local_output_changed_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if (i < 0) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    i = 0;
  }
  if (myrx->local_audio) {
    myrx->local_audio = 0;     // stop writing audio to OLD device
    audio_close_output(myrx);  // audio_close with OLD device
  }
  if (i > 0) {
    snprintf(myrx->audio_name, sizeof(myrx->audio_name), "%s", output_devices[i - 1].name);
    if (audio_open_output(myrx) < 0) {  // audio_open with NEW device...
      myrx->local_audio = 0;            // ... was not successful
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    } else {
      myrx->local_audio = 1;  // NEW device successfully opened
    }
  }
  //
  // Update ModeSettings data base if this is RX1:
  // If local audio is turned off or audio_open_input() failed, do not
  // overwrite the audio name
  //
  if (myid == 0) {
    int mode = vfo[myid].mode;
    RXTXprofile[mode].rx.local_audio = myrx->local_audio;
    if (myrx->local_audio) {
      snprintf(RXTXprofile[mode].rx.audio_name, sizeof(RXTXprofile[mode].rx.audio_name), "%s", myrx->audio_name);
    }
    profiles_copy_rxtxprofile(mode);
  }
}

static void audio_channel_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  switch (val) {
  case 0:
    myrx->audio_channel = STEREO;
    break;
  case 1:
    myrx->audio_channel = LEFT;
    break;
  case 2:
    myrx->audio_channel = RIGHT;
    break;
  }
  if (myid == 0) {
    int mode = vfo[myid].mode;
    RXTXprofile[mode].rx.audio_channel = myrx->audio_channel;
    profiles_copy_rxtxprofile(mode);
  }
}

#ifdef PIPEWIRE
static void pipewire_quantum_cb(GtkWidget *widget, gpointer data) {
  int active = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  const int quantums[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
  int q = quantums[active];

  if (myrx->latency != q) {
    myrx->latency = q;
    rx_save_state(myrx);

    // If audio is running, restart it to apply the new quantum immediately
    if (myrx->local_audio) {
      myrx->local_audio = 0;
      audio_close_output(myrx);
      if (audio_open_output(myrx) < 0) {
        myrx->local_audio = 0;
      } else {
        myrx->local_audio = 1;
      }
    }
  }
}
#endif

void rx_menu(GtkWidget *parent) {
  int i;
  GtkWidget *btn;
  GtkWidget *lbl;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char text[64];
  int rate;
  int maxrate;
  //
  // This guards against changing the active receivere while the menu is open
  //
  myrx = active_receiver;
  myadc = myrx->adc;
  myid = myrx->id;
  snprintf(text, sizeof(text), "piHPSDR - Receive (RX%d VFO-%s)", myid + 1, myid == 0 ? "A" : "B");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), text);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
  int row = 1;
  lbl = gtk_label_new("Sample Rate");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
  btn = gtk_combo_box_text_new();
  if (protocol == SOAPYSDR_PROTOCOL) {
    maxrate = radio->soapy.sample_rate;
  } else if (protocol == ORIGINAL_PROTOCOL) {
    maxrate = 384000;
  } else {
    maxrate = 1536000;
  }
  rate = 48000;
  while (rate <= maxrate) {
    snprintf(text, sizeof(text), "%d", rate);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, text);
    rate = 2 * rate;
  }
  rate = 48000;
  for (i = 0; i < 10; i++) {
    if (rate == myrx->sample_rate) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(btn), i);
      break;
    }
    rate = 2 * rate;
  }
  my_combo_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(sample_rate_cb), NULL);
  row++;
  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    if (filter_board == ALEX && myadc == 0 && have_alex_att) {
      //
      // The "Alex ATT" only exists for ADC1
      //
      lbl = gtk_label_new("Alex Attenuator");
      gtk_widget_set_name(lbl, "boldlabel");
      gtk_widget_set_halign(lbl, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
      btn = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, " 0 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "10 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "20 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "30 dB");
      gtk_combo_box_set_active(GTK_COMBO_BOX(btn), adc[0].alex_attenuation);
      my_combo_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
      g_signal_connect(btn, "changed", G_CALLBACK(alex_att_cb), NULL);
      row++;
    }
    //
    // HPSDR:    If there is more than one ADC, let the user associate an ADC
    //           with the current receiver.
    //
    if (n_adc > 1) {
      lbl = gtk_label_new("Select ADC");
      gtk_widget_set_name(lbl, "boldlabel");
      gtk_widget_set_halign(lbl, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
      btn = gtk_combo_box_text_new();
      for (i = 0; i < n_adc; i++) {
        char label[32];
        snprintf(label, sizeof(label), "ADC-%d", i + 1);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, label);
      }
      gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myadc);
      my_combo_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
      g_signal_connect(btn, "changed", G_CALLBACK(adc_cb), NULL);
      row++;
    }
    if (have_dither) {
      // We assume  Dither/Random are either both available or both not available
      btn = gtk_check_button_new_with_label("Dither");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), adc[myadc].dither);
      gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(dither_cb), NULL);
      btn = gtk_check_button_new_with_label("Random");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), adc[myadc].random);
      gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(random_cb), NULL);
      row++;
    }
    if (have_preamp) {
      btn = gtk_check_button_new_with_label("Preamp");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), adc[myadc].preamp);
      gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(preamp_cb), NULL);
      row++;
    }
  }
  if (row < 4) { row = 4;}
  btn = gtk_check_button_new_with_label("Mute when not active");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->mute_when_not_active);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 2, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(mute_audio_cb), NULL);
  btn = gtk_check_button_new_with_label("Mute Receiver");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->mute_radio);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(mute_radio_cb), NULL);
  row++;
  if (filter_board == ALEX) {
    btn = gtk_check_button_new_with_label("Bypass ADC1 RX filters");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), adc[0].filter_bypass);
    gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 2, 1);
    g_signal_connect(btn, "toggled", G_CALLBACK(adc_filter_bypass_cb), GINT_TO_POINTER(0));
    if (device == DEVICE_ORION2 || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN) {
      btn = gtk_check_button_new_with_label("Bypass ADC2 RX filters");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), adc[1].filter_bypass);
      gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
      g_signal_connect(btn, "toggled", G_CALLBACK(adc_filter_bypass_cb), GINT_TO_POINTER(1));
    }
    row++;
  }
  btn = gtk_check_button_new_with_label("Squelch");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), myrx->squelch_enable);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(squelch_enable_cb), NULL);
  btn = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_increments (GTK_RANGE(btn), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(btn), myrx->squelch);
  gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 3, 1);
  g_signal_connect(G_OBJECT(btn), "value_changed", G_CALLBACK(squelch_value_cb), NULL);
  row++;
  lbl = gtk_label_new("SAM Sideband:");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 1, row, 1, 1);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "Both Sidebands");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "LSB Only");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "USB Only");
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), myrx->sam_sb_mode);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(sam_sb_cb), NULL);
  row++;
  btn = gtk_check_button_new_with_label("FM Volume Limiter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), myrx->fm_limiter);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(fm_limiter_cb), NULL);
  lbl = gtk_label_new("Limiter Gain (dB):");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, 1, row, 1, 1);
  btn = gtk_spin_button_new_with_range(0.0, 30.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), myrx->fm_limiter_gain);
  gtk_grid_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "value-changed", G_CALLBACK(fm_lim_gain_cb), NULL);
  row++;
  //
  // RX Audio options, hard-wired to rows 1-3 in columns
  lbl = gtk_label_new("RX Audio Out");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), lbl, 2, 1, 1, 1);
  output = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(output), NULL, "Only Audio to Radio");
  gtk_combo_box_set_active(GTK_COMBO_BOX(output), 0);
  for (i = 0; i < n_output_devices; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(output), NULL, output_devices[i].description);
    if (myrx->local_audio && strcmp(myrx->audio_name, output_devices[i].name) == 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(output), i + 1);
    }
  }
  if (!myrx->local_audio) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(output), 0);
  }
  my_combo_attach(GTK_GRID(grid), output, 2, 2, 1, 1);
  g_signal_connect(output, "changed", G_CALLBACK(local_output_changed_cb), NULL);
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Stereo");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Left");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Right");
  switch (myrx->audio_channel) {
  case STEREO:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 0);
    break;
  case LEFT:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 1);
    break;
  case RIGHT:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 2);
    break;
  }
  my_combo_attach(GTK_GRID(grid), btn, 2, 3, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(audio_channel_cb), NULL);

#ifdef PIPEWIRE
  lbl = gtk_label_new("PipeWire Quantum:");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl, 1, row, 1, 1);

  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "16 (0.3ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "32 (0.7ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "64 (1.3ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "128 (2.7ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "256 (5.3ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "512 (10.7ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "1024 (21.3ms)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(btn), "2048 (42.7ms)");

  int active_idx = 2; // Default 64
  switch (myrx->latency) {
    case 16: active_idx = 0; break;
    case 32: active_idx = 1; break;
    case 64: active_idx = 2; break;
    case 128: active_idx = 3; break;
    case 256: active_idx = 4; break;
    case 512: active_idx = 5; break;
    case 1024: active_idx = 6; break;
    case 2048: active_idx = 7; break;
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(btn), active_idx);
  my_combo_attach(GTK_GRID(grid), btn, 2, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(pipewire_quantum_cb), NULL);
  row++;
#endif

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
