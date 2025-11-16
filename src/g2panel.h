/* Copyright (C)
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

void g2panel_save_state(int andromeda_type, const int *buttonvec, const int *encodervec);
void g2panel_restore_state(int andromeda_type, int *buttonvec, int *encodervec);
void g2panel_execute_encoder(int andromeda_type, const int *vec, int p, int v);
void g2panel_execute_button(int andromeda_type, const int *vec, int p, int tr01, int tr10, int tr12, int tr20);
int * g2panel_default_buttons(int andromeda_type);
int * g2panel_default_encoders(int andromeda_type);
