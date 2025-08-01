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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "radio.h"
#include "toolbar.h"
#include "transmitter.h"
#include "vfo.h"

static GtkWidget *dialog = NULL;
static GtkWidget *feedback_l;
static GtkWidget *correcting_l;
static GtkWidget *get_pk;
static GtkWidget *set_pk;
static GtkWidget *tx_att;
static GtkWidget *tx_att_spin;

static char   pk_text[16];

//
// Todo: create buttons to change PS 2.0 values
//

static int running = 0;
static guint info_timer = 0;

#define INFO_SIZE 16

static GtkWidget *entry[INFO_SIZE];

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    //
    // Let PS thread terminate before destroying dialog
    //
    running = 0;

    if (info_timer > 0) {
      g_source_remove(info_timer);
      info_timer = 0;
    }

    usleep(200000);

    if (transmitter->twotone) {
      radio_set_twotone(transmitter, 0);
    }

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

//
// Restart PS:
// PS reset, wait 100 msec, PS resume
//
static void ps_off_on() {
  if (transmitter->puresignal) {
    tx_ps_reset(transmitter);
    usleep(100000);
    tx_ps_resume(transmitter);
  }
}

static void att_spin_cb(GtkWidget *widget, gpointer data) {
  transmitter->attenuation = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_psatt(client_socket); // this sends auto, attenuation, feedback, and ps antenna
  } else {
    schedule_high_priority();
  }
}

static void setpk_cb(GtkWidget *widget, gpointer data) {
  double newpk = -1.0;
  const gchar *text;
  text = gtk_entry_get_text(GTK_ENTRY(widget));
  sscanf(text, "%lf", &newpk);

  if (newpk > 0.01 && newpk < 1.01 && fabs(newpk - transmitter->ps_getpk) > 0.001) {
    transmitter->ps_setpk = newpk;
    transmitter->ps_getpk = newpk;

    if (radio_is_remote) {
      send_psparams(client_socket, transmitter);
    } else {
      tx_ps_setparams(transmitter);
      ps_off_on();
    }
  }

  // Display new value
  snprintf(pk_text, sizeof(pk_text), "%6.3f", transmitter->ps_getpk);
  gtk_entry_set_text(GTK_ENTRY(set_pk), pk_text);
}

static void clear_fields() {
  //
  // This clears most of the text fields and puts
  // the "Feedback" and "Correcting" string in black colour.
  // This will not be re-coloured until a new valid calibration
  // has taken place.
  // This is called when disabling PS, but also when starting a two-tone experiment.
  // In the latter case, the fields stay cleared until the first successful "new"
  // calibration result is obtained.
  //
  if (dialog == NULL) {
    // e.g. doing a two-tone experiment and PS menu is not open
    return;
  }

  gtk_label_set_markup(GTK_LABEL(feedback_l), "<span color='black'>Feedback Lvl</span>");
  gtk_label_set_markup(GTK_LABEL(correcting_l), "<span color='black'>Correcting</span>");

  for (int i = 0; i < INFO_SIZE; i++) {
    if (entry[i] != NULL) {
      gtk_entry_set_text(GTK_ENTRY(entry[i]), "");
    }
  }

  gtk_entry_set_text(GTK_ENTRY(get_pk), "");
  gtk_entry_set_text(GTK_ENTRY(tx_att), "");
}

//
// This is periodically when starting  a
// two-tone experiment. If running PURESIGNAL
// with auto calibration, this thread will
// adjust the TX-ATT value. This thread also
// updates the PS status. If PS is not enabled,
// this is essentially a no-op.
//
int ps_calibration_timer(gpointer arg) {
  guint *timer = (guint *)arg;
  static int state = -1;

  if (!transmitter->twotone) {
    state = -1;
    *timer = 0;
    return G_SOURCE_REMOVE;
  }

  if (state < 0) {
    //
    // Initialised two-tone experiment
    //
    state = 1;          // start with PS reset
    clear_fields();     // clear all data until the next calibration has been done
  }

  if (transmitter->puresignal) {
    int tx_att_min;
    int tx_att_max;
    static int old5 = 0;

    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      tx_att_min = -29;
      tx_att_max = 31;
    } else {
      tx_att_min = 0;
      tx_att_max = 31;
    }

    tx_ps_getinfo(transmitter);
    //
    // newcal is set to 1 if we have a new calibration value
    // (info[5] is the calibration counter)
    //
    int newcal = 0;

    if (transmitter->psinfo[5] !=  old5) {
      old5 = transmitter->psinfo[5];
      newcal = 1;
    }

    if (transmitter->auto_on) {
      switch (state) {
      case 0:

        //
        // A value of 165 means 0.7 dB too strong
        // A value of 140 means 0.7 dB too weak
        // So everything between 140 and 165 is accepted without changing the attenuation
        //
        if (newcal && ((transmitter->psinfo[4] > 165 && transmitter->attenuation < tx_att_max) || (transmitter->psinfo[4] < 140
                       && transmitter->attenuation > tx_att_min))) {
          int delta_att;
          int new_att;

          if (transmitter->psinfo[4] > 275) {
            // If signal is very strong, increase attenuation by 15 dB
            // Note the value is limited to about 300-350 due to ADC clipping/IQ overflow,
            // so the feedback level might be much stronger than indicated here
            delta_att = 15;

            if (transmitter->attenuation < -15) { delta_att += 15; }
          } else if (transmitter->psinfo[4] < 25) {
            // If signal is very weak, decrease attenuation by 15 dB
            delta_att = -15;
          } else {
            // calculate new delta, this mostly succeeds in one step
            delta_att = (int) lround(20.0 * log10((double)transmitter->psinfo[4] / 152.293));
          }

          new_att = transmitter->attenuation + delta_att;

          // keep new value of attenuation in allowed range
          if (new_att < tx_att_min) { new_att = tx_att_min; }

          if (new_att > tx_att_max) { new_att = tx_att_max; }

          // A "PS reset" is only necessary if the attenuation
          // has actually changed. This prevents firing "reset"
          // constantly if the SDR board does not have a TX attenuator
          // (in this case, att will fast reach tx_att_max and stay there if the
          // feedback level is too high).
          // Actually, we first adjust the attenuation (state=0),
          // then do a PS reset (state=1), and then restart PS (state=2).
          if (transmitter->attenuation != new_att) {
            tx_ps_reset(transmitter);
            transmitter->attenuation = new_att;
            schedule_transmit_specific();
            state = 1;
          }
        }

        break;

      case 1:
        // Perform a PS reset and proceed to a PS restart
        state = 2;
        tx_ps_reset(transmitter);
        break;

      case 2:
        // Perform a PS restart and proceed to the calibration loop
        state = 0;
        tx_ps_resume(transmitter);
        break;
      }
    }
  }

  return G_SOURCE_CONTINUE;
}

//
// This is called periodically so it must be a state machine.
// If this thread is activated without the PS menu being
// active, then no menu elements are accessed.
//
static int info_thread(gpointer arg) {
  if (!running) {
    return G_SOURCE_REMOVE;
  }

  if (transmitter->puresignal) {
    gchar label[20];
    static int old5 = 0;  // used to detect an increase of the calibration count
    static int old14 = 0; // used to detect change of "Correcting" status

    if (!radio_is_remote) {
      tx_ps_getinfo(transmitter);
      tx_ps_getmx(transmitter);
    }

    //
    // Set newcal if there is a new calibration
    // Set newcorr if "Correcting" status changed
    //
    int newcal = 0;
    int newcorr = 0;

    if (transmitter->psinfo[5] !=  old5) {
      old5 = transmitter->psinfo[5];
      newcal = 1;
    }

    if (transmitter->psinfo[14] != old14) {
      old14 = transmitter->psinfo[14];
      newcorr = 1;
    }

    if (newcal) {
      if (transmitter->psinfo[4] > 181)  {
        gtk_label_set_markup(GTK_LABEL(feedback_l), "<span color='blue'>Feedback Lvl</span>");
      } else if (transmitter->psinfo[4] > 128)  {
        gtk_label_set_markup(GTK_LABEL(feedback_l), "<span color='green'>Feedback Lvl</span>");
      } else if (transmitter->psinfo[4] > 90)  {
        gtk_label_set_markup(GTK_LABEL(feedback_l), "<span color='yellow'>Feedback Lvl</span>");
      } else {
        gtk_label_set_markup(GTK_LABEL(feedback_l), "<span color='red'>Feedback Lvl</span>");
      }
    }

    if (newcorr) {
      if (transmitter->psinfo[14] == 0) {
        gtk_label_set_markup(GTK_LABEL(correcting_l), "<span color='red'>Correcting</span>");
      } else {
        gtk_label_set_markup(GTK_LABEL(correcting_l), "<span color='green'>Correcting</span>");
      }
    }

    //
    // Print PS status into the text boxes (if they exist)
    //
    for (int i = 0; i < INFO_SIZE; i++) {
      if (entry[i] == NULL) { continue; }

      snprintf(label, sizeof(label), "%d", transmitter->psinfo[i]);

      //
      // Translate PS state variable into human-readable string
      //
      if (i == 15) {
        switch (transmitter->psinfo[15]) {
        case 0:
          snprintf(label, sizeof(label), "Reset");
          break;

        case 1:
          snprintf(label, sizeof(label), "Wait");
          break;

        case 2:
          snprintf(label, sizeof(label), "MoxDelay");
          break;

        case 3:
          snprintf(label, sizeof(label), "Setup");
          break;

        case 4:
          snprintf(label, sizeof(label), "Collect");
          break;

        case 5:
          snprintf(label, sizeof(label), "MoxCheck");
          break;

        case 6:
          snprintf(label, sizeof(label), "Calculate");
          break;

        case 7:
          snprintf(label, sizeof(label), "Delay");
          break;

        case 8:
          snprintf(label, sizeof(label), "StayOn");
          break;

        case 9:
          snprintf(label, sizeof(label), "TurnOn");
          break;
        }
      }

      gtk_entry_set_text(GTK_ENTRY(entry[i]), label);
    }

    snprintf(label, sizeof(label), "%d", transmitter->attenuation);
    gtk_entry_set_text(GTK_ENTRY(tx_att), label);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
    snprintf(label, sizeof(label), "%6.3f", transmitter->ps_getmx);
    gtk_entry_set_text(GTK_ENTRY(get_pk), label);
  }

  return G_SOURCE_CONTINUE;
}

//
// select route for PS feedback signal.
//
static void ps_ant_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    adc[2].antenna = 0;
    break;

  case 1:
    adc[2].antenna = 6;
    break;

  case 2:
    adc[2].antenna = 7;
    break;
  }

  if (radio_is_remote) {
    send_psatt(client_socket);
  } else {
    schedule_high_priority();
  }
}

static void enable_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
    clear_fields();
    tx_ps_onoff(transmitter, val);

    if (val) {
      if ( transmitter->auto_on) {
        char label[16];
        snprintf(label, sizeof(label), "%d", transmitter->attenuation);
        gtk_entry_set_text(GTK_ENTRY(tx_att), label);
        gtk_widget_show(tx_att);
        gtk_widget_hide(tx_att_spin);
      } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
        gtk_widget_show(tx_att_spin);
        gtk_widget_hide(tx_att);
      }
    } else {
      gtk_widget_hide(tx_att_spin);
      gtk_widget_show(tx_att);
      gtk_entry_set_text(GTK_ENTRY(tx_att), "");
    }
  }
}

static void tol_cb(GtkWidget *widget, gpointer data) {
  transmitter->ps_ptol = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (radio_is_remote) {
    send_psparams(client_socket, transmitter);
  } else {
    tx_ps_setparams(transmitter);
    ps_off_on();
  }
}

static void oneshot_cb(GtkWidget *widget, gpointer data) {
  transmitter->ps_oneshot = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (radio_is_remote) {
    send_psparams(client_socket, transmitter);
  } else {
    tx_ps_setparams(transmitter);
    ps_off_on();
  }
}

static void map_cb(GtkWidget *widget, gpointer data) {
  transmitter->ps_map = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (radio_is_remote) {
    send_psparams(client_socket, transmitter);
  } else {
    tx_ps_setparams(transmitter);
    ps_off_on();
  }
}

static void auto_cb(GtkWidget *widget, gpointer data) {
  transmitter->auto_on = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (radio_is_remote) {
    send_psatt(client_socket);
  }

  if (transmitter->puresignal) {
    if (transmitter->auto_on) {
      //
      // automatic attenuation switched on:
      // hide spin-box for manual attenuation
      // show text field for automatic attenuation
      //
      char label[16];
      snprintf(label, sizeof(label), "%d", transmitter->attenuation);
      gtk_entry_set_text(GTK_ENTRY(tx_att), label);
      gtk_widget_show(tx_att);
      gtk_widget_hide(tx_att_spin);
    } else {
      //
      // automatic attenuation switched off:
      // show spin-box for manual attenuation
      // hide text field for automatic attenuation
      // set attenuation to value stored in spin button
      //
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
      gtk_widget_show(tx_att_spin);
      gtk_widget_hide(tx_att);
    }
  } else {
    gtk_widget_show(tx_att);
    gtk_widget_hide(tx_att_spin);
    gtk_entry_set_text(GTK_ENTRY(tx_att), "");
  }
}

static void resume_cb(GtkWidget *widget, gpointer data) {
  //
  // Set the attenuation to zero if auto-adjusting and resuming.
  // A very high attenuation value here could lead to no PS calculation
  // done in WDSP, and hence no attenuation adjustment.
  // If not auto-adjusting, do not change attenuation value.
  //
  if (transmitter->puresignal) {
    if (transmitter->auto_on) {
      transmitter->attenuation = 0;

      if (radio_is_remote) {
        send_psatt(client_socket);
      }
    }

    tx_ps_resume(transmitter);
  }
}

static void feedback_cb(GtkWidget *widget, gpointer data) {
  transmitter->feedback = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (radio_is_remote) {
    send_psatt(client_socket);
  }
}

// cppcheck-suppress constParameterCallback
static void reset_cb(GtkWidget *widget, gpointer data) {
  if (transmitter->puresignal) {
    tx_ps_reset(transmitter);
  }
}

static void twotone_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_twotone(transmitter, state);
}

void ps_menu(GtkWidget *parent) {
  int i;
  char text[16];
  dialog = gtk_dialog_new();
  g_signal_connect (dialog, "destroy", G_CALLBACK(close_cb), NULL);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - Pure Signal");
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  int row = 0;
  int col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  gtk_widget_set_name(close_b, "close_button");
  row++;
  col = 0;
  GtkWidget *enable_b = gtk_check_button_new_with_label("Enable PS");
  gtk_widget_set_name(enable_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_b), transmitter->puresignal);
  gtk_grid_attach(GTK_GRID(grid), enable_b, col, row, 1, 1);
  g_signal_connect(enable_b, "toggled", G_CALLBACK(enable_cb), NULL);
  col++;
  GtkWidget *twotone_b = gtk_toggle_button_new_with_label("Two Tone");
  gtk_widget_set_name(twotone_b, "small_toggle_button");
  gtk_widget_show(twotone_b);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(twotone_b), transmitter->twotone);
  gtk_grid_attach(GTK_GRID(grid), twotone_b, col, row, 1, 1);
  g_signal_connect(twotone_b, "toggled", G_CALLBACK(twotone_cb), NULL);
  col++;
  GtkWidget *auto_b = gtk_check_button_new_with_label("Auto Attenuate");
  gtk_widget_set_name(auto_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (auto_b), transmitter->auto_on);
  gtk_grid_attach(GTK_GRID(grid), auto_b, col, row, 1, 1);
  g_signal_connect(auto_b, "toggled", G_CALLBACK(auto_cb), NULL);
  col++;
  GtkWidget *reset_b = gtk_button_new_with_label("OFF");
  gtk_widget_show(reset_b);
  gtk_grid_attach(GTK_GRID(grid), reset_b, col, row, 1, 1);
  g_signal_connect(reset_b, "button-press-event", G_CALLBACK(reset_cb), NULL);
  col++;
  GtkWidget *resume_b = gtk_button_new_with_label("Restart");
  gtk_grid_attach(GTK_GRID(grid), resume_b, col, row, 1, 1);
  g_signal_connect(resume_b, "button-press-event", G_CALLBACK(resume_cb), NULL);
  col++;
  GtkWidget *feedback_b = gtk_toggle_button_new_with_label("MON");
  gtk_widget_set_name(feedback_b, "small_toggle_button");
  gtk_widget_show(feedback_b);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(feedback_b), transmitter->feedback);
  gtk_grid_attach(GTK_GRID(grid), feedback_b, col, row, 1, 1);
  g_signal_connect(feedback_b, "toggled", G_CALLBACK(feedback_cb), NULL);
  row++;
  col = 0;
  //
  // Selection of feedback path for PureSignal
  //
  // AUTO               Using internal feedback (to ADC0)
  // EXT1               Using EXT1 jacket (to ADC0), ANAN-7000: still uses AUTO
  // BYPASS             Using BYPASS. Not available with ANAN-100/200 up to Rev. 16 filter boards
  //
  // In fact, we provide the possibility of using EXT1 only to support these older
  // (before February, 2015) ANAN-100/200 devices.
  //
  GtkWidget *ps_ant_label = gtk_label_new("PS FeedBk ANT:");
  gtk_widget_set_name(ps_ant_label, "boldlabel");
  gtk_widget_show(ps_ant_label);
  gtk_grid_attach(GTK_GRID(grid), ps_ant_label, col, row, 1, 1);
  col++;
  GtkWidget *ps_ant_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ps_ant_combo), NULL, "Internal");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ps_ant_combo), NULL, "Ext1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ps_ant_combo), NULL, "ByPass");

  switch (adc[2].antenna) {
  case 0:
    gtk_combo_box_set_active(GTK_COMBO_BOX(ps_ant_combo), 0);
    break;

  case 6:
    gtk_combo_box_set_active(GTK_COMBO_BOX(ps_ant_combo), 1);
    break;

  case 7:
    gtk_combo_box_set_active(GTK_COMBO_BOX(ps_ant_combo), 2);
    break;
  }

  my_combo_attach(GTK_GRID(grid), ps_ant_combo, col, row, 1, 1);
  g_signal_connect(ps_ant_combo, "changed", G_CALLBACK(ps_ant_cb), NULL);
  col++;
  GtkWidget *map_b = gtk_check_button_new_with_label("PS MAP");
  gtk_widget_set_name(map_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (map_b), transmitter->ps_map);
  gtk_grid_attach(GTK_GRID(grid), map_b, col, row, 1, 1);
  g_signal_connect(map_b, "toggled", G_CALLBACK(map_cb), NULL);
  col++;
  GtkWidget *tol_b = gtk_check_button_new_with_label("PS Relax Tolerance");
  gtk_widget_set_name(tol_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tol_b), transmitter->ps_ptol);
  gtk_grid_attach(GTK_GRID(grid), tol_b, col, row, 2, 1);
  g_signal_connect(tol_b, "toggled", G_CALLBACK(tol_cb), NULL);
  col++;
  col++;
  GtkWidget *oneshot_b = gtk_check_button_new_with_label("OneShot");
  gtk_widget_set_name(oneshot_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (oneshot_b), transmitter->ps_oneshot);
  gtk_grid_attach(GTK_GRID(grid), oneshot_b, col, row, 1, 1);
  g_signal_connect(oneshot_b, "toggled", G_CALLBACK(oneshot_cb), NULL);
  row++;
  col = 0;
  feedback_l = gtk_label_new("Feedback Lvl");
  gtk_widget_set_name(feedback_l, "boldlabel");
  gtk_widget_show(feedback_l);
  gtk_grid_attach(GTK_GRID(grid), feedback_l, col, row, 1, 1);
  col++;
  correcting_l = gtk_label_new("Correcting");
  gtk_widget_set_name(correcting_l, "boldlabel");
  gtk_widget_show(correcting_l);
  gtk_grid_attach(GTK_GRID(grid), correcting_l, col, row, 1, 1);
  row++;
  col = 0;

  for (i = 0; i < INFO_SIZE; i++) {
    int display = 1;

    switch (i) {
    case 4:
      snprintf(text, sizeof(text), "feedbk");
      break;

    case 5:
      snprintf(text, sizeof(text), "cor.cnt");
      break;

    case 6:
      snprintf(text, sizeof(text), "sln.chk");
      break;

    case 13:
      snprintf(text, sizeof(text), "db.cnt");
      break;

    case 15:
      snprintf(text, sizeof(text), "status");
      break;

    default:
      display = 0;
      break;
    }

    if (display) {
      GtkWidget *lbl = gtk_label_new(text);
      gtk_widget_set_name(lbl, "boldlabel");
      entry[i] = gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(entry[i]), 10);
      gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
      col++;
      gtk_grid_attach(GTK_GRID(grid), entry[i], col, row, 1, 1);
      gtk_entry_set_width_chars(GTK_ENTRY(entry[i]), 10);
      col++;

      if (col >= 6) {
        row++;
        col = 0;
      }
    } else {
      entry[i] = NULL;
    }
  }

  row++;
  col = 0;
  GtkWidget *lbl = gtk_label_new("GetPk");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  get_pk = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), get_pk, col, row, 1, 1);
  gtk_entry_set_width_chars(GTK_ENTRY(get_pk), 10);
  col++;
  lbl = gtk_label_new("SetPk");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;

  if (!radio_is_remote) {
    tx_ps_getpk(transmitter);
  }

  snprintf(pk_text, sizeof(pk_text), "%6.3f", transmitter->ps_getpk);
  set_pk = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(set_pk), pk_text);
  gtk_grid_attach(GTK_GRID(grid), set_pk, col, row, 1, 1);
  gtk_entry_set_width_chars(GTK_ENTRY(set_pk), 10);
  g_signal_connect(set_pk, "activate", G_CALLBACK(setpk_cb), NULL);
  col++;
  lbl = gtk_label_new("TX ATT");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  tx_att = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), tx_att, col, row, 1, 1);
  snprintf(text, sizeof(text), "%d", transmitter->attenuation);
  gtk_entry_set_text(GTK_ENTRY(tx_att), text);
  gtk_entry_set_width_chars(GTK_ENTRY(tx_att), 10);

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    tx_att_spin = gtk_spin_button_new_with_range(-29.0, 31.0, 1.0);
  } else {
    tx_att_spin = gtk_spin_button_new_with_range(  0.0, 31.0, 1.0);
  }

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
  gtk_grid_attach(GTK_GRID(grid), tx_att_spin, col, row, 1, 1);
  g_signal_connect(tx_att_spin, "value-changed", G_CALLBACK(att_spin_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  running = 1;
  info_timer = g_timeout_add((guint) 250, info_thread, NULL);
  gtk_widget_show_all(dialog);

  //
  // If using auto-attenuattion, hide the
  // "manual attenuation" label and spin button
  //
  if (transmitter->puresignal) {
    if (transmitter->auto_on) {
      gtk_widget_hide(tx_att_spin);
      gtk_widget_show(tx_att);
    } else {
      gtk_widget_hide(tx_att);
      gtk_widget_show(tx_att_spin);
    }
  } else {
    gtk_widget_hide(tx_att_spin);
    gtk_widget_show(tx_att);
    gtk_entry_set_text(GTK_ENTRY(tx_att), "");
  }
}

