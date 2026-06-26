/* Copyright (C)
*  2023, 2024 - Christoph van Wüllen, DL1YCF
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
 * This file contains data (tables) which describe the layout
 * e.g. of the VFO bar. The layout contains (x,y) coordinates of
 * the individual elements as well as font sizes.
 *
 * There can be more than one "layout", characterized by its size
 * request. So the program can choose the largest layout that
 * fits into the allocated area.
 *
 * What this should do is, that if the user increases the width of
 * the screen and the VFO bar, the program can automatically
 * switch to a larger font.
 */

#include <stdlib.h>

#include "appearance.h"

const VFO_BAR_LAYOUT *current_vfo_layout = vfo_layout_list;
//
// When a VFO bar layout that fits is searched in this list,
// first mathing layout is taken,
// so the largest one come first and the smallest one last.
//
const VFO_BAR_LAYOUT vfo_layout_list[] = {
  //
  // Our largest layout. Hopefully suitable for those
  // with impaired vision using a 1920px Screen
  //
  {
    .description = "VFO for LARGE Screens",
    .width = 1420,
    .height = 170,
    .size1 = 28,
    .size2 = 54,
    .size3 = 72,

    .vfo_a_l = 5,
    .vfo_a_r = 600,
    .vfo_a_y = 125,
    .vfo_b_l = 880,
    .vfo_b_r = 1410,
    .vfo_b_y = 125,

    .mode_x = 5,
    .mode_y = 43,
    .agc_x = 450,
    .agc_y = 43,
    .nr_x = 630,
    .nr_y = 43,
    .nb_x = 720,
    .nb_y = 43,
    .anf_x = 790,
    .anf_y = 43,
    .snb_x = 880,
    .snb_y = 43,
    .div_x = 950,
    .div_y = 43,
    .eq_x = 1030,
    .eq_y = 43,
    .cat_x = 1100,
    .cat_y = 43,

    .cmpr_x = 630,
    .cmpr_y = 85,
    .ps_x = 880,
    .ps_y = 160,
    .dexp_x = 790,
    .dexp_y = 85,

    .vox_x = 630,
    .vox_y = 125,
    .dup_x = 790,
    .dup_y = 125,

    .lock_x = 5,
    .lock_y = 160,
    .zoom_x = 160,
    .zoom_y = 160,
    .ctun_x = 320,
    .ctun_y = 160,
    .step_x = 450,
    .step_y = 160,
    .split_x = 630,
    .split_y = 160,
    .sat_x = 790,
    .sat_y = 160,
    .rit_x = 950,
    .rit_y = 160,
    .xit_x = 1110,
    .xit_y = 160,
    .filter_x = 1240,
    .filter_y = 43,
    .multifn_x = 1260,
    .multifn_y = 160,

    .lat_x = 5,
    .lat_y = 75
  },

  //
  // 1210 pix: 1280 screen layout scaled with 1.5
  //
  {
    .description = "1210pix",
    .width = 1210,
    .height = 145,
    .size1 = 24,
    .size2 = 45,
    .size3 = 66,

    .vfo_a_l = 5,
    .vfo_a_r = 525,
    .vfo_a_y = 105,
    .vfo_b_l = 735,
    .vfo_b_r = 1210,
    .vfo_b_y = 105,

    .mode_x = 5,
    .mode_y = 36,
    .agc_x = 375,
    .agc_y = 36,
    .nr_x = 525,
    .nr_y = 36,
    .nb_x = 600,
    .nb_y = 36,
    .anf_x = 660,
    .anf_y = 36,
    .snb_x = 735,
    .snb_y = 36,
    .div_x = 795,
    .div_y = 36,
    .eq_x = 855,
    .eq_y = 36,
    .cat_x = 930,
    .cat_y = 36,

    .cmpr_x = 525,
    .cmpr_y = 68,
    .ps_x = 735,
    .ps_y = 135,
    .dexp_x = 660,
    .dexp_y = 68,

    .vox_x = 525,
    .vox_y = 102,
    .dup_x = 660,
    .dup_y = 102,

    .lock_x = 5,
    .lock_y = 135,
    .zoom_x = 135,
    .zoom_y = 135,
    .ctun_x = 270,
    .ctun_y = 135,
    .step_x = 375,
    .step_y = 135,
    .split_x = 525,
    .split_y = 135,
    .sat_x = 660,
    .sat_y = 135,
    .rit_x = 795,
    .rit_y = 135,
    .xit_x = 930,
    .xit_y = 135,
    .filter_x = 1035,
    .filter_y = 36,
    .multifn_x = 1070,
    .multifn_y = 135,
    .lat_x = 5,
    .lat_y = 63
  },

  //
  // A layout tailored for a screen 1600 px wide,
  // which is the 1280px version scaled with 1.25
  //
  {
    .description = "VFO for 1600px Screens",
    .width = 1005,
    .height = 120,
    .size1 = 20,
    .size2 = 38,
    .size3 = 54,

    .vfo_a_l = 5,
    .vfo_a_r = 440,
    .vfo_a_y = 86,
    .vfo_b_l = 612,
    .vfo_b_r = 1005,
    .vfo_b_y = 86,

    .mode_x = 5,
    .mode_y = 30,
    .agc_x = 312,
    .agc_y = 30,
    .nr_x = 438,
    .nr_y = 30,
    .nb_x = 500,
    .nb_y = 30,
    .anf_x = 550,
    .anf_y = 30,
    .snb_x = 612,
    .snb_y = 30,
    .div_x = 662,
    .div_y = 30,
    .eq_x = 712,
    .eq_y = 30,
    .cat_x = 775,
    .cat_y = 30,

    .cmpr_x = 438,
    .cmpr_y = 56,
    .ps_x = 612,
    .ps_y = 112,
    .dexp_x = 550,
    .dexp_y = 56,

    .vox_x = 438,
    .vox_y = 85,
    .dup_x = 550,
    .dup_y = 85,

    .lock_x = 5,
    .lock_y = 112,
    .zoom_x = 112,
    .zoom_y = 112,
    .ctun_x = 225,
    .ctun_y = 112,
    .step_x = 312,
    .step_y = 112,
    .split_x = 438,
    .split_y = 112,
    .sat_x = 550,
    .sat_y = 112,
    .rit_x = 662,
    .rit_y = 112,
    .xit_x = 775,
    .xit_y = 112,
    .filter_x = 862,
    .filter_y = 30,
    .multifn_x = 892,
    .multifn_y = 112,

    .lat_x = 5,
    .lat_y = 52
  },

  //
  // 925 pix wide, the "1280" pix layout scaled with 1.15
  //
  {
    .description = "925 pix wide",
    .width = 925,
    .height = 109,
    .size1 = 18,
    .size2 = 34,
    .size3 = 50,

    .vfo_a_l = 5,
    .vfo_a_r = 405,
    .vfo_a_y = 80,
    .vfo_b_l = 565,
    .vfo_b_r = 925,
    .vfo_b_y = 80,

    .mode_x = 5,
    .mode_y = 28,
    .agc_x = 285,
    .agc_y = 28,
    .nr_x = 405,
    .nr_y = 28,
    .nb_x = 460,
    .nb_y = 28,
    .anf_x = 505,
    .anf_y = 28,
    .snb_x = 565,
    .snb_y = 28,
    .div_x = 610,
    .div_y = 28,
    .eq_x = 655,
    .eq_y = 28,
    .cat_x = 715,
    .cat_y = 28,

    .cmpr_x = 405,
    .cmpr_y = 52,
    .ps_x = 565,
    .ps_y = 105,
    .dexp_x = 505,
    .dexp_y = 52,

    .vox_x = 405,
    .vox_y = 78,
    .dup_x = 505,
    .dup_y = 78,

    .lock_x = 5,
    .lock_y = 105,
    .zoom_x = 105,
    .zoom_y = 105,
    .ctun_x = 205,
    .ctun_y = 105,
    .step_x = 285,
    .step_y = 105,
    .split_x = 405,
    .split_y = 105,
    .sat_x = 460,
    .sat_y = 105,
    .rit_x = 610,
    .rit_y = 105,
    .xit_x = 715,
    .xit_y = 105,
    .filter_x = 795,
    .filter_y = 28,
    .multifn_x = 820,
    .multifn_y = 105,
    .lat_x = 5,
    .lat_y = 48
  },

  //
  // A layout tailored for a screen 1280 px wide:
  // a Layout with dial digits of size 50, and a "LED" size 20
  // which requires a width of 875 and a height of 90
  //
  {
    .description = "VFO for 1280px Screens",
    .width = 805,
    .height = 95,
    .size1 = 16,
    .size2 = 30,
    .size3 = 44,

    .vfo_a_l = 5,
    .vfo_a_r = 350,
    .vfo_a_y = 70,
    .vfo_b_l = 490,
    .vfo_b_r = 805,
    .vfo_b_y = 70,

    .mode_x = 5,
    .mode_y = 24,
    .agc_x = 250,
    .agc_y = 24,
    .nr_x = 350,
    .nr_y = 24,
    .nb_x = 400,
    .nb_y = 24,
    .anf_x = 440,
    .anf_y = 24,
    .snb_x = 490,
    .snb_y = 24,
    .div_x = 530,
    .div_y = 24,
    .eq_x = 570,
    .eq_y = 24,
    .cat_x = 620,
    .cat_y = 24,

    .cmpr_x = 350,
    .cmpr_y = 45,
    .ps_x = 490,
    .ps_y = 90,
    .dexp_x = 440,
    .dexp_y = 45,

    .vox_x = 350,
    .vox_y = 68,
    .dup_x = 440,
    .dup_y = 68,

    .lock_x = 5,
    .lock_y = 90,
    .zoom_x = 90,
    .zoom_y = 90,
    .ctun_x = 180,
    .ctun_y = 90,
    .step_x = 250,
    .step_y = 90,
    .split_x = 350,
    .split_y = 90,
    .sat_x = 440,
    .sat_y = 90,
    .rit_x = 530,
    .rit_y = 90,
    .xit_x = 620,
    .xit_y = 90,
    .filter_x = 690,
    .filter_y = 24,
    .multifn_x = 715,
    .multifn_y = 90,
    .lat_x = 5,
    .lat_y = 42
  },

  //
  // A layout tailored for a screen 1024 px wide:
  // a Layout with dial digits of size 40, and a "LED" size 17
  // which requires a width of 745 and a height of 78
  //
  {
    .description = "VFO for 1024px Screens",
    .width = 700,
    .height = 82,
    .size1 = 14,
    .size2 = 24,
    .size3 = 36,

    .vfo_a_l = 5,
    .vfo_a_r = 305,
    .vfo_a_y = 59,
    .vfo_b_l = 425,
    .vfo_b_r = 695,
    .vfo_b_y = 59,

    .mode_x = 5,
    .mode_y = 21,
    .agc_x = 220,
    .agc_y = 21,
    .nr_x = 305,
    .nr_y = 21,
    .nb_x = 345,
    .nb_y = 21,
    .anf_x = 385,
    .anf_y = 21,
    .snb_x = 425,
    .snb_y = 21,
    .div_x = 460,
    .div_y = 21,
    .eq_x = 500,
    .eq_y = 21,
    .cat_x = 540,
    .cat_y = 21,

    .cmpr_x = 305,
    .cmpr_y = 40,
    .ps_x = 425,
    .ps_y = 78,
    .dexp_x = 385,
    .dexp_y = 40,

    .vox_x = 305,
    .vox_y = 59,
    .dup_x = 385,
    .dup_y = 59,

    .lock_x = 5,
    .lock_y = 78,
    .zoom_x = 80,
    .zoom_y = 78,
    .ctun_x = 155,
    .ctun_y = 78,
    .step_x = 220,
    .step_y = 78,
    .split_x = 305,
    .split_y = 78,
    .sat_x = 385,
    .sat_y = 78,
    .rit_x = 460,
    .rit_y = 78,
    .xit_x = 540,
    .xit_y = 78,
    .filter_x = 580,
    .filter_y = 21,
    .multifn_x = 620,
    .multifn_y = 78,
    .lat_x = 5,
    .lat_y = 35
  },

//MISSING: something about 610 pix, Layout(530) scaled with 1.15

  {
    .description = "VFO for 832px Screens",
    .width = 530,
    .height = 80,
    .size1 = 13,
    .size2 = 17,
    .size3 = 32,

    .vfo_a_l = 5,
    .vfo_a_r = 240,
    .vfo_a_y = 54,
    .vfo_b_l = 310,
    .vfo_b_r = 530,
    .vfo_b_y = 54,

    .mode_x = 5,
    .mode_y = 16,
    .agc_x = 175,
    .agc_y = 15,
    .nr_x = 240,
    .nr_y = 15,
    .nb_x = 282,
    .nb_y = 15,
    .anf_x = 310,
    .anf_y = 15,
    .snb_x = 340,
    .snb_y = 15,
    .div_x = 385,
    .div_y = 15,
    .cmpr_x = 460,
    .cmpr_y = 16,
    .cat_x = 420,
    .cat_y = 16,

    .eq_x = 240,
    .eq_y = 36,
    .ps_x = 280,
    .ps_y = 36,

    .vox_x = 240,
    .vox_y = 54,
    .dup_x = 282,
    .dup_y = 54,

    .lock_x = 5,
    .lock_y = 72,
    .zoom_x = 60,
    .zoom_y = 72,
    .ctun_x = 120,
    .ctun_y = 72,
    .step_x = 160,
    .step_y = 72,
    .split_x = 240,
    .split_y = 72,
    .sat_x = 282,
    .sat_y = 72,
    .rit_x = 310,
    .rit_y = 72,
    .xit_x = 385,
    .xit_y = 72,
    .filter_x = 0,
    .multifn_x = 460,
    .multifn_y = 72,
    .dexp_x = 0,
    .dexp_y = 0,
    .lat_x = 5,
    .lat_y = 32
  },

  // MISSING: something about 444 pix, Layout(370) scaled with 1.2

  //
  // This is for those who want to run piHPDSR on a 640x480 screen
  //
  {
    .description = "VFO for SMALL Screens",
    .width = 370,
    .height = 84,
    .size1 = 13,
    .size2 = 18,
    .size3 = 24,
    .vfo_a_l = 5,
    .vfo_a_r = 185,
    .vfo_a_y = 41,
    .vfo_b_l = 200,
    .vfo_b_r = 370,
    .vfo_b_y = 41,
    .mode_x = 5,
    .mode_y = 15,
    .zoom_x = 65,
    .zoom_y = 54,
    .ps_x = 5,
    .ps_y = 68,
    .rit_x = 170,
    .rit_y = 15,
    .xit_x = 260,
    .xit_y = 15,
    .nb_x = 35,
    .nb_y = 82,
    .nr_x = 5,
    .nr_y = 82,
    .anf_x = 65,
    .anf_y = 82,
    .snb_x = 95,
    .snb_y = 82,
    .agc_x = 140,
    .agc_y = 82,
    .cmpr_x = 65,
    .cmpr_y = 68,
    .eq_x = 140,
    .eq_y = 68,
    .div_x = 35,
    .div_y = 68,
    .step_x = 215,
    .step_y = 82,
    .ctun_x = 215,
    .ctun_y = 68,
    .cat_x = 260,
    .cat_y = 54,
    .vox_x = 260,
    .vox_y = 68,
    .lock_x = 5,
    .lock_y = 54,
    .split_x = 170,
    .split_y = 54,
    .sat_x = 140,
    .sat_y = 54,
    .dup_x = 215,
    .dup_y = 54,
    .filter_x = 0,
    .multifn_x = 300,
    .multifn_y = 82,
    .dexp_x = 0,
    .dexp_y = 0,
    .lat_x = 5,
    .lat_y = 27
  },
  //
  // The last "layout" must have a negative width to
  // mark the end of the list
  //
  {
    .width = -1
  }
};
