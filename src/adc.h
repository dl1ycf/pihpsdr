/* Copyright (C)
* 2018 - John Melton, G0ORX/N6LYT
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

#ifndef _ADC_H_
#define _ADC_H_

#include <gtk/gtk.h>

typedef struct _adc {
  int antenna;
  int attenuation;
  double gain;
  double min_gain;
  double max_gain;
  int    agc;
  int    dither;
  int    random;
  int    preamp;
  int    alex_attenuation;
  int    filter_bypass;
  int    overload;
} ADC;

#endif
