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
#include <stdlib.h>
#include <math.h>

#include "actions.h"
#include "agc.h"
#include "appearance.h"
#include "band.h"
#include "bandstack.h"
#include "channel.h"
#include "client_server.h"
#include "discovered.h"
#include "ext.h"
#include "filter.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "new_protocol.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "sliders.h"
#include "transmitter.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "vfo.h"

static int width;
static int height;

static GtkWidget *sliders;

static guint scale_timer;
static enum ACTION scale_status = NO_ACTION;
static GtkWidget *scale_dialog;

static GtkWidget *af_gain_scale = NULL;
static gulong af_signal_id = 0;

static GtkWidget *rf_gain_label = NULL;
static GtkWidget *rf_gain_scale = NULL;
static gulong rf_signal_id = 0;

static GtkWidget *agc_scale;
static gulong agc_signal_id = 0;

static GtkWidget *attenuation_label = NULL;
static GtkWidget *attenuation_scale = NULL;
static gulong att_signal_id = 0;

static GtkWidget *c25_container = NULL;
static GtkWidget *c25_combobox = NULL;
static GtkWidget *c25_label = NULL;
static gulong c25_signal_id = 0;

static GtkWidget *mic_gain_scale;
static gulong mic_signal_id = 0;

static GtkWidget *drive_scale;
static gulong drive_signal_id = 0;

static GtkWidget *squelch_scale;
static gulong squelch_signal_id = 0;
static GtkWidget *squelch_enable;
static gulong squelch_enable_signal_id = 0;

//
// call-back functions. They simply call utility functions radio_*()
//

static void attenuation_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(attenuation_scale));
  radio_set_attenuation(active_receiver->id, value);
}

static void agcgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(agc_scale));
  radio_set_agc_gain(active_receiver->id, value);
}

static void afgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(af_gain_scale));
  radio_set_af_gain(active_receiver->id, value);
}

static void rf_gain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(rf_gain_scale));
  radio_set_rf_gain(active_receiver->id, value);
}

static void micgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_mic_gain(value);
}

static void drive_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(drive_scale));
  radio_set_drive(value);
}

static void squelch_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_squelch(active_receiver->id, value);
}

static void squelch_enable_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  radio_set_squelch_enable(active_receiver->id, val);
}

static void c25_att_cb(GtkWidget *widget, gpointer data) {
  int val = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget)));
  radio_set_c25_att(active_receiver->id, val);
}

//
// general tool for displaying a pop-up slider. This can also be used for a value for which there
// is no GTK slider. Make the slider "insensitive" so one cannot operate on it.
// Putting this into a separate function avoids much code repetition.
//

static int scale_timeout_cb(gpointer data) {
  gtk_widget_destroy(scale_dialog);
  scale_status = NO_ACTION;
  return FALSE;
}

void show_popup_slider(enum ACTION action, int rx, double min, double max, double delta, double value,
                       const char *title) {
  //
  // general function for displaying a pop-up slider. This can also be used for a value for which there
  // is no GTK slider. Make the slider "insensitive" so one cannot operate on it.
  // Putting this into a separate function avoids much code repetition.
  //
  static GtkWidget *popup_scale = NULL;
  static int scale_rx;
  static double scale_min;
  static double scale_max;
  static double scale_wid;

  if (suppress_popup_sliders) {
    return;
  }

  //
  // a) if there is still a pop-up slider on the screen for a different action, destroy it
  //
  if (scale_status != action || scale_rx != rx) {
    if (scale_status != NO_ACTION) {
      g_source_remove(scale_timer);
      gtk_widget_destroy(scale_dialog);
      scale_status = NO_ACTION;
    }
  }

  if (scale_status == NO_ACTION) {
    //
    // b) if a pop-up slider for THIS action is not on display, create one
    //    (only in this case input parameters min and max will be used)
    //
    scale_status = action;
    scale_rx = rx;
    scale_min = min;
    scale_max = max;
    scale_wid = max - min;
    scale_dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(top_window), GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(scale_dialog));
    popup_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, delta);
    gtk_widget_set_name(popup_scale, "popup_scale");
    gtk_widget_set_size_request (popup_scale, 400, 30);
    gtk_range_set_value (GTK_RANGE(popup_scale), value),
                        gtk_widget_show(popup_scale);
    gtk_widget_set_sensitive(popup_scale, FALSE);
    gtk_container_add(GTK_CONTAINER(content), popup_scale);
    scale_timer = g_timeout_add(2000, scale_timeout_cb, NULL);
    gtk_widget_show_all(scale_dialog);
  } else {
    //
    // c) if a pop-up slider for THIS action is still on display, adjust value and reset timeout
    //
    g_source_remove(scale_timer);

    if (value > scale_min + 1.01 * scale_wid) {
      scale_min = scale_min + 0.5 * scale_wid;
      scale_max = scale_max + 0.5 * scale_wid;
      gtk_range_set_range(GTK_RANGE(popup_scale), scale_min, scale_max);
    }

    if (value < scale_max - 1.01 * scale_wid) {
      scale_min = scale_min - 0.5 * scale_wid;
      scale_max = scale_max - 0.5 * scale_wid;
      gtk_range_set_range(GTK_RANGE(popup_scale), scale_min, scale_max);
    }

    gtk_range_set_value (GTK_RANGE(popup_scale), value),
                        scale_timer = g_timeout_add(2000, scale_timeout_cb, NULL);
  }
}

int sliders_att_type_changed(gpointer data) {
  //
  // Prepeare sliders to reflect whether we have a CHARLY25 or not
  // This is invoked via the GTK queue
  //
  suppress_popup_sliders = 1;
  if (filter_board == CHARLY25) {
    //
    // The filter board is now CHARLY25, but may have been before as well
    //
    if (attenuation_label != NULL) { gtk_widget_hide(attenuation_label); }

    if (rf_gain_label != NULL) { gtk_widget_hide(rf_gain_label); }

    //
    // Set gain/attenuation depending on the CHARLY25 settings
    //
    radio_set_attenuation(0, 0);
    radio_set_attenuation(1, 0);
    radio_set_rf_gain(0, rx_gain_calibration);
    radio_set_rf_gain(1, rx_gain_calibration);

    if (adc[0].preamp || adc[0].dither) { adc[0].alex_attenuation = 0; }

    if (attenuation_scale != NULL) { gtk_widget_hide(attenuation_scale); }

    if (rf_gain_scale != NULL) { gtk_widget_hide(rf_gain_scale); }

    if (c25_container != NULL) { gtk_widget_show(c25_container); }

    if (c25_label != NULL) { gtk_widget_show(c25_label); }

    sliders_c25_att(active_receiver->id);
  } else {
    adc[0].preamp = 0;
    adc[0].dither = 0;
    adc[0].alex_attenuation = 0;
    radio_set_attenuation(0, 0);
    radio_set_attenuation(1, 0);
    radio_set_rf_gain(0, rx_gain_calibration);
    radio_set_rf_gain(1, rx_gain_calibration);

    if (attenuation_label != NULL) { gtk_widget_show(attenuation_label); }

    if (rf_gain_label != NULL) { gtk_widget_show(rf_gain_label); }

    if (attenuation_scale != NULL) { gtk_widget_show(attenuation_scale); }

    if (rf_gain_scale != NULL) { gtk_widget_show(rf_gain_scale); }

    if (c25_container != NULL) { gtk_widget_hide(c25_container); }

    if (c25_label != NULL) { gtk_widget_hide(c25_label); }

    sliders_attenuation(active_receiver->id);
    sliders_rf_gain(active_receiver->id, active_receiver->adc);
  }
  suppress_popup_sliders = 0;
  return FALSE;
}

int sliders_active_receiver_changed(void *data) {
  suppress_popup_sliders = 1;
  if (display_sliders) {
    //
    // Change sliders and check-boxes to reflect the state of the
    // new active receiver
    //
    int id = active_receiver->id;
    int rxadc = active_receiver->adc;
    sliders_af_gain(id);
    sliders_rf_gain(id, rxadc);
    sliders_agc_gain(id);
    sliders_squelch(id);
    sliders_c25_att(id);
    sliders_attenuation(id);
  }
  suppress_popup_sliders = 0;
  return FALSE;
}

//
// The following functions, sliders_*(), "do" nothing but simply
// change sliders/buttons to reflect the current radio status.
// Usually sliders are "blocked" while their value is changed
// to prevent them to emit a signal.
//

void sliders_c25_att(int id) {
  if (id > receivers) { return; }
  if (filter_board != CHARLY25) { return; }
  //
  // Only change the combo-box (no popup slider)
  //
  if (display_sliders && active_receiver->id == id && c25_combobox != NULL) {
    char lbl[16];
    int rxadc = active_receiver->adc;
    int att = -12 * adc[rxadc].alex_attenuation + 18 * (adc[rxadc].dither + adc[rxadc].preamp);
    snprintf(lbl, sizeof(lbl), "%d", att);
    if (c25_signal_id) { g_signal_handler_block(G_OBJECT(c25_combobox), c25_signal_id); }
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c25_combobox), lbl);
    if (c25_signal_id) { g_signal_handler_unblock(G_OBJECT(c25_combobox), c25_signal_id); }
  }
}

void sliders_attenuation(int id) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  if (display_sliders && active_receiver->id == id && attenuation_scale != 0) {
    if (att_signal_id) { g_signal_handler_block(G_OBJECT(attenuation_scale), att_signal_id); }
    gtk_range_set_value (GTK_RANGE(attenuation_scale), (double)adc[id].attenuation);
    if (att_signal_id) { g_signal_handler_unblock(G_OBJECT(attenuation_scale), att_signal_id); }
  } else {
    char title[64];
    snprintf(title, sizeof(title), "Attenuation - ADC-%d (dB)", id);
    show_popup_slider(ATTENUATION, id, 0.0, 31.0, 1.0, (double)adc[id].attenuation,
                      title);
  }
}

void sliders_agc_gain(int id) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  if (display_sliders && active_receiver->id == id && agc_scale != NULL) {
    if (agc_signal_id) { g_signal_handler_block(G_OBJECT(agc_scale), agc_signal_id); }
    gtk_range_set_value (GTK_RANGE(agc_scale), receiver[id]->agc_gain);
    if (agc_signal_id) { g_signal_handler_unblock(G_OBJECT(agc_scale), agc_signal_id); }
  } else {
    char title[64];
    snprintf(title, sizeof(title), "AGC Gain RX%d", id + 1);
    show_popup_slider(AGC_GAIN, id, -20.0, 120.0, 1.0, receiver[id]->agc_gain, title);
  }
}

void sliders_af_gain(int id) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  const RECEIVER *rx = receiver[id];
  if (display_sliders && id == active_receiver->id && af_gain_scale != NULL) {
    if (af_signal_id) { g_signal_handler_block(G_OBJECT(af_gain_scale), af_signal_id); }
    gtk_range_set_value (GTK_RANGE(af_gain_scale), rx->volume);
    if (af_signal_id) { g_signal_handler_unblock(G_OBJECT(af_gain_scale), af_signal_id); }
  } else {
    char title[64];
    snprintf(title, sizeof(title), "AF Gain RX%d", id + 1);
    show_popup_slider(AF_GAIN, id, -40.0, 0.0, 1.0, rx->volume, title);
  }
}

void sliders_rf_gain(int id, int rxadc) {
  if (id > receivers) { return; }
  if (rf_gain_scale == NULL) { return; }
  //
  // This ONLY moves the slider
  //
  if (display_sliders && active_receiver->id == id) {
    if (rf_signal_id) { g_signal_handler_block(G_OBJECT(rf_gain_scale), rf_signal_id); }
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[rxadc].gain);
    if (rf_signal_id) { g_signal_handler_unblock(G_OBJECT(rf_gain_scale), rf_signal_id); }
  } else {
    char title[64];
    snprintf(title, sizeof(title), "RF Gain ADC %d", rxadc);
    show_popup_slider(RF_GAIN, rxadc, adc[rxadc].min_gain, adc[rxadc].max_gain, 1.0, adc[rxadc].gain, title);
  }
}

void sliders_filter_width(int id, int width) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  char title[64];
  int min, max;
  snprintf(title, sizeof(title), "Filter Width RX%d (Hz)", id + 1);
  min = 0;
  max = 2 * width;

  if (max < 200) { max = 200; }

  if (width > 1000) {
    max = width + 1000;
    min = width - 1000;
  }

  if (width > 3000) {
    max = width + 2000;
    min = width - 2000;
  }

  show_popup_slider(IF_WIDTH, id, (double)(min), (double)(max), 1.0, (double) width, title);
}

void sliders_filter_shift(int id, int shift) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  char title[64];
  int min, max;
  snprintf(title, sizeof(title), "Filter SHIFT RX%d (Hz)", id + 1);
  min = shift - 500;
  max = shift + 500;
  show_popup_slider(IF_SHIFT, id, (double)(min), (double) (max), 1.0, (double) shift, title);
}

void sliders_linein_gain() {
  //
  // This ONLY moves the slider
  //
  show_popup_slider(LINEIN_GAIN, 0, -34.0, 12.0, 1.0, linein_gain, "LineIn Gain");
}

void sliders_mic_gain() {
  //
  // This ONLY moves the slider
  //
  if (can_transmit) {
    if (display_sliders ) {
      if (mic_signal_id) { g_signal_handler_block(G_OBJECT(mic_gain_scale), mic_signal_id); }
      gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);
      if (mic_signal_id) { g_signal_handler_unblock(G_OBJECT(mic_gain_scale), mic_signal_id); }
    } else {
      show_popup_slider(MIC_GAIN, 0, -12.0, 50.0, 1.0, transmitter->mic_gain, "Mic Gain");
    }
  }
}

void sliders_drive(void) {
  t_print("%s\n", __FUNCTION__);
  //
  // This ONLY moves the slider
  //
  if (can_transmit) {
    if (display_sliders) {
      if (drive_signal_id) { g_signal_handler_block(G_OBJECT(drive_scale), drive_signal_id); }
      gtk_range_set_value (GTK_RANGE(drive_scale), (double) transmitter->drive);
      if (drive_signal_id) { g_signal_handler_unblock(G_OBJECT(drive_scale), drive_signal_id); }
    } else {
    show_popup_slider(DRIVE, 0, drive_min, drive_max, 1.0, (double) transmitter->drive, "TX Drive");
    }
  }
}

void sliders_filter_high(int id, int var) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  char title[64];
  int min, max;
  snprintf(title, sizeof(title), "Filter Cut High RX%d (Hz)", id + 1);
  //
  // The hi-cut is always non-negative
  //
  min = 0;
  max = 2 * var;

  if (max <  200) { max = 200; }

  if (var > 1000) {
    max = var + 1000;
    min = var - 1000;
  }

  show_popup_slider(FILTER_CUT_HIGH, id, (double)(min), (double)(max), 1.00, (double) var, title);
}

void sliders_filter_low(int id, int var) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider
  //
  char title[64];
  int min, max;
  snprintf(title, sizeof(title), "Filter Cut Low RX%d (Hz)", id + 1);

  //
  // The low-cut is either always positive, or always negative for a given mode
  //
  if (var > 0) {
    min = 0;
    max = 2 * var;

    if (max <  200) { max = 200; }

    if (var > 1000) {
      max = var + 1000;
      min = var - 1000;
    }
  } else {
    max = 0;
    min = 2 * var;

    if (min >  -200) { min = -200; }

    if (var < -1000) {
      max = var + 1000;
      min = var - 1000;
    }
  }

  show_popup_slider(FILTER_CUT_LOW, id, (double)(min), (double)(max), 1.00, (double) var, title);
}

void sliders_squelch(int id) {
  if (id > receivers) { return; }
  //
  // This ONLY moves the slider and updates the checkbutton
  //
  RECEIVER *rx = receiver[id];

  if (display_sliders && id == active_receiver->id) {
    if (squelch_signal_id) { g_signal_handler_block(G_OBJECT(squelch_scale), squelch_signal_id); }
    if (squelch_enable_signal_id) { g_signal_handler_block(G_OBJECT(squelch_enable), squelch_enable_signal_id); }
    gtk_range_set_value (GTK_RANGE(squelch_scale), rx->squelch);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), rx->squelch_enable);
    if (squelch_enable_signal_id) { g_signal_handler_unblock(G_OBJECT(squelch_enable), squelch_enable_signal_id); }
    if (squelch_signal_id) { g_signal_handler_unblock(G_OBJECT(squelch_scale), squelch_signal_id); }
  } else {
    char title[64];
    snprintf(title, sizeof(title), "Squelch RX%d (Hz)", id + 1);
    show_popup_slider(SQUELCH, id, 0.0, 100.0, 1.0, rx->squelch, title);
  }
}

void sliders_diversity_gain() {
  //
  // This ONLY moves the slider and updates the checkbutton
  //
  show_popup_slider(DIV_GAIN, 0, -27.0, 27.0, 0.01, div_gain, "Diversity Gain");
}

void sliders_diversity_phase() {
  //
  // This ONLY moves the slider and updates the checkbutton
  //
  show_popup_slider(DIV_PHASE, 0, -180.0, 180.0, 0.1, div_phase, "Diversity Phase");
}

GtkWidget *sliders_init(int my_width, int my_height) {
  GtkWidget *label;
  width = my_width;
  height = my_height;
  t_print("sliders_init: width=%d height=%d\n", width, height);
  //
  // The larger the width, the smaller the fraction used for the label can be
  // font size.
  //
  int twidth, swidth, tpix;
  int t1pos, t2pos, t3pos;
  int s1pos, s2pos, s3pos, sqpos;
  const char *csslabel;

  if (width < 1024) {
    // label  width: 1/9 of screen width
    // slider width: 2/9 of screen width
    tpix   =  width / 9;      // width of text label in pixel
    twidth =  3;              // width of text label in grid units
    swidth =  6;              // width of slider in grid units
  } else if (width < 1280) {
    // label  width: 1/12 of screen width
    // slider width: 3/12 of screen width
    tpix   =  width / 12;
    twidth =  3;              // width of text label in pixel
    swidth =  9;              // width of slider in grid units
  } else {
    // label  width: 1/15 of screen width
    // slider width: 4/12 of screen width
    tpix   =  width / 15;
    twidth =  2;              // width of text label in pixel
    swidth =  8;              // width of slider in grid units
  }

  //
  // Depending on the width for the Label, we can increase the
  // font size. Note the minimum value for tpix is 71
  // (for a 640-pix-screen)
  //
  if (tpix < 75 ) {
    csslabel = "slider1";
  } else if (tpix < 85) {
    csslabel = "slider2";
  } else if (tpix < 100) {
    csslabel = "slider3";
  } else {
    csslabel = "slider4";
  }

  t1pos  =  0;
  s1pos  =  t1pos + twidth;
  t2pos  =  s1pos + swidth;
  s2pos  =  t2pos + twidth;
  t3pos  =  s2pos + swidth;
  s3pos  =  t3pos + twidth;
  sqpos  =  s3pos + 1;
  sliders = gtk_grid_new();
  gtk_widget_set_size_request (sliders, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(sliders), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(sliders), TRUE);
  label = gtk_label_new("AF");
  gtk_widget_set_name(label, csslabel);
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_show(label);
  gtk_grid_attach(GTK_GRID(sliders), label, t1pos, 0, twidth, 1);
  af_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 0.0, 1.00);
  gtk_widget_set_size_request(af_gain_scale, 0, height / 2);
  gtk_widget_set_valign(af_gain_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(af_gain_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(af_gain_scale), active_receiver->volume);
  gtk_widget_show(af_gain_scale);
  gtk_grid_attach(GTK_GRID(sliders), af_gain_scale, s1pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(af_gain_scale), "value_changed", G_CALLBACK(afgain_value_changed_cb), NULL);
  label = gtk_label_new("AGC");
  gtk_widget_set_name(label, csslabel);
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_show(label);
  gtk_grid_attach(GTK_GRID(sliders), label, t2pos, 0, twidth, 1);
  agc_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
  gtk_widget_set_size_request(agc_scale, 0, height / 2);
  gtk_widget_set_valign(agc_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(agc_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_scale), active_receiver->agc_gain);
  gtk_widget_show(agc_scale);
  gtk_grid_attach(GTK_GRID(sliders), agc_scale, s2pos, 0, swidth, 1);
  g_signal_connect(G_OBJECT(agc_scale), "value_changed", G_CALLBACK(agcgain_value_changed_cb), NULL);

  if (have_rx_gain) {
    rf_gain_label = gtk_label_new("RF");
    gtk_widget_set_name(rf_gain_label, csslabel);
    gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_END);
    gtk_widget_show(rf_gain_label);
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_label, t3pos, 0, twidth, 1);
    rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, adc[0].min_gain, adc[0].max_gain, 1.0);
    gtk_widget_set_size_request(rf_gain_scale, 0, height / 2);
    gtk_widget_set_valign(rf_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
    gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);
    gtk_widget_show(rf_gain_scale);
    gtk_grid_attach(GTK_GRID(sliders), rf_gain_scale, s3pos, 0, swidth, 1);
    g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed", G_CALLBACK(rf_gain_value_changed_cb), NULL);
  } else {
    rf_gain_label = NULL;
    rf_gain_scale = NULL;
  }

  if (have_rx_att) {
    attenuation_label = gtk_label_new("ATT");
    gtk_widget_set_name(attenuation_label, csslabel);
    gtk_widget_set_halign(attenuation_label, GTK_ALIGN_END);
    gtk_widget_show(attenuation_label);
    gtk_grid_attach(GTK_GRID(sliders), attenuation_label, t3pos, 0, twidth, 1);
    attenuation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
    gtk_widget_set_size_request(attenuation_scale, 0, height / 2);
    gtk_widget_set_valign(attenuation_scale, GTK_ALIGN_CENTER);
    gtk_range_set_value (GTK_RANGE(attenuation_scale), adc[active_receiver->adc].attenuation);
    gtk_range_set_increments (GTK_RANGE(attenuation_scale), 1.0, 1.0);
    gtk_widget_show(attenuation_scale);
    gtk_grid_attach(GTK_GRID(sliders), attenuation_scale, s3pos, 0, swidth, 1);
    g_signal_connect(G_OBJECT(attenuation_scale), "value_changed", G_CALLBACK(attenuation_value_changed_cb), NULL);
  } else {
    attenuation_label = NULL;
    attenuation_scale = NULL;
  }

  //
  // These handles need to be created because they are activated/deactivaded
  // depending on selecting/deselcting the CHARLY25 filter board
  // Because "touch-screen friendly" comboboxes cannot be shown/hidden properly,
  // we put this into a container
  //
  c25_label = gtk_label_new("Att/Pre");
  gtk_widget_set_name(c25_label, csslabel);
  gtk_widget_set_halign(c25_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(sliders), c25_label, t3pos, 0, twidth, 1);
  c25_container = gtk_fixed_new();
  gtk_grid_attach(GTK_GRID(sliders), c25_container, s3pos, 0, swidth, 1);
  GtkWidget *c25_grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(c25_grid), TRUE);
  //
  // One could achieve a finer granulation by combining attenuators and preamps,
  // but it seems sufficient to either engage attenuators or preamps
  //
  c25_combobox = gtk_combo_box_text_new();
  gtk_widget_set_name(c25_combobox, csslabel);
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "-36", "-36 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "-24", "-24 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "-12", "-12 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "0",   "  0 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "18",  "+18 dB");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c25_combobox), "36",  "+36 dB");
  my_combo_attach(GTK_GRID(c25_grid), c25_combobox, 0, 0, 2, 1);
  c25_signal_id = g_signal_connect(G_OBJECT(c25_combobox), "changed", G_CALLBACK(c25_att_cb), NULL);
  gtk_container_add(GTK_CONTAINER(c25_container), c25_grid);

  if (can_transmit) {
    label = gtk_label_new("Mic");
    gtk_widget_set_name(label, csslabel);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(sliders), label, t1pos, 1, twidth, 1);
    mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
    gtk_widget_set_size_request(mic_gain_scale, 0, height / 2);
    gtk_widget_set_valign(mic_gain_scale, GTK_ALIGN_CENTER);
    gtk_range_set_increments (GTK_RANGE(mic_gain_scale), 1.0, 1.0);
    gtk_grid_attach(GTK_GRID(sliders), mic_gain_scale, s1pos, 1, swidth, 1);
    gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);
    g_signal_connect(G_OBJECT(mic_gain_scale), "value_changed", G_CALLBACK(micgain_value_changed_cb), NULL);
    label = gtk_label_new("TX Drv");
    gtk_widget_set_name(label, csslabel);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(sliders), label, t2pos, 1, twidth, 1);
    drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, drive_min, drive_max, 1.00);
    gtk_widget_set_size_request(drive_scale, 0, height / 2);
    gtk_widget_set_valign(drive_scale, GTK_ALIGN_CENTER);
    gtk_range_set_increments (GTK_RANGE(drive_scale), 1.0, 1.0);
    gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive());
    gtk_widget_show(drive_scale);
    gtk_grid_attach(GTK_GRID(sliders), drive_scale, s2pos, 1, swidth, 1);
    drive_signal_id = g_signal_connect(G_OBJECT(drive_scale), "value_changed", G_CALLBACK(drive_value_changed_cb), NULL);
  }

  label = gtk_label_new("Sqlch");
  gtk_widget_set_name(label, csslabel);
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_widget_show(label);
  gtk_grid_attach(GTK_GRID(sliders), label, t3pos, 1, twidth, 1);
  squelch_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_widget_set_size_request(squelch_scale, 0, height / 2);
  gtk_widget_set_valign(squelch_scale, GTK_ALIGN_CENTER);
  gtk_range_set_increments (GTK_RANGE(squelch_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(squelch_scale), active_receiver->squelch);
  gtk_widget_show(squelch_scale);
  gtk_grid_attach(GTK_GRID(sliders), squelch_scale, sqpos, 1, swidth - 1, 1);
  squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed", G_CALLBACK(squelch_value_changed_cb),
                                       NULL);
  squelch_enable = gtk_check_button_new();
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable), active_receiver->squelch_enable);
  gtk_widget_show(squelch_enable);
  gtk_grid_attach(GTK_GRID(sliders), squelch_enable, s3pos, 1, 1, 1);
  gtk_widget_set_halign(squelch_enable, GTK_ALIGN_CENTER);
  squelch_enable_signal_id = g_signal_connect(squelch_enable, "toggled", G_CALLBACK(squelch_enable_cb), NULL);
  return sliders;
}
