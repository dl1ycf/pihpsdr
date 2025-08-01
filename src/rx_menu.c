/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include "radio.h"
#include "receiver.h"
#include "rx_menu.h"
#include "sliders.h"

static GtkWidget *dialog = NULL;
static GtkWidget *local_audio_b = NULL;
static GtkWidget *output = NULL;
static RECEIVER *myrx;
static int myadc;
static int myid;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb () {
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
    send_sample_rate(client_socket, myid, samplerate);
    return;
  }

  rx_change_sample_rate(myrx, samplerate);
}

static void adc_cb(GtkToggleButton *widget, gpointer data) {
  myrx->adc = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (radio_is_remote) {
    send_adc(client_socket, myid, myadc);
    return;
  }

  rx_change_adc(myrx);
}

static void local_audio_cb(GtkWidget *widget, gpointer data) {
  t_print("local_audio_cb: rx=%d\n", myid);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
    if (audio_open_output(myrx) == 0) {
      myrx->local_audio = 1;
    } else {
      t_print("local_audio_cb: audio_open_output failed\n");
      myrx->local_audio = 0;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
    }
  } else {
    if (myrx->local_audio) {
      myrx->local_audio = 0;
      audio_close_output(myrx);
    }
  }

  t_print("local_audio_cb: local_audio=%d\n", myrx->local_audio);
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
    send_rxmenu(client_socket, myadc);
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

  if (myrx->local_audio) {
    audio_close_output(myrx);                     // audio_close with OLD device
  }

  if (i >= 0) {
    t_print("local_output_changed rx=%d %s\n", myid, output_devices[i].name);
    snprintf(myrx->audio_name, sizeof(myrx->audio_name), "%s", output_devices[i].name);
  }

  if (myrx->local_audio) {
    if (audio_open_output(myrx) < 0) {           // audio_open with NEW device
      myrx->local_audio = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (local_audio_b), FALSE);
    }
  }

  t_print("local_output_changed rx=%d local_audio=%d\n", myid, myrx->local_audio);
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
}

void rx_menu(GtkWidget *parent) {
  int i;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  //
  // This guards against changing the active receivere while the menu is open
  //
  myrx = active_receiver;
  myadc = myrx->adc;
  myid = myrx->id;
  snprintf(title, sizeof(title), "piHPSDR - Receive (RX%d VFO-%s)", myid + 1, myid == 0 ? "A" : "B");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  int row = 1;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    switch (protocol) {
    case NEW_PROTOCOL: { // Sample rate in RX menu only for P2
      GtkWidget *sample_rate_label = gtk_label_new("Sample Rate");
      gtk_widget_set_name(sample_rate_label, "boldlabel");
      gtk_widget_set_halign(sample_rate_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), sample_rate_label, 0, row, 1, 1);
      GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "48000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "96000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "192000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "384000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "768000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "1536000");

      switch (myrx->sample_rate) {
      case 48000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 0);
        break;

      case 96000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 1);
        break;

      case 192000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 2);
        break;

      case 384000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 3);
        break;

      case 768000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 4);
        break;

      case 1536000:
        gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 5);
        break;
      }

      my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 1, row, 1, 1);
      g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
    }

    row++;
    break;
    }

    if (filter_board == ALEX && myadc == 0 && have_alex_att) {
      //
      // The "Alex ATT" only exists for ADC0
      //
      GtkWidget *alex_att_label = gtk_label_new("Alex Attenuator");
      gtk_widget_set_name(alex_att_label, "boldlabel");
      gtk_widget_set_halign(alex_att_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), alex_att_label, 0, row, 1, 1);
      GtkWidget *alex_att_combo_box = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, " 0 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "10 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "20 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "30 dB");
      gtk_combo_box_set_active(GTK_COMBO_BOX(alex_att_combo_box), adc[0].alex_attenuation);
      my_combo_attach(GTK_GRID(grid), alex_att_combo_box, 1, row, 1, 1);
      g_signal_connect(alex_att_combo_box, "changed", G_CALLBACK(alex_att_cb), NULL);
      row++;
    }

    //
    // HPSDR:    If there is more than one ADC, let the user associate an ADC
    //           with the current receiver.
    // SOAPYSDR: RX1 is hard-wired to ADC0, and RX2 to ADC1, so rx->id and rx->adc are
    //           the same for SOAPY.
    //
    if (n_adc > 1 && protocol != SOAPYSDR_PROTOCOL) {
      GtkWidget *adc_label = gtk_label_new("Select ADC");
      gtk_widget_set_name(adc_label, "boldlabel");
      gtk_widget_set_halign(adc_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), adc_label, 0, row, 1, 1);
      GtkWidget *adc_combo_box = gtk_combo_box_text_new();

      for (i = 0; i < n_adc; i++) {
        char label[32];
        snprintf(label, sizeof(label), "ADC-%d", i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(adc_combo_box), NULL, label);
      }

      gtk_combo_box_set_active(GTK_COMBO_BOX(adc_combo_box), myadc);
      my_combo_attach(GTK_GRID(grid), adc_combo_box, 1, row, 1, 1);
      g_signal_connect(adc_combo_box, "changed", G_CALLBACK(adc_cb), NULL);
      row++;
    }

    if (have_dither) {
      // We assume  Dither/Random are either both available or both not available
      GtkWidget *dither_b = gtk_check_button_new_with_label("Dither");
      gtk_widget_set_name(dither_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dither_b), adc[myadc].dither);
      gtk_grid_attach(GTK_GRID(grid), dither_b, 0, row, 1, 1);
      g_signal_connect(dither_b, "toggled", G_CALLBACK(dither_cb), NULL);
      GtkWidget *random_b = gtk_check_button_new_with_label("Random");
      gtk_widget_set_name(random_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (random_b), adc[myadc].random);
      gtk_grid_attach(GTK_GRID(grid), random_b, 1, row, 1, 1);
      g_signal_connect(random_b, "toggled", G_CALLBACK(random_cb), NULL);
      row++;
    }

    if (have_preamp) {
      GtkWidget *preamp_b = gtk_check_button_new_with_label("Preamp");
      gtk_widget_set_name(preamp_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (preamp_b), adc[myadc].preamp);
      gtk_grid_attach(GTK_GRID(grid), preamp_b, 0, row, 1, 1);
      g_signal_connect(preamp_b, "toggled", G_CALLBACK(preamp_cb), NULL);
      row++;
    }
  }

  if (row < 4) { row = 4;}

  GtkWidget *mute_audio_b = gtk_check_button_new_with_label("Mute when not active");
  gtk_widget_set_name(mute_audio_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute_audio_b), myrx->mute_when_not_active);
  gtk_widget_show(mute_audio_b);
  gtk_grid_attach(GTK_GRID(grid), mute_audio_b, 0, row, 2, 1);
  g_signal_connect(mute_audio_b, "toggled", G_CALLBACK(mute_audio_cb), NULL);

  if (protocol == ORIGINAL_PROTOCOL || protocol  == NEW_PROTOCOL) {
    GtkWidget *mute_radio_b = gtk_check_button_new_with_label("Mute Receiver");
    gtk_widget_set_name(mute_radio_b, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute_radio_b), myrx->mute_radio);
    gtk_widget_show(mute_radio_b);
    gtk_grid_attach(GTK_GRID(grid), mute_radio_b, 2, row, 1, 1);
    g_signal_connect(mute_radio_b, "toggled", G_CALLBACK(mute_radio_cb), NULL);
  }

  row++;

  if (filter_board == ALEX) {
    GtkWidget *adc0_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC0 RX filters");
    gtk_widget_set_name(adc0_filter_bypass_b, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (adc0_filter_bypass_b), adc[0].filter_bypass);
    gtk_grid_attach(GTK_GRID(grid), adc0_filter_bypass_b, 0, row, 2, 1);
    g_signal_connect(adc0_filter_bypass_b, "toggled", G_CALLBACK(adc_filter_bypass_cb), GINT_TO_POINTER(0));

    if (device == DEVICE_ORION2 || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN) {
      GtkWidget *adc1_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC1 RX filters");
      gtk_widget_set_name(adc1_filter_bypass_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (adc1_filter_bypass_b), adc[1].filter_bypass);
      gtk_grid_attach(GTK_GRID(grid), adc1_filter_bypass_b, 2, row, 1, 1);
      g_signal_connect(adc1_filter_bypass_b, "toggled", G_CALLBACK(adc_filter_bypass_cb), GINT_TO_POINTER(1));
    }
  }

  if (n_output_devices > 0) {
    local_audio_b = gtk_check_button_new_with_label("Local Audio Output:");
    gtk_widget_set_name(local_audio_b, "boldlabel");
    gtk_widget_set_halign(local_audio_b, GTK_ALIGN_START);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (local_audio_b), myrx->local_audio);
    gtk_widget_show(local_audio_b);
    gtk_grid_attach(GTK_GRID(grid), local_audio_b, 2, 1, 1, 1);
    g_signal_connect(local_audio_b, "toggled", G_CALLBACK(local_audio_cb), NULL);

    if (myrx->audio_device == -1) { myrx->audio_device = 0; }

    output = gtk_combo_box_text_new();

    for (i = 0; i < n_output_devices; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(output), NULL, output_devices[i].description);

      if (strcmp(myrx->audio_name, output_devices[i].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(output), i);
      }
    }

    i = gtk_combo_box_get_active(GTK_COMBO_BOX(output));

    if (i < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(output), 0);
      snprintf(myrx->audio_name, sizeof(myrx->audio_name), "%s", output_devices[0].name);
    }

    my_combo_attach(GTK_GRID(grid), output, 2, 2, 1, 1);
    g_signal_connect(output, "changed", G_CALLBACK(local_output_changed_cb), NULL);
    GtkWidget *channel = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Stereo");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Left");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Right");

    switch (myrx->audio_channel) {
    case STEREO:
      gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 0);
      break;

    case LEFT:
      gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 1);
      break;

    case RIGHT:
      gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 2);
      break;
    }

    my_combo_attach(GTK_GRID(grid), channel, 2, 3, 1, 1);
    g_signal_connect(channel, "changed", G_CALLBACK(audio_channel_cb), NULL);
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
