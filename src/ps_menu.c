/* Copyright (C)
*  2017 - John Melton, G0ORX/N6LYT
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
static GtkWidget *corr_info_b;
static GtkWidget *status_info_b;
static GtkWidget *feedbk_info_b;
static GtkWidget *chk_info_b;
static GtkWidget *cnt_info_b;
static GtkWidget *get_pk_b;
static GtkWidget *tx_att_spin;

//
// Todo: create buttons to change PS 2.0 values
//

static int running = 0;
static guint info_timer = 0;

#define INFO_SIZE 16

//static GtkWidget *entry[INFO_SIZE];

static void cleanup(void) {
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

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

//
// Restart PS:
// PS reset, wait 100 msec, PS resume
//
static void ps_off_on(void) {
  if (transmitter->puresignal) {
    tx_ps_reset(transmitter);
    usleep(100000);
    tx_ps_resume(transmitter);
  }
}

static void att_spin_cb(GtkWidget *widget, gpointer data) {
  if (transmitter->auto_on) {
    //
    // We come here if the calibration loop adjusts the attenuation and
    // updates the spin button to reflect the new value. Here we supress
    // another send_psatt/schedule_hp in this case
    //
    return;
  }
  transmitter->attenuation = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  if (radio_is_remote) {
    send_psatt(cl_sock_tcp); // this sends auto, attenuation, feedback, and ps antenna
  } else {
    schedule_transmit_specific();
    schedule_high_priority();
  }
}

static void setpk_cb(GtkWidget *widget, gpointer data) {
  transmitter->ps_setpk = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (radio_is_remote) {
    send_psparams(cl_sock_tcp, transmitter);
  } else {
    tx_ps_setparams(transmitter);
    ps_off_on();
  }
}

//
// This is periodically when starting  a
// two-tone experiment. If running PURESIGNAL
// with auto calibration, this thread will
// adjust the TX-ATT value. This thread also
// updates the PS status. If PS is not enabled,
// this is essentially a no-op.
//
// ATTN: This can be called if the menu is not open
//       so we must not access any GUI elements
//
int ps_calibration_timer(gpointer arg) {
  guint *timer = (guint *)arg;
  static int state = -1;
  static int old5  = -1;
  static int old4  = -1;
  if (!transmitter->twotone) {
    state = -1;
    *timer = 0;
    return G_SOURCE_REMOVE;
  }
  if (state < 0) {
    //
    // Start two-tone experiment
    //
    state = 1;          // start with PS reset
    old5 = -1;
  }
  if (transmitter->puresignal) {
    int tx_att_min;
    int tx_att_max;
    if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
      //
      // This range corresponds to +32 ... -12 on the RF slider
      tx_att_min = -13;
      tx_att_max = 31;
    } else {
      tx_att_min = 0;
      tx_att_max = 31;
    }
    tx_ps_getinfo(transmitter);
    //
    // newcal is set to 1 if we have a new calibration or a new feedback value
    // TODO: consider setting newcal to 1 when it was zero upon the last 10
    //       entries into this loop.
    //
    int newcal = 0;
    if (transmitter->psinfo[5] !=  old5) {
      old5 = transmitter->psinfo[5];
      newcal = 1;
    }
    if (transmitter->psinfo[4] > 0 && transmitter->psinfo[4] != old4) {
      old4 = transmitter->psinfo[4];
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
            //
            // If signal is very strong, increase attenuation by 10 dB
            // Note the value is limited to about 300-350 due to ADC clipping/IQ overflow,
            // so the feedback level might be much stronger than indicated here, so advancing
            // the attenuation by 10 also might be needed to protect the RF front-end.
            //
            delta_att = 10;
            //
            // HL2: transmitter "attenuation" can be negative, a value of zero corresponds
            //      to a RF gain of about 19. If we are far in the negative, make a 15dB jump
            //
            if (transmitter->attenuation < -5) { delta_att += 15; }
          } else if (transmitter->psinfo[4] < 25) {
            // If signal is very weak, decrease attenuation by 10 dB
            delta_att = -10;
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
            schedule_high_priority();
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
//
static int info_thread(gpointer arg) {
  if (!running) {
    return G_SOURCE_REMOVE;
  }


  if (transmitter->puresignal) {
    //
    // Put Info/Colour on the buttons
    //
    static int  chkcnt = 0;
    gchar label[20];

    //
    // Get PS info. If the radio is remote, this is transmitted
    // periodically and the data is set be the client thread.
    //
    if (!radio_is_remote) {
      tx_ps_getinfo(transmitter);
      tx_ps_getmx(transmitter);
    }

    if (transmitter->psinfo[14] == 0) {
      gtk_button_set_label(GTK_BUTTON(corr_info_b), "No Corr");
      gtk_widget_set_name(corr_info_b, "redbutton");
    } else {
      gtk_button_set_label(GTK_BUTTON(corr_info_b), "Correcting");
      gtk_widget_set_name(corr_info_b, "greenbutton");
    }

    int fbk = transmitter->psinfo[4];
    snprintf(label, sizeof(label), "%d", fbk);
    gtk_button_set_label(GTK_BUTTON(feedbk_info_b), label);
    if (fbk > 181)  {
      gtk_widget_set_name(feedbk_info_b, "bluebutton");
    } else if (fbk > 128)  {
      gtk_widget_set_name(feedbk_info_b, "greenbutton");
    } else if (fbk > 90)  {
      gtk_widget_set_name(feedbk_info_b, "yellowbutton");
    } else {
      gtk_widget_set_name(feedbk_info_b, "redbutton");
    }

    snprintf(label, sizeof(label), "%d", transmitter->psinfo[5]);
    gtk_button_set_label(GTK_BUTTON(cnt_info_b), label);

    switch (transmitter->psinfo[6]) {
    case 0:
      if (chkcnt > 0) {
        chkcnt--;
      } else {
        gtk_button_set_label(GTK_BUTTON(chk_info_b), "");
        gtk_widget_set_name(chk_info_b, "boldlabel");
      }
      break;
    case 1:
      if (chkcnt == 0) {
        gtk_button_set_label(GTK_BUTTON(chk_info_b), "Fail");
        gtk_widget_set_name(chk_info_b, "orangebutton");
        chkcnt = 10;
      }
      break;
    default:
      gtk_button_set_label(GTK_BUTTON(chk_info_b), "Drive");
      gtk_widget_set_name(chk_info_b, "redbutton");
      chkcnt = 10;
      break;
    }
    switch (transmitter->psinfo[15]) {
    case 0:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Reset");
      break;
    case 1:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Wait");
      break;
    case 2:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "MoxDelay");
      break;
    case 3:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Setup");
      break;
    case 4:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Collect");
      break;
    case 5:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "MoxCheck");
      break;
    case 6:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Calculate");
      break;
    case 7:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "Delay");
      break;
    case 8:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "StayOn");
      break;
    case 9:
      gtk_button_set_label(GTK_BUTTON(status_info_b), "TurnOn");
      break;
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
    snprintf(label, sizeof(label), "%6.3f", transmitter->ps_getmx);
    gtk_button_set_label(GTK_BUTTON(get_pk_b), label);
  } else {
    //
    // Clear all fields/buttons. They will be re-populated
    // if PS is running
    //
    gtk_widget_set_name(corr_info_b, "boldlabel");
    gtk_button_set_label(GTK_BUTTON(corr_info_b), "");
    gtk_widget_set_name(feedbk_info_b, "boldlabel");
    gtk_button_set_label(GTK_BUTTON(feedbk_info_b), "");
    gtk_button_set_label(GTK_BUTTON(cnt_info_b), "");
    gtk_widget_set_name(chk_info_b, "boldlabel");
    gtk_button_set_label(GTK_BUTTON(chk_info_b), "");
    gtk_button_set_label(GTK_BUTTON(status_info_b), "");
    gtk_button_set_label(GTK_BUTTON(get_pk_b), "");
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
    send_psatt(cl_sock_tcp);
  } else {
    schedule_high_priority();
  }
}

static void enable_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
    tx_ps_onoff(transmitter, val);
  }
}

static void oneshot_cb(GtkWidget *widget, gpointer data) {
  transmitter->ps_oneshot = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  if (radio_is_remote) {
    send_psparams(cl_sock_tcp, transmitter);
  } else {
    tx_ps_setparams(transmitter);
    ps_off_on();
  }
}

static void auto_cb(GtkWidget *widget, gpointer data) {
  transmitter->auto_on = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  if (radio_is_remote) {
    send_psatt(cl_sock_tcp);
  }
  if (transmitter->auto_on) {
    gtk_widget_set_sensitive(tx_att_spin, FALSE);
  } else {
    gtk_widget_set_sensitive(tx_att_spin, TRUE);
  }
}

static gboolean resume_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  //
  // Set the attenuation to zero if auto-adjusting and resuming.
  // A very high attenuation value here could lead to no PS calculation
  // done in WDSP, and hence no attenuation adjustment.
  // If not auto-adjusting, do not change attenuation value.
  //
  if (transmitter->puresignal) {
    if (transmitter->twotone && transmitter->auto_on) {
      transmitter->attenuation = 0;
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
      if (radio_is_remote) {
        send_psatt(cl_sock_tcp);
      } else {
        schedule_high_priority();
        schedule_transmit_specific();
      }
    }
    tx_ps_resume(transmitter);
  }
  return FALSE;
}

static void mon_cb(GtkWidget *widget, gpointer data) {
  transmitter->feedback = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (radio_is_remote) {
    send_psatt(cl_sock_tcp);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean reset_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  if (transmitter->puresignal) {
    tx_ps_reset(transmitter);
  }
  return FALSE;
}

static void twotone_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  radio_set_twotone(transmitter, state);
}

void ps_menu(GtkWidget *parent) {
  GtkWidget *btn, *lbl;
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
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), TRUE);
  btn = gtk_button_new_with_label("Close");
  gtk_widget_set_name(btn, "close_button");
  g_signal_connect (btn, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
  btn = gtk_toggle_button_new_with_label("MON");
  gtk_widget_set_name(btn, "small_toggle_button");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->feedback);
  gtk_grid_attach(GTK_GRID(grid), btn, 3, 0, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(mon_cb), NULL);
  int row = 1;
  int col = 0;
  btn = gtk_check_button_new_with_label("Enable PS");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->puresignal);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(enable_cb), NULL);
  col++;
  btn = gtk_toggle_button_new_with_label("Two Tone");
  gtk_widget_set_name(btn, "small_toggle_button");
  gtk_widget_show(btn);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), transmitter->twotone);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(twotone_cb), NULL);
  col++;
  btn = gtk_button_new_with_label("Restart");
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "button-press-event", G_CALLBACK(resume_cb), NULL);
  col++;
  btn = gtk_button_new_with_label("Off");
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "button-press-event", G_CALLBACK(reset_cb), NULL);
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
  lbl = gtk_label_new("FeedBk Ant");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  btn = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Internal");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "Ext1");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(btn), NULL, "ByPass");
  switch (adc[2].antenna) {
  case 0:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 0);
    break;
  case 6:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 1);
    break;
  case 7:
    gtk_combo_box_set_active(GTK_COMBO_BOX(btn), 2);
    break;
  }
  my_combo_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "changed", G_CALLBACK(ps_ant_cb), NULL);
  col++;
  GtkWidget *oneshot_b = gtk_check_button_new_with_label("OneShot");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (oneshot_b), transmitter->ps_oneshot);
  gtk_grid_attach(GTK_GRID(grid), oneshot_b, col, row, 1, 1);
  g_signal_connect(oneshot_b, "toggled", G_CALLBACK(oneshot_cb), NULL);
  col++;
  btn = gtk_check_button_new_with_label("Auto Att.");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), transmitter->auto_on);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "toggled", G_CALLBACK(auto_cb), NULL);

  row++;
  col = 0;

  lbl = gtk_label_new("FeedBack");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  feedbk_info_b = gtk_button_new();
  gtk_widget_set_name(feedbk_info_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), feedbk_info_b, col, row, 1, 1);
  col++;
  lbl = gtk_label_new("Status");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  corr_info_b = gtk_button_new();
  gtk_widget_set_name(corr_info_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), corr_info_b, col, row, 1, 1);
  row++;
  col = 0;
  lbl = gtk_label_new("Count");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  cnt_info_b = gtk_button_new();
  gtk_widget_set_name(cnt_info_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), cnt_info_b, col, row, 1, 1);
  col ++;
  lbl = gtk_label_new("Check");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  chk_info_b = gtk_button_new();
  gtk_widget_set_name(chk_info_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), chk_info_b, col, row, 1, 1);
  row++;
  col = 0;
  lbl = gtk_label_new("State");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  status_info_b = gtk_button_new();
  gtk_widget_set_name(status_info_b, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), status_info_b, col, row, 1, 1);
  col++;
  lbl = gtk_label_new("GetPk");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  get_pk_b = gtk_button_new();
  gtk_grid_attach(GTK_GRID(grid), get_pk_b, col, row, 1, 1);
  gtk_widget_set_name(get_pk_b, "boldlabel");
  row++;
  col = 0;
  lbl = gtk_label_new("Tx Att");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    tx_att_spin = gtk_spin_button_new_with_range(-13.0, 31.0, 1.0);
  } else {
    tx_att_spin = gtk_spin_button_new_with_range(  0.0, 31.0, 1.0);
  }
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_att_spin), (double) transmitter->attenuation);
  g_signal_connect(tx_att_spin, "value-changed", G_CALLBACK(att_spin_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), tx_att_spin, col, row, 1, 1);
  col++;
  lbl = gtk_label_new("SetPk");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), lbl, col, row, 1, 1);
  col++;
  btn = gtk_spin_button_new_with_range(0.001, 0.999, 0.001);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(btn), (double) transmitter->ps_setpk);
  gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
  g_signal_connect(btn, "activate", G_CALLBACK(setpk_cb), NULL);
  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  running = 1;
  info_timer = g_timeout_add((guint) 250, info_thread, NULL);
  gtk_widget_show_all(dialog);
  //
  // If using auto-attenuattion, hide the
  // "manual attenuation" label and spin button
  //
  if (transmitter->auto_on) {
    gtk_widget_set_sensitive(tx_att_spin, FALSE);
  } else {
    gtk_widget_set_sensitive(tx_att_spin, TRUE);
  }
}

