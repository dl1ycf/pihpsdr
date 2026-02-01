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

#ifndef _NEW_MENU_H_
#define _NEW_MENU_H_

#include <gtk/gtk.h>

extern GtkWidget *sub_menu;
extern GtkWidget *main_menu;

extern void new_menu(void);

extern void start_meter_menu(void);
extern void start_step_menu(void);
extern void start_band_menu(void);
extern void start_bandstack_menu(void);
extern void start_mode_menu(void);
extern void start_filter_menu(void);
extern void start_noise_menu(void);
extern void start_encoder_menu(void);
extern void start_vfo_menu(int vfo);
extern void start_agc_menu(void);
extern void start_store_menu(void);
extern void start_rx_menu(void);
extern void start_radio_menu(void);
extern void start_tx_menu(void);
extern void start_diversity_menu(void);
extern void start_ps_menu(void);
extern void start_server_menu(void);

extern int menu_active_receiver_changed(void *data);

enum _active_menu {
  NO_MENU = 0,
  BAND_MENU,
  BANDSTACK_MENU,
  MODE_MENU,
  FILTER_MENU,
  NOISE_MENU,
  AGC_MENU,
  VFO_MENU,
  STORE_MENU
};

extern int active_menu;

#endif
