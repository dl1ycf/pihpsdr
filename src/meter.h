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

#ifndef _METER_H_
#define _METER_H_

#include <gtk/gtk.h>

extern GtkWidget* meter_init(int width, int height);
extern void rxmeter_update(double rxlvl, double peak, double gain, double out);
extern void txmeter_update(double pwr, double alc, double swr, double mic, double out);

enum _meter_type_const {
  DIGITAL = 0,
  ANALOG,
  EDGEWISE,
  DUALSCALE
};
#endif
