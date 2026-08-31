/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
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
extern void diversity_menu(GtkWidget *parent);

//
// Repaint the dialog's controls from the div_auto_* globals. Does nothing
// if it is not open. Called when the settings changed somewhere other
// than this dialog - which, with the UI able to run on a client, now
// happens on both sides.
//
extern void diversity_menu_refresh(void);

//
// The radio swapped one block of modal settings for another because the
// mode changed. Show the new block, and tell a client that is running the
// panel about it. A g_idle_add() target: a mode change can arrive on the
// server thread, and both halves of this belong to GTK.
//
extern gboolean diversity_menu_settings_changed(gpointer data);
