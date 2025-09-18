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

#include "actions.h"
#include "receiver.h"
#include "transmitter.h"
#include "actions.h"

#define MAX_ZOOM 32
#define NUM_SLIDERS 9

extern enum ACTION slider_functions[NUM_SLIDERS];

extern void sliders_save_state();
extern void sliders_restore_state();

void queue_popup_slider(enum ACTION action, int rx, double min, double max, double delta, double value,
                        const char *title);

extern void sliders_show_zoompan(int y);
extern void sliders_show_sliders(int y);
extern void sliders_create(int width, int height, int rows);
extern void sliders_destroy();

extern int sliders_active_receiver_changed(void *data);
extern int sliders_att_type_changed(void *data);

//
// These functions ONLY move the sliders
//
extern int sliders_filter_low(gpointer data);
extern int sliders_filter_high(gpointer data);
extern int sliders_filter_width(gpointer data);
extern int sliders_filter_shift(gpointer data);
extern int sliders_agc_gain(gpointer data);
extern int sliders_af_gain(gpointer data);
extern int sliders_rf_gain(gpointer data);
extern int sliders_attenuation(gpointer data);
extern int sliders_c25_att(gpointer data);
extern int sliders_squelch(gpointer data);
extern int sliders_cmpr(gpointer data);
extern int sliders_vox(gpointer data);
extern int sliders_mic_gain(gpointer data);
extern int sliders_linein_gain(gpointer data);
extern int sliders_drive(gpointer data);
extern int sliders_diversity_gain(gpointer data);
extern int sliders_diversity_phase(gpointer data);
extern int sliders_zoom(gpointer data);
extern int sliders_pan(gpointer data);
extern int sliders_panlow(gpointer data);
