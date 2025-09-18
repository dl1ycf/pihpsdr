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


/*
 * This module takes care of the Sliders/Zoom/Pan area. It creates elements
 * in this area. The callbacks then call radio functions. There are also
 * functions to move a slider without doing anything, e.g. when a setting
 * is changed via an external controller.
 * Finally, this module implements a "pop-up" slider that displays a slider
 * on the screen for about 2 seconds. This can be used to inform the user
 * about the changes made via an external controller.
 *
 * Most functions implemented in this module are intended to be queued
 * in the GTK event queue, everything called directly must only be called
 * from within the GTK main thread!
 */
#include <gtk/gtk.h>

#include "actions.h"
#include "ext.h"
#include "main.h"
#include "message.h"
#include "property.h"
#include "radio.h"
#include "sliders.h"

enum ACTION slider_functions[NUM_SLIDERS] = {
  ZOOM,         AGC_GAIN, DRIVE,
  ATTENUATION,  AF_GAIN,  MIC_GAIN,
  PAN,    SQUELCH,  COMPRESSION };

static GtkWidget *sliders_grid = NULL;

static guint scale_timer;
static enum ACTION scale_status = NO_ACTION;
static GtkWidget *scale_dialog;

static GtkWidget *linein_scale = NULL;
static gulong linein_signal_id = 0;

static GtkWidget *speed_scale = NULL;
static gulong speed_signal_id = 0;

static GtkWidget *panlow_scale = NULL;
static gulong panlow_signal_id = 0;

static GtkWidget *af_gain_scale = NULL;
static gulong af_signal_id = 0;

static GtkWidget *rf_gain_label = NULL;
static GtkWidget *rf_gain_scale = NULL;
static gulong rf_signal_id = 0;

static GtkWidget *agc_scale = NULL;
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
static GtkWidget *squelch_enable_b;
static gulong squelch_enable_signal_id = 0;

static GtkWidget *cmpr_scale;
static gulong cmpr_signal_id = 0;
static GtkWidget *cmpr_enable_b;
static gulong cmpr_enable_signal_id = 0;

static GtkWidget *vox_scale;
static gulong vox_signal_id = 0;
static GtkWidget *vox_enable_b;
static gulong vox_enable_signal_id = 0;

static GtkWidget *zoom_scale;
static gulong zoom_signal_id;

static GtkWidget *pan_scale;
static gulong pan_signal_id;

//
// Store/Restore slider settings
//
void sliders_save_state() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    SetPropA1("sliders[%d].funcion",  i,   slider_functions[i]);
  }
}

void sliders_restore_state() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    GetPropA1("sliders[%d].funcion",  i,   slider_functions[i]);
  }
}
//
// call-back functions. They simply call utility functions radio_*()
//

static void attenuation_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_attenuation(active_receiver->id, value);
}

static void agcgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_agc_gain(active_receiver->id, value);
}

static void linein_value_changed_cb(GtkWidget *widget, gpointer data) {
  double val = gtk_range_get_value(GTK_RANGE(widget));
  // Round to half-integers
  val = 0.5 * (int)(2.0 * val);
  radio_set_linein_gain(val);
}

static void panlow_value_changed_cb(GtkWidget *widget, gpointer data) {
  int val = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5);
  radio_set_panlow(active_receiver->id, val);
}

static void speed_value_changed_cb(GtkWidget *widget, gpointer data) {
  int val = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5);
  radio_set_cw_speed(val);
}

static void afgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_af_gain(active_receiver->id, value);
}

static void rf_gain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_rf_gain(active_receiver->id, value);
}

static void micgain_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_mic_gain(value);
}

static void drive_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_drive(value);
}

static void squelch_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_squelch(active_receiver->id, value);
}

static void vox_value_changed_cb(GtkWidget *widget, gpointer data) {
  double value = gtk_range_get_value(GTK_RANGE(widget));
  radio_set_voxlevel(value);
}

static void cmpr_value_changed_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    transmitter->compressor_level = gtk_range_get_value(GTK_RANGE(widget));
    tx_set_compressor(transmitter);
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void squelch_enable_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  radio_set_squelch_enable(active_receiver->id, val);
}

static void vox_enable_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    vox_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void cmpr_enable_cb(GtkWidget *widget, gpointer data) {
  if (can_transmit) {
    transmitter->compressor = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
    tx_set_compressor(transmitter);
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void c25_att_cb(GtkWidget *widget, gpointer data) {
  int val = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget)));
  radio_set_c25_att(active_receiver->id, val);
}

static void zoom_value_changed_cb(GtkWidget *widget, gpointer data) {
  int zoom = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5);
  radio_set_zoom(active_receiver->id, zoom);
}

static void pan_value_changed_cb(GtkWidget *widget, gpointer data) {
  int pan = (int)(gtk_range_get_value(GTK_RANGE(widget)) + 0.5) - 100;
  radio_set_pan(active_receiver->id, pan);
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

static void show_popup_slider(enum ACTION action, int rx, double min, double max, double delta, double value,
                              const char *what) {
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
  char title[128];

  if (rx >= 0) {
    snprintf(title, sizeof(title), "%s%d", what, rx);
  } else {
    snprintf(title, sizeof(title), "%s", what);
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

//
// queue_popup_slider queues a call of show_popup_slider with the same arguments,
// so it can used from non-GTK threads
//
struct _popup_data {
  enum ACTION action;
  int  rx;
  double min;
  double max;
  double delta;
  double value;
  char *what;
};

typedef struct _popup_data POPUP_DATA;

static int remote_popup_slider(gpointer data) {
  const POPUP_DATA *pud = (POPUP_DATA *) data;
  show_popup_slider(pud->action, pud->rx, pud->min, pud->max, pud->delta, pud->value, pud->what);
  g_free(data);
  return G_SOURCE_REMOVE;
}

void queue_popup_slider(enum ACTION action, int rx, double min, double max,
                        double delta, double value, const char *what) {
  POPUP_DATA *pud = g_new(POPUP_DATA, 1);
  pud->action = action;
  pud->rx     = rx;
  pud->min    = min;
  pud->max    = max;
  pud->delta  = delta;
  pud->value  = value;
  pud->what   = g_strdup(what);
  g_idle_add(remote_popup_slider, (gpointer) pud);
}

int sliders_att_type_changed(gpointer data) {
  //
  // Prepeare sliders to reflect whether we have a CHARLY25 or not
  // This is invoked via the GTK queue. This function also takes
  // case set zero out gain/attenuation if there is no such slider
  //
  if (filter_board == CHARLY25) {
    //
    // The filter board is now CHARLY25, but may have been before as well
    //
    adc[0].gain = 0.0;       // NOT USED in C25
    adc[1].gain = 0.0;       // NOT USED in C25
    adc[0].attenuation = 0;  // NOT USED in C25
    adc[1].attenuation = 0;  // NOT USED in C25

    if (attenuation_scale) {
      gtk_widget_hide(attenuation_label);
      gtk_widget_hide(attenuation_scale);
    }

    if (rf_gain_scale) {
      gtk_widget_hide(rf_gain_label);
      gtk_widget_hide(rf_gain_scale);
    }

    if (c25_container) {
      gtk_widget_show(c25_label);
      gtk_widget_show(c25_container);
    }

    if (adc[0].preamp || adc[0].dither) {
      adc[0].alex_attenuation = 0;
    }

    sliders_c25_att(GINT_TO_POINTER(active_receiver->id));
  } else {
    //
    // Zero out controls that may not be present but could have been
    // hi-jacked by C25
    //
    if (!have_preamp) { adc[0].preamp = 0; }

    if (!have_dither) { adc[0].dither = 0; }

    if (!have_alex_att) { adc[0].alex_attenuation = 0; }

    if (attenuation_scale) {
      gtk_widget_show(attenuation_label);
      gtk_widget_show(attenuation_scale);
    }

    if (rf_gain_scale) {
      gtk_widget_show(rf_gain_label);
      gtk_widget_show(rf_gain_scale);
    }

    if (c25_container) {
      gtk_widget_hide(c25_label);
      gtk_widget_hide(c25_container);
    }

    // no need to call these through the event queue
    sliders_attenuation(GINT_TO_POINTER(100 + active_receiver->id));
    sliders_rf_gain(GINT_TO_POINTER(100 + active_receiver->id));
  }

  return G_SOURCE_REMOVE;
}

int sliders_active_receiver_changed(void *data) {
  if (sliders_grid) {
    //
    // Change sliders and check-boxes to reflect the state of the
    // new active receiver. No need to call these through the event queue.
    //
    gpointer id = GINT_TO_POINTER(100 + active_receiver->id);
    sliders_af_gain(id);
    sliders_rf_gain(id);
    sliders_agc_gain(id);
    sliders_squelch(id);
    sliders_c25_att(id);
    sliders_attenuation(id);
    sliders_zoom(id);
    sliders_pan(id);
  }

  return G_SOURCE_REMOVE;
}

//
// The following functions, sliders_*(), "do" nothing but simply
// change sliders/buttons to reflect the current radio status.
// Usually sliders are "blocked" while their value is changed
// to prevent them to emit a signal.
// These functions are intended to be called through the GTK
// event queue but may be called "directly" if you are sure
// you are in the main GTK thread.
//

int sliders_c25_att(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  if (filter_board != CHARLY25) { return G_SOURCE_REMOVE; }

  //
  // Only change the combo-box (no popup slider)
  //
  if (active_receiver->id == id && c25_container) {
    char lbl[16];
    int rxadc = active_receiver->adc;
    int att = -12 * adc[rxadc].alex_attenuation + 18 * (adc[rxadc].dither + adc[rxadc].preamp);
    snprintf(lbl, sizeof(lbl), "%d", att);
    g_signal_handler_block(G_OBJECT(c25_combobox), c25_signal_id);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c25_combobox), lbl);
    g_signal_handler_unblock(G_OBJECT(c25_combobox), c25_signal_id);
  }

  return G_SOURCE_REMOVE;
}

int sliders_attenuation(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers || !have_rx_att) { return G_SOURCE_REMOVE; }

  //
  // This ONLY moves the slider
  //
  int rxadc = receiver[id]->adc;

  if (active_receiver->adc == rxadc && attenuation_scale) {
    g_signal_handler_block(G_OBJECT(attenuation_scale), att_signal_id);
    gtk_range_set_value (GTK_RANGE(attenuation_scale), (double)adc[rxadc].attenuation);
    g_signal_handler_unblock(G_OBJECT(attenuation_scale), att_signal_id);
  } else if (val < 100) {
    show_popup_slider(ATTENUATION, id + 1, 0.0, 31.0, 1.0, (double)adc[rxadc].attenuation,
                      "Attenuation ADC");
  }

  return G_SOURCE_REMOVE;
}

int sliders_agc_gain(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY moves the slider
  //
  if (active_receiver->id == id && agc_scale) {
    g_signal_handler_block(G_OBJECT(agc_scale), agc_signal_id);
    gtk_range_set_value (GTK_RANGE(agc_scale), receiver[id]->agc_gain);
    g_signal_handler_unblock(G_OBJECT(agc_scale), agc_signal_id);
  } else if (val < 100) {
    show_popup_slider(AGC_GAIN, id + 1, -20.0, 120.0, 1.0, receiver[id]->agc_gain, "AGC gain RX");
  }

  return G_SOURCE_REMOVE;
}

int sliders_af_gain(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY moves the slider
  //
  const RECEIVER *rx = receiver[id];

  if (id == active_receiver->id && af_gain_scale) {
    g_signal_handler_block(G_OBJECT(af_gain_scale), af_signal_id);
    gtk_range_set_value (GTK_RANGE(af_gain_scale), rx->volume);
    g_signal_handler_unblock(G_OBJECT(af_gain_scale), af_signal_id);
  } else if (val < 100) {
    show_popup_slider(AF_GAIN, id + 1, -40.0, 0.0, 1.0, rx->volume, "AF gain RX");
  }

  return G_SOURCE_REMOVE;
}

int sliders_rf_gain(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers || !have_rx_gain) { return G_SOURCE_REMOVE; }

  //
  // This ONLY moves the slider
  //
  int rxadc = receiver[id]->adc;

  if (rf_gain_scale && active_receiver->adc == rxadc) {
    g_signal_handler_block(G_OBJECT(rf_gain_scale), rf_signal_id);
    gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[rxadc].gain);
    g_signal_handler_unblock(G_OBJECT(rf_gain_scale), rf_signal_id);
  } else if (val < 100) {
    show_popup_slider(RF_GAIN, rxadc + 1, adc[rxadc].min_gain, adc[rxadc].max_gain, 1.0, adc[rxadc].gain,
                      "RF gain ADC");
  }

  return G_SOURCE_REMOVE;
}

int sliders_filter_width(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  int id = v / 100000;
  int width = v % 100000 - 50000;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY produces a popup-slider
  //
  int min, max;
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

  show_popup_slider(IF_WIDTH, id + 1, (double)(min), (double)(max), 1.0, (double) width,
                    "Filter Width RX");
  return G_SOURCE_REMOVE;
}

int sliders_filter_shift(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  int id = v / 100000;
  int shift = v % 100000 - 50000;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY produces a popup-slider
  //
  int min, max;
  min = shift - 500;
  max = shift + 500;
  show_popup_slider(IF_SHIFT, id + 1, (double)(min), (double) (max), 1.0, (double) shift,
                    "Filter Shift RX");
  return G_SOURCE_REMOVE;
}

int sliders_linein_gain(gpointer data) {
  int val = GPOINTER_TO_INT(data);

  //
  // This ONLY moves the slider
  //
  if (linein_scale) {
    g_signal_handler_block(G_OBJECT(linein_scale), linein_signal_id);
    gtk_range_set_value (GTK_RANGE(linein_scale), linein_gain);
    g_signal_handler_unblock(G_OBJECT(linein_scale), linein_signal_id);
  } else if (val < 100) {
    show_popup_slider(LINEIN_GAIN, -1, -34.0, 12.0, 1.0, linein_gain, "LineIn Gain");
  }

  return G_SOURCE_REMOVE;
}

int sliders_mic_gain(gpointer data) {
  int val = GPOINTER_TO_INT(data);

  //
  // This ONLY moves the slider
  //
  if (can_transmit) {
    if (mic_gain_scale) {
      g_signal_handler_block(G_OBJECT(mic_gain_scale), mic_signal_id);
      gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);
      g_signal_handler_unblock(G_OBJECT(mic_gain_scale), mic_signal_id);
    } else if (val < 100) {
      show_popup_slider(MIC_GAIN, -1, -12.0, 50.0, 1.0, transmitter->mic_gain, "Mic Gain");
    }
  }

  return G_SOURCE_REMOVE;
}

int sliders_drive(gpointer data) {
  int val = GPOINTER_TO_INT(data);

  //
  // This ONLY moves the slider
  //
  if (can_transmit) {
    if (drive_scale) {
      g_signal_handler_block(G_OBJECT(drive_scale), drive_signal_id);
      gtk_range_set_value (GTK_RANGE(drive_scale), (double) transmitter->drive);
      g_signal_handler_unblock(G_OBJECT(drive_scale), drive_signal_id);
    } else if (val < 100) {
      show_popup_slider(DRIVE, -1, drive_min, drive_max, 1.0, (double) transmitter->drive, "TX Drive");
    }
  }

  return G_SOURCE_REMOVE;
}

int sliders_filter_high(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  int id = v / 100000;
  int var = v % 100000 - 50000;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY produces a popup-slider
  //
  int min, max;
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

  show_popup_slider(FILTER_CUT_HIGH, id + 1, (double)(min), (double)(max), 1.00, (double) var,
                    "Filter Cut High RX");
  return G_SOURCE_REMOVE;
}

int sliders_filter_low(gpointer data) {
  int v = GPOINTER_TO_INT(data);
  int id = v / 100000;
  int var = v % 100000 - 50000;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  // This ONLY produces a popup-slider
  //
  int min, max;

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

  show_popup_slider(FILTER_CUT_LOW, id + 1, (double)(min), (double)(max), 1.00, (double) var,
                    "Filter Cut Low RX");
  return G_SOURCE_REMOVE;
}

int sliders_squelch(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  if (id > receivers) { return G_SOURCE_REMOVE; }

  //
  RECEIVER *rx = receiver[id];

  if (squelch_scale && id == active_receiver->id) {
    g_signal_handler_block(G_OBJECT(squelch_scale), squelch_signal_id);
    gtk_range_set_value (GTK_RANGE(squelch_scale), rx->squelch);
    g_signal_handler_unblock(G_OBJECT(squelch_scale), squelch_signal_id);
    g_signal_handler_block(G_OBJECT(squelch_enable_b), squelch_enable_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable_b), rx->squelch_enable);
    g_signal_handler_unblock(G_OBJECT(squelch_enable_b), squelch_enable_signal_id);
  } else if (val < 100) {
    show_popup_slider(SQUELCH, id + 1, 0.0, 100.0, 1.0, rx->squelch, "Squelch RX");
  }

  return G_SOURCE_REMOVE;
}

int sliders_panlow(gpointer data) {
  //
  // This ONLY moves the slider
  // No popup-slider since settings are displayed in VFO bar,
  //
  if (panlow_scale) {
    g_signal_handler_block(G_OBJECT(panlow_scale), panlow_signal_id);
    gtk_range_set_value (GTK_RANGE(panlow_scale), active_receiver->panadapter_low);
    g_signal_handler_unblock(G_OBJECT(panlow_scale), panlow_signal_id);
  }

  return G_SOURCE_REMOVE;
}

int sliders_wpm(gpointer data) {
  //
  // This ONLY moves the slider
  // No popup-slider since settings are displayed in VFO bar,
  //
  if (speed_scale) {
    g_signal_handler_block(G_OBJECT(speed_scale), speed_signal_id);
    gtk_range_set_value (GTK_RANGE(speed_scale), cw_keyer_speed);
    g_signal_handler_unblock(G_OBJECT(speed_scale), speed_signal_id);
  }

  g_idle_add(ext_vfo_update, NULL);
  return G_SOURCE_REMOVE;
}

int sliders_vox(gpointer data) {
  //
  // This ONLY moves the slider and updates the checkbutton
  // No popup-slider since settings are displayed in VFO bar,
  //
  if (can_transmit && vox_scale) {
    g_signal_handler_block(G_OBJECT(vox_scale), vox_signal_id);
    gtk_range_set_value (GTK_RANGE(vox_scale), vox_threshold);
    g_signal_handler_unblock(G_OBJECT(vox_scale), vox_signal_id);
    g_signal_handler_block(G_OBJECT(vox_enable_b), vox_enable_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vox_enable_b), vox_enabled);
    g_signal_handler_unblock(G_OBJECT(vox_enable_b), vox_enable_signal_id);
  }

  g_idle_add(ext_vfo_update, NULL);
  return G_SOURCE_REMOVE;
}

int sliders_cmpr(gpointer data) {
  //
  // This ONLY moves the slider and updates the checkbutton
  // No popup-slider since settings are displayed in VFO bar,
  //
  if (can_transmit && cmpr_scale) {
    g_signal_handler_block(G_OBJECT(cmpr_scale), cmpr_signal_id);
    gtk_range_set_value (GTK_RANGE(cmpr_scale), transmitter->compressor_level);
    g_signal_handler_unblock(G_OBJECT(cmpr_scale), cmpr_signal_id);
    g_signal_handler_block(G_OBJECT(cmpr_enable_b), cmpr_enable_signal_id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cmpr_enable_b), transmitter->compressor);
    g_signal_handler_unblock(G_OBJECT(cmpr_enable_b), cmpr_enable_signal_id);
  }

  g_idle_add(ext_vfo_update, NULL);
  return G_SOURCE_REMOVE;
}

int sliders_diversity_gain(gpointer data) {
  //
  // This ONLY produces a popup-slider
  //
  show_popup_slider(DIV_GAIN, -1, -27.0, 27.0, 0.01, div_gain, "Diversity Gain");
  return G_SOURCE_REMOVE;
}

int sliders_diversity_phase(gpointer data) {
  //
  // This ONLY produces a popup-slider
  //
  show_popup_slider(DIV_PHASE, -1, -180.0, 180.0, 0.1, div_phase, "Diversity Phase");
  return G_SOURCE_REMOVE;
}

int sliders_zoom(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  // This ONLY moves the zoom slider

  if (id == active_receiver->id && zoom_scale) {
    g_signal_handler_block(G_OBJECT(zoom_scale), zoom_signal_id);
    gtk_range_set_value(GTK_RANGE(zoom_scale), active_receiver->zoom);
    g_signal_handler_unblock(G_OBJECT(zoom_scale), zoom_signal_id);
  }

  return G_SOURCE_REMOVE;
}

int sliders_pan(gpointer data) {
  int val = GPOINTER_TO_INT(data);
  int id = val % 100;

  // This ONLY moves the pan sliders

  if (id == active_receiver->id && pan_scale) {
    g_signal_handler_block(G_OBJECT(pan_scale), pan_signal_id);
    gtk_range_set_value (GTK_RANGE(pan_scale), (double) active_receiver->pan);
    g_signal_handler_unblock(G_OBJECT(pan_scale), pan_signal_id);
  }

  return G_SOURCE_REMOVE;
}

void sliders_show_sliders(int ypos) {
  if (sliders_grid) {
    gtk_fixed_put(GTK_FIXED(fixed),  sliders_grid, 0, ypos);
    gtk_widget_show_all(sliders_grid);
    sliders_att_type_changed(NULL);
  }
}

void sliders_destroy() {
  if (sliders_grid) {
    gtk_container_remove(GTK_CONTAINER(fixed), sliders_grid);
    sliders_grid = NULL;
  }

  af_gain_scale = NULL;
  rf_gain_scale = NULL;
  agc_scale = NULL;
  attenuation_scale = NULL;
  c25_container = NULL;
  mic_gain_scale = NULL;
  drive_scale = NULL;
  squelch_scale = NULL;
  cmpr_scale = NULL;
  vox_scale = NULL;
  zoom_scale = NULL;
  pan_scale = NULL;
  speed_scale = NULL;
  linein_scale = NULL;
  panlow_scale = NULL;
}

void sliders_create(int width, int height, int rows) {
  GtkWidget *label;
  //
  // The larger the width, the smaller the fraction used for the label can be
  // font size.
  //
  int twidth, swidth, tpix;
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
    // slider width: 4/15 of screen width
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

  sliders_destroy();
  sliders_grid = gtk_grid_new();
  gtk_widget_set_size_request (sliders_grid, width, height);
  gtk_grid_set_row_homogeneous(GTK_GRID(sliders_grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(sliders_grid), TRUE);

  for (int i = 0; i < rows; i++) {
    int pos = 0;

    for (int j = 0; j < 3; j++) {
      switch (slider_functions[3 * i + j]) {
      case ZOOM:
        if (zoom_scale == NULL) {
          label = gtk_label_new("Zoom");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, MAX_ZOOM, 1.00);
          gtk_widget_set_size_request(zoom_scale, 0, height);
          gtk_widget_set_valign(zoom_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(zoom_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(zoom_scale), active_receiver->zoom);
          gtk_grid_attach(GTK_GRID(sliders_grid), zoom_scale, pos + twidth, i, swidth, 1);
          zoom_signal_id = g_signal_connect(G_OBJECT(zoom_scale), "value_changed",
                                            G_CALLBACK(zoom_value_changed_cb), NULL);
        }

        break;

      case PAN:
        if (pan_scale == NULL) {
          label = gtk_label_new("Pan");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          pan_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100.0, 100.0, 1.0);
          gtk_widget_set_size_request(pan_scale, 0, height);
          gtk_widget_set_valign(pan_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(pan_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(pan_scale), (double) active_receiver->pan);
          gtk_grid_attach(GTK_GRID(sliders_grid), pan_scale, pos + twidth, i, swidth, 1);
          pan_signal_id = g_signal_connect(G_OBJECT(pan_scale), "value_changed",
                                           G_CALLBACK(pan_value_changed_cb), NULL);
        }

        break;

      case ATTENUATION:
      case RF_GAIN:

        //
        // ATT, RFGAIN, and C25 stuff all go to this position
        //
        if (have_rx_gain && rf_gain_scale == NULL) {
          rf_gain_label = gtk_label_new("RF");
          gtk_widget_set_name(rf_gain_label, csslabel);
          gtk_widget_set_halign(rf_gain_label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), rf_gain_label, pos, i, twidth, 1);
          rf_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                          adc[0].min_gain, adc[0].max_gain, 1.0);
          gtk_widget_set_size_request(rf_gain_scale, 0, height);
          gtk_widget_set_valign(rf_gain_scale, GTK_ALIGN_CENTER);
          gtk_range_set_value (GTK_RANGE(rf_gain_scale), adc[0].gain);
          gtk_range_set_increments (GTK_RANGE(rf_gain_scale), 1.0, 1.0);
          gtk_grid_attach(GTK_GRID(sliders_grid), rf_gain_scale, pos + twidth, i, swidth, 1);
          rf_signal_id = g_signal_connect(G_OBJECT(rf_gain_scale), "value_changed",
                                          G_CALLBACK(rf_gain_value_changed_cb), NULL);
        }

        if (have_rx_att && attenuation_scale == NULL) {
          attenuation_label = gtk_label_new("ATT");
          gtk_widget_set_name(attenuation_label, csslabel);
          gtk_widget_set_halign(attenuation_label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), attenuation_label, pos, i, twidth, 1);
          attenuation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 31.0, 1.0);
          gtk_widget_set_size_request(attenuation_scale, 0, height);
          gtk_widget_set_valign(attenuation_scale, GTK_ALIGN_CENTER);
          gtk_range_set_value (GTK_RANGE(attenuation_scale), adc[active_receiver->adc].attenuation);
          gtk_range_set_increments (GTK_RANGE(attenuation_scale), 1.0, 1.0);
          gtk_grid_attach(GTK_GRID(sliders_grid), attenuation_scale, pos + twidth, i, swidth, 1);
          att_signal_id = g_signal_connect(G_OBJECT(attenuation_scale), "value_changed",
                                           G_CALLBACK(attenuation_value_changed_cb), NULL);
        }

        //
        // These handles need to be created because they are activated/deactivaded
        // depending on selecting/deselcting the CHARLY25 filter board
        // Because "touch-screen friendly" comboboxes cannot be shown/hidden properly,
        // we put this into a container
        //
        if (c25_container == NULL) {
          c25_label = gtk_label_new("Att/Pre");
          gtk_widget_set_name(c25_label, csslabel);
          gtk_widget_set_halign(c25_label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), c25_label, pos, i, twidth, 1);
          c25_container = gtk_fixed_new();
          gtk_grid_attach(GTK_GRID(sliders_grid), c25_container, pos + twidth, i, swidth, 1);
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
          c25_signal_id = g_signal_connect(G_OBJECT(c25_combobox), "changed",
                                           G_CALLBACK(c25_att_cb), NULL);
          gtk_container_add(GTK_CONTAINER(c25_container), c25_grid);
        }

        break;

      case AF_GAIN:
        if (af_gain_scale == NULL) {
          label = gtk_label_new("AF");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          af_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 0.0, 1.00);
          gtk_widget_set_size_request(af_gain_scale, 0, height);
          gtk_widget_set_valign(af_gain_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(af_gain_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(af_gain_scale), active_receiver->volume);
          gtk_grid_attach(GTK_GRID(sliders_grid), af_gain_scale, pos + twidth, i, swidth, 1);
          af_signal_id = g_signal_connect(G_OBJECT(af_gain_scale), "value_changed",
                                          G_CALLBACK(afgain_value_changed_cb), NULL);
        }

        break;

      case AGC_GAIN:
        if (agc_scale == NULL) {
          label = gtk_label_new("AGC");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          agc_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20.0, 120.0, 1.0);
          gtk_widget_set_size_request(agc_scale, 0, height);
          gtk_widget_set_valign(agc_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(agc_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(agc_scale), active_receiver->agc_gain);
          gtk_grid_attach(GTK_GRID(sliders_grid), agc_scale, pos + twidth, i, swidth, 1);
          agc_signal_id = g_signal_connect(G_OBJECT(agc_scale), "value_changed",
                                           G_CALLBACK(agcgain_value_changed_cb), NULL);
        }

        break;

      case SQUELCH:
        if (squelch_scale == NULL) {
          label = gtk_label_new("Sqlch");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          squelch_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
          gtk_widget_set_size_request(squelch_scale, 0, height);
          gtk_widget_set_valign(squelch_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(squelch_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(squelch_scale), active_receiver->squelch);
          gtk_grid_attach(GTK_GRID(sliders_grid), squelch_scale, pos + twidth + 1, i, swidth - 1, 1);
          squelch_signal_id = g_signal_connect(G_OBJECT(squelch_scale), "value_changed",
                                               G_CALLBACK(squelch_value_changed_cb), NULL);
          squelch_enable_b = gtk_check_button_new();
          gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squelch_enable_b),
                                       active_receiver->squelch_enable);
          gtk_grid_attach(GTK_GRID(sliders_grid), squelch_enable_b, pos + twidth, i, 1, 1);
          gtk_widget_set_halign(squelch_enable_b, GTK_ALIGN_CENTER);
          squelch_enable_signal_id = g_signal_connect(squelch_enable_b, "toggled",
                                     G_CALLBACK(squelch_enable_cb), NULL);
        }

        break;

      case MIC_GAIN:
        if (can_transmit && mic_gain_scale == NULL) {
          label = gtk_label_new("Mic");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          mic_gain_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 50.0, 1.0);
          gtk_widget_set_size_request(mic_gain_scale, 0, height);
          gtk_widget_set_valign(mic_gain_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(mic_gain_scale), 1.0, 1.0);
          gtk_grid_attach(GTK_GRID(sliders_grid), mic_gain_scale, pos + twidth, i, swidth, 1);
          gtk_range_set_value (GTK_RANGE(mic_gain_scale), transmitter->mic_gain);
          mic_signal_id = g_signal_connect(G_OBJECT(mic_gain_scale), "value_changed",
                                           G_CALLBACK(micgain_value_changed_cb), NULL);
        }

        break;

      case DRIVE:
        if (can_transmit && drive_scale == NULL) {
          label = gtk_label_new("TX Drv");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          drive_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                 drive_min, drive_max, 1.00);
          gtk_widget_set_size_request(drive_scale, 0, height);
          gtk_widget_set_valign(drive_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(drive_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(drive_scale), radio_get_drive());
          gtk_grid_attach(GTK_GRID(sliders_grid), drive_scale, pos + twidth, i, swidth, 1);
          drive_signal_id = g_signal_connect(G_OBJECT(drive_scale), "value_changed",
                                             G_CALLBACK(drive_value_changed_cb), NULL);
        }

        break;

      case VOXLEVEL:
        if (can_transmit && vox_scale == NULL) {
          label = gtk_label_new("VOX");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          vox_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
          gtk_widget_set_size_request(vox_scale, 0, height);
          gtk_widget_set_valign(vox_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(vox_scale), 0.01, 0.01);
          gtk_range_set_value (GTK_RANGE(vox_scale), vox_threshold);
          gtk_grid_attach(GTK_GRID(sliders_grid), vox_scale, pos + twidth + 1, i, swidth - 1, 1);
          vox_signal_id = g_signal_connect(G_OBJECT(vox_scale), "value_changed",
                                           G_CALLBACK(vox_value_changed_cb), NULL);
          vox_enable_b = gtk_check_button_new();
          gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vox_enable_b), vox_enabled);
          gtk_grid_attach(GTK_GRID(sliders_grid), vox_enable_b, pos + twidth, i, 1, 1);
          gtk_widget_set_halign(vox_enable_b, GTK_ALIGN_CENTER);
          vox_enable_signal_id = g_signal_connect(vox_enable_b, "toggled",
                                                  G_CALLBACK(vox_enable_cb), NULL);
        }

        break;

      case COMPRESSION:
        if (can_transmit && cmpr_scale == NULL) {
          label = gtk_label_new("Cmpr");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          cmpr_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 20.0, 1.0);
          gtk_widget_set_size_request(cmpr_scale, 0, height);
          gtk_widget_set_valign(cmpr_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(cmpr_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(cmpr_scale), transmitter->compressor_level);
          gtk_grid_attach(GTK_GRID(sliders_grid), cmpr_scale, pos + twidth + 1, i, swidth - 1, 1);
          cmpr_signal_id = g_signal_connect(G_OBJECT(cmpr_scale), "value_changed",
                                            G_CALLBACK(cmpr_value_changed_cb), NULL);
          cmpr_enable_b = gtk_check_button_new();
          gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cmpr_enable_b), transmitter->compressor);
          gtk_grid_attach(GTK_GRID(sliders_grid), cmpr_enable_b, pos + twidth, i, 1, 1);
          gtk_widget_set_halign(cmpr_enable_b, GTK_ALIGN_CENTER);
          cmpr_enable_signal_id = g_signal_connect(cmpr_enable_b, "toggled",
                                  G_CALLBACK(cmpr_enable_cb), NULL);
        }

        break;

     case PANADAPTER_LOW:
        if (panlow_scale == NULL) {
          label = gtk_label_new("PLow");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          panlow_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -160.0, -80.0, 5.0);
          gtk_widget_set_size_request(panlow_scale, 0, height);
          gtk_widget_set_valign(panlow_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(panlow_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(panlow_scale), active_receiver->panadapter_low);
          gtk_grid_attach(GTK_GRID(sliders_grid), panlow_scale, pos + twidth, i, swidth, 1);
          panlow_signal_id = g_signal_connect(G_OBJECT(panlow_scale), "value_changed",
                                             G_CALLBACK(panlow_value_changed_cb), NULL);
        }

        break;

     case CW_SPEED:
        if (speed_scale == NULL) {
          label = gtk_label_new("WPM");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 60.0, 1.0);
          gtk_widget_set_size_request(speed_scale, 0, height);
          gtk_widget_set_valign(speed_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(speed_scale), 1.0, 1.0);
          gtk_range_set_value (GTK_RANGE(speed_scale), cw_keyer_speed);
          gtk_grid_attach(GTK_GRID(sliders_grid), speed_scale, pos + twidth, i, swidth, 1);
          speed_signal_id = g_signal_connect(G_OBJECT(speed_scale), "value_changed",
                                             G_CALLBACK(speed_value_changed_cb), NULL);
        }

        break;

      case LINEIN_GAIN:
        if (linein_scale == NULL) {
          label = gtk_label_new("Line");
          gtk_widget_set_name(label, csslabel);
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_grid_attach(GTK_GRID(sliders_grid), label, pos, i, twidth, 1);
          linein_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -34.5, 12.0, 1.5);
          gtk_widget_set_size_request(linein_scale, 0, height);
          gtk_widget_set_valign(linein_scale, GTK_ALIGN_CENTER);
          gtk_range_set_increments (GTK_RANGE(linein_scale), 1.5, 1.5);
          gtk_scale_set_digits(GTK_SCALE(linein_scale), 1);
          gtk_range_set_value (GTK_RANGE(linein_scale), linein_gain);
          gtk_grid_attach(GTK_GRID(sliders_grid), linein_scale, pos + twidth, i, swidth, 1);
          linein_signal_id = g_signal_connect(G_OBJECT(linein_scale), "value_changed",
                                              G_CALLBACK(linein_value_changed_cb), NULL);
        }

        break;

      default:
        break;
      }

      pos += swidth + twidth;
    }
  }
}
