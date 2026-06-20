/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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

#ifndef _APPEARANCE_H_
#define _APPEARANCE_H_
//
// This *only* defines Fonts and sizes for VFO, meter, and the panadapters,
// since fonts used for GTK buttons, texts, etc. are defined via CSS in css.c
//
// Note both the digital and analog RX meter "dBm" reading is printed in a font size
// that is calculated based on available space.
//
//
#include "css.h"

#define DISPLAY_FONT_FACE  cssfont[which_css_font]

//
// thin and thick line widths in the panadapers
// "thick" and "extra" also used in the analog meter
//
#define PAN_LINE_THIN  0.5
#define PAN_LINE_THICK 1.0
#define PAN_LINE_EXTRA 2.0  // used for really important things such as band edges, and the analog meter needle.

//
// This data structure contains the size of the VFO bar, and the position of its elements
// Several such layouts are stored in the array vfo_layout_list[] (see appearance.c).
//
struct _VFO_BAR_LAYOUT {
  const char *description; // Text appearing in the screen menu combobox
  int width;               // overall width required
  int height;              // overall height required
  int size1;               // Font size for the "LED markers"
  int size2;               // Font size for the "small dial digits"
  int size3;               // Font size for the "large dial digits"

  int vfo_a_l, vfo_a_r, vfo_a_y;    // x (left/right) and y coordinates of VFO dial frequency
  int vfo_b_l, vfo_b_r, vfo_b_y;

  int mode_x,  mode_y;     // Mode/Filter/CW wpm string
  int zoom_x,  zoom_y;     // "Zoom x1"
  int ps_x,    ps_y;       // "PS"
  int rit_x,   rit_y ;     // "RIT -9999Hz"
  int xit_x,   xit_y;      // "XIT -9999Hz"
  int nb_x,    nb_y;       // NB/NB2
  int nr_x,    nr_y;
  int anf_x,   anf_y;
  int snb_x,   snb_y;
  int agc_x,   agc_y;      // "AGC slow"
  int cmpr_x,  cmpr_y;
  int eq_x,    eq_y;
  int div_x,   div_y;
  int step_x,  step_y;     // "Step 100 kHz"
  int ctun_x,  ctun_y;
  int cat_x,   cat_y;
  int dexp_x,  dexp_y;
  int vox_x,   vox_y;
  int lock_x,  lock_y;
  int split_x, split_y;
  int sat_x,   sat_y;
  int dup_x,   dup_y;
  int filter_x, filter_y;
  int multifn_x, multifn_y;
  int lat_x, lat_y;      // latency indicator (right- or left-aligned)
};

typedef struct _VFO_BAR_LAYOUT VFO_BAR_LAYOUT;
extern const VFO_BAR_LAYOUT vfo_layout_list[];

#endif
