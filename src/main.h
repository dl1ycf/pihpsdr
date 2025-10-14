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

#ifndef _MAIN_H_
#define _MAIN_H_

#include <gtk/gtk.h>

extern GdkScreen *screen;
extern int display_size;       // pointer into display_width and display_height, 0 means FullScreen
extern int display_width[6];
extern int display_height[6];
extern int display_vfobar[6];  // Store VFO bar last used with this slot
extern int this_monitor;

extern GtkWidget *top_window;
extern GtkWidget *topgrid;
extern void status_text(const char *text);

extern gulong keypress_signal_id;
extern int fatal_error(void *data);
#endif
