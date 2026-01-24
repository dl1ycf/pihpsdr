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

#define MAX_TB_FUNCTIONS 6
#define MAX_TB_BUTTONS 8
#define MAX_TB_ROWS 3

extern int tb_function[MAX_TB_ROWS];
extern enum ACTION tb_actions[MAX_TB_FUNCTIONS][MAX_TB_BUTTONS];

extern void toolbar_save_state(void);
extern void toolbar_restore_state(void);

extern void toolbar_create(int width, int height, int rows);
extern void toolbar_destroy(void);
extern void toolbar_show(int ypos);
extern void update_toolbar_labels(void);
