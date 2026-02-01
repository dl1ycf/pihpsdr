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

#include "client_server.h"

extern int radio_reconfigure_screen_done;

extern int ext_set_title(gpointer data);

//
// The following calls functions can be called usig g_idle_add
//
extern int ext_start_radio(gpointer data);
extern int ext_vfo_update(gpointer data);
extern int ext_radio_set_tune(gpointer data);
extern int ext_radio_set_mox(gpointer data);
extern int ext_start_tx_menu(gpointer data);        // is this necessary?
extern int ext_start_rx_menu(gpointer data);
extern int ext_start_vfo_menu(gpointer data);
extern int ext_start_band_menu(gpointer data);
extern int ext_radio_set_vox(gpointer data);
extern int ext_radio_set_duplex(gpointer data);      // is this necessary?
extern int ext_radio_reconfigure_screen(gpointer data);

///////////////////////////////////////////////////////////
//
// Obsolete functions removed. Note that calls  such as
//
// g_idle_add(ext_menu_filter,NULL);
//
// can/should be replaced by
//
// schedule_action(MENU_FILTER, PRESSED, 0);
//
// to avoid duplicate code
//
///////////////////////////////////////////////////////////
