/* Copyright (C)
* 2023 - Christoph van Wüllen, 
#define DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, 
#define either version 3 of the License, 
#define or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, 
#define see <https://www.gnu.org/licenses/>.
*
*/

/*
 * This file contains lots of #defines that control the appearance
 * of piHPSDR, 
#define e.g.
 * - window sizes
 * - font sizes
 * colours.
 *
 * The purpose of this file is that the appearance can be
 * changed easily at compile time.
 *
 * DO NOT CHANGE the "Default" values in the comments, 
#define since
 * these define the original look-and-feel of piHPSDR.
 *
 * IMPORTANT: think twice before adding new colours or font  sizes,
 *            and then decide to re-use an existing one.
 */

/*
 * This file contains lots of #defines that control the appearance
 * of piHPSDR, 
#define e.g.
 * - window sizes
 * - font sizes
 * colours.
 *
 * The purpose of this file is that the appearance can be
 * changed easily at compile time.
 *
 * DO NOT CHANGE the "Default" values in the comments, 
#define since
 * these define the original look-and-feel of piHPSDR.
 *
 * IMPORTANT: think twice before adding new colours or font  sizes,
 *            and then decide to re-use an existing one.
 */

/* The format of the json layout configuration needs to be in a version
 * of format that this version of pihpsdr expects. In the number ..xxyz
 * yz indicates variety without loss of compatibility, i.e. values changed
 * or were added, but no items were removed or repurposed.
 * In case pihpsdr expects new items, the ..xx needs to be increased.
 * An incompatible json file will not be parsed.  
*/
#define FORMAT_MIN 100
#define FORMAT_MAX 199

// the maximum number of layouts in a vfo_layout.json file. 
// The char buffer in function 'parse_vfo_layouts' needs to be able to
// accomodate the data in the file.
#define MAX_LAYOUTS 10

/* Following defines give the indices of the first dimension
 * of an array with layout parameters.
 * In appearance.c, function 'parse_vfo_layouts', the key lists
 * need to be fully compliant with these defines. Order matters.
 * The define's function is to turn the indices of arrays into a hint
 * in the program code about functionality of the array element. 
*/

// these defines start with a a letter that serves
// as a hint in code to fontsize, colour, thickness

#define NR_FONTSIZES 4
#define fSZ1 0  // FONT_SIZE1
#define fSZ2 1  // FONT_SIZE2
#define fSZ3 2  // FONT_SIZE3
#define fSZ4 3  // FONT_SIZE4

#define NR_COLOURS 26
#define cALW 0  // ALARM_WEAK
#define cALM 1  // ALARM
#define cATW 2  // ATTN_WEAK
#define cATT 3  // ATTN
#define cOKW 4  // OK_WEAK
#define cCOK 5  // OK
#define cPFI 6  // PAN_FILTER
#define cPLW 7  // PAN_LINE_WEAK
#define cPLI 8  // PAN_LINE
#define cP60 9  // PAN_60M
#define cMBG 10 // MENU_BACKGND
#define cPBG 11 // PAN_BACKGND
#define cVBG 12 // VFO_BACKGND
#define cSHD 13 // SHADE
#define cMTR 14 // METER
#define cG1W 15 // GRAD1_WEAK
#define cG2W 16 // GRAD2_WEAK
#define cG3W 17 // GRAD3_WEAK
#define cG4W 18 // GRAD4_WEAK
#define cGR1 19 // GRAD1
#define cGR2 20 // GRAD2
#define cGR3 21 // GRAD3
#define cGR4 22 // GRAD4
#define cPF1 23 // PAN_FILL1
#define cPF2 24 // PAN_FILL2
#define cPF3 25 // PAN_FILL3

#define NR_LINE_THICKNESSES 3
#define tLTN 0 // PAN_LINE_THIN
#define tLTH 1 // PAN_LINE_THICK
#define tLXT 2 // PAN_LINE_EXTRA

#define NR_ITEMS 53
#define min_w 0 
#define min_h 1
#define size1 2
#define size2 3
#define size3 4
#define vfo_a_x 5
#define vfo_a_y 6
#define vfo_b_x 7
#define vfo_b_y 8
#define mode_x 9
#define mode_y 10
#define zoom_x 11
#define zoom_y 12
#define ps_x 13
#define ps_y 14
#define rit_x 15
#define rit_y 16
#define xit_x 17
#define xit_y 18
#define nb_x 19
#define nb_y 20
#define nr_x 21
#define nr_y 22
#define anf_x 23
#define anf_y 24
#define snb_x 25
#define snb_y 26
#define agc_x 27
#define agc_y 28
#define cmpr_x 29
#define cmpr_y 30
#define eq_x 31
#define eq_y 32
#define div_x 33
#define div_y 34
#define step_x 35
#define step_y 36
#define ctun_x 37
#define ctun_y 38
#define cat_x 39
#define cat_y 40
#define vox_x 41
#define vox_y 42
#define lock_x 43
#define lock_y 44
#define split_x 45
#define split_y 46
#define sat_x 47
#define sat_y 48
#define dup_x 49
#define dup_y 50
#define filter_x 51
#define filter_y 52

struct _VFO_BAR_LAYOUT {
  char   description[64];          // Text appearing in the screen menu combobox
  int    itm[NR_ITEMS];
  char   FNT[64];                  // font name string
  double FSZ[NR_FONTSIZES];        // font sizes
  double CLR[NR_COLOURS][4];       // colours; 2nd dim. are rgba values
  double PLT[NR_LINE_THICKNESSES]; // pan line widths
};

typedef struct _VFO_BAR_LAYOUT VFO_BAR_LAYOUT;
extern VFO_BAR_LAYOUT vfo_layout_list[MAX_LAYOUTS];

extern int nr_layouts;
extern int vfo_layout;
extern int parse_vfo_layouts( char* filename);

