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

/*
 * This file contains lots of #defines that control the appearance
 * of piHPSDR, e.g.
 * - window sizes
 * - font sizes
 * colours.
 *
 * The purpose of this file is that the appearance can be
 * changed easily at compile time.
 *
 * DO NOT CHANGE the "Default" values in the comments, since
 * these define the original look-and-feel of piHPSDR.
 *
 * IMPORTANT: think twice before adding new colours or font  sizes,
 *            and then decide to re-use an existing one.
 */

/*
 * This file contains lots of #defines that control the appearance
 * of piHPSDR, e.g.
 * - window sizes
 * - font sizes
 * colours.
 *
 * The purpose of this file is that the appearance can be
 * changed easily at compile time.
 *
 * DO NOT CHANGE the "Default" values in the comments, since
 * these define the original look-and-feel of piHPSDR.
 *
 * IMPORTANT: think twice before adding new colours or font  sizes,
 *            and then decide to re-use an existing one.


 * Following defines give the indices of the first dimension
 * of an array with GTK layout parameters.
 * In appearance.c the order in which filled from a json file
 * with values needs to match the order here
 */

#define SZ1 0  // FONT_SIZE1
#define SZ2 1  // FONT_SIZE2
#define SZ3 2  // FONT_SIZE3
#define SZ4 3  // FONT_SIZE4

#define ALW 0  // ALARM_WEAK
#define ALM 1  // ALARM
#define ATW 2  // ATTN_WEAK
#define ATT 3  // ATTN
#define OKW 4  // OK_WEAK
#define COK 5  // OK
#define PFI 6  // PAN_FILTER
#define PLW 7  // PAN_LINE_WEAK
#define PLI 8  // PAN_LINE
#define P60 9  // PAN_60M
#define MBG 10 // MENU_BACKGND
#define PBG 11 // PAN_BACKGND
#define VBG 12 // VFO_BACKGND
#define SHD 13 // SHADE
#define MTR 14 // METER
#define G1W 15 // GRAD1_WEAK
#define G2W 16 // GRAD2_WEAK
#define G3W 17 // GRAD3_WEAK
#define G4W 18 // GRAD4_WEAK
#define GR1 19 // GRAD1
#define GR2 20 // GRAD2
#define GR3 21 // GRAD3
#define GR4 22 // GRAD4
#define PF1 23 // PAN_FILL1
#define PF2 24 // PAN_FILL2
#define PF3 25 // PAN_FILL3

#define LTN 0  // PAN_LINE_THIN
#define LTH 1  // PAN_LINE_THICK
#define LXT 2  // PAN_LINE_EXTRA

#define MAX_LAYOUTS 10

extern char   FNT[32];      // display font
extern double FSZ[4];       // font sizes
extern double cl[26][4];    // colours
extern double PLT[3];       // pan line widths

struct _VFO_BAR_LAYOUT {
  char description[64];    // Text appearing in the screen menu combobox
  int width;               // overall width required
  int height;              // overall height required
  int size1;               // Font size for the "LED markers"
  int size2;               // Font size for the "small dial digits"
  int size3;               // Font size for the "large dial digits"

  int vfo_a_x, vfo_a_y;    // coordinates of VFO A/B dial
  int vfo_b_x, vfo_b_y;

  int mode_x,  mode_y;     // Mode/Filter/CW wpm string
  int zoom_x,  zoom_y;     // "Zoom x1"
  int ps_x,    ps_y;       // "PS"
  int rit_x,   rit_y ;     // "RIT +9999Hz"
  int xit_x,   xit_y;      // "XIT +9999Hz"
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
  int vox_x,   vox_y;
  int lock_x,  lock_y;
  int split_x, split_y;
  int sat_x,   sat_y;
  int dup_x,   dup_y;
  int filter_x, filter_y;
};

typedef struct _VFO_BAR_LAYOUT VFO_BAR_LAYOUT;
extern VFO_BAR_LAYOUT vfo_layout_list[MAX_LAYOUTS];

extern int nr_layouts;
extern int vfo_layout;
extern int parse_vfo_layouts( char* filename);
extern int parse_cairo_layout( char* filename);
