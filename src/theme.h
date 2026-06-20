/* Copyright (C)
*  2026 - piHPSDR Modernisation, contribution from AL
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
 *
 * Provides 12 named themes selectable at runtime.
 * GTK buttons,  backgrounds etc. are NOT overridden.
 *
 */

#ifndef _THEME_H_
#define _THEME_H_

#include <gtk/gtk.h>

#define TC(r,g,b,a) {(r),(g),(b),(a)}

typedef struct {
  const char *name;
  float pan_backgnd[4];
  float vfo_backgnd[4];
  float pan_line1[4];   // active RX / TX spectrum line (non-gradient)
  float pan_line2[4];   // inactive RX spectrum line
  float pan_fill1[4];   // active RX / TX spectrum fill
  float pan_fill2[4];   // inactive RX spectrum fill
  float pan_filter[4];  // PAN filter area
  float pan_line[4];    // panadapter lines, active
  float pan_linew[4];   // panadapter lines, non-active
  float pan_text[4];    // text on the panadapter
  float pan_notch[4];   // notched area
  float pan_60m[4];     // 60m channels
  float shade[4];
  float meter[4];
  float ok[4];
  float okw[4];
  float attn[4];
  float attnw[4];
  float alarm[4];
  float alarmw[4];
  float grad1[4];   // low signal (green)
  float grad2[4];   // mid-low   (amber)
  float grad3[4];   // mid-high  (yellow)
  float grad4[4];   // peak      (red)
  float grad1w[4];  // weak versions
  float grad2w[4];
  float grad3w[4];
  float grad4w[4];
  float dxspot[4]; // how to mark DX cluster spots on the panadapter
} THEME;

extern const THEME themes[];
extern const int   num_themes;
extern int         active_theme_index;
extern int         gtk_dark_theme;

const THEME *theme_get_active(void);
void theme_set(void);

//
// COLOUR defines that used to be in appearance.h
//
#define COLOUR_GRAD1       theme_get_active()->grad1[0], theme_get_active()->grad1[1], theme_get_active()->grad1[2], theme_get_active()->grad1[3]
#define COLOUR_GRAD2       theme_get_active()->grad2[0], theme_get_active()->grad2[1], theme_get_active()->grad2[2], theme_get_active()->grad2[3]
#define COLOUR_GRAD3       theme_get_active()->grad3[0], theme_get_active()->grad3[1], theme_get_active()->grad3[2], theme_get_active()->grad3[3]
#define COLOUR_GRAD4       theme_get_active()->grad4[0], theme_get_active()->grad4[1], theme_get_active()->grad4[2], theme_get_active()->grad4[3]
#define COLOUR_GRAD1_WEAK  theme_get_active()->grad1w[0], theme_get_active()->grad1w[1], theme_get_active()->grad1w[2], theme_get_active()->grad1w[3]
#define COLOUR_GRAD2_WEAK  theme_get_active()->grad2w[0], theme_get_active()->grad2w[1], theme_get_active()->grad2w[2], theme_get_active()->grad2w[3]
#define COLOUR_GRAD3_WEAK  theme_get_active()->grad3w[0], theme_get_active()->grad3w[1], theme_get_active()->grad3w[2], theme_get_active()->grad3w[3]
#define COLOUR_GRAD4_WEAK  theme_get_active()->grad4w[0], theme_get_active()->grad4w[1], theme_get_active()->grad4w[2], theme_get_active()->grad4w[3]
#define COLOUR_PAN_BACKGND theme_get_active()->pan_backgnd[0], theme_get_active()->pan_backgnd[1], theme_get_active()->pan_backgnd[2], theme_get_active()->pan_backgnd[3]
#define COLOUR_VFO_BACKGND theme_get_active()->vfo_backgnd[0], theme_get_active()->vfo_backgnd[1], theme_get_active()->vfo_backgnd[2], theme_get_active()->vfo_backgnd[3]
#define COLOUR_PAN_LINE1   theme_get_active()->pan_line1[0], theme_get_active()->pan_line1[1], theme_get_active()->pan_line1[2], theme_get_active()->pan_line1[3]
#define COLOUR_PAN_LINE2   theme_get_active()->pan_line2[0], theme_get_active()->pan_line2[1], theme_get_active()->pan_line2[2], theme_get_active()->pan_line2[3]
#define COLOUR_PAN_FILL1   theme_get_active()->pan_fill1[0], theme_get_active()->pan_fill1[1], theme_get_active()->pan_fill1[2], theme_get_active()->pan_fill1[3]
#define COLOUR_PAN_FILL2   theme_get_active()->pan_fill2[0], theme_get_active()->pan_fill2[1], theme_get_active()->pan_fill2[2], theme_get_active()->pan_fill2[3]
#define COLOUR_PAN_FILTER  theme_get_active()->pan_filter[0], theme_get_active()->pan_filter[1], theme_get_active()->pan_filter[2], theme_get_active()->pan_filter[3]
#define COLOUR_PAN_LINE    theme_get_active()->pan_line[0], theme_get_active()->pan_line[1], theme_get_active()->pan_line[2], theme_get_active()->pan_line[3]
#define COLOUR_PAN_LINE_WEAK theme_get_active()->pan_linew[0], theme_get_active()->pan_linew[1], theme_get_active()->pan_linew[2], theme_get_active()->pan_linew[3]
#define COLOUR_PAN_TEXT    theme_get_active()->pan_text[0], theme_get_active()->pan_text[1], theme_get_active()->pan_text[2], theme_get_active()->pan_text[3]
#define COLOUR_PAN_NOTCH   theme_get_active()->pan_notch[0], theme_get_active()->pan_notch[1], theme_get_active()->pan_notch[2], theme_get_active()->pan_notch[3]
#define COLOUR_PAN_60M     theme_get_active()->pan_60m[0], theme_get_active()->pan_60m[1], theme_get_active()->pan_60m[2], theme_get_active()->pan_60m[3]
#define COLOUR_SHADE       theme_get_active()->shade[0],  theme_get_active()->shade[1],  theme_get_active()->shade[2],  theme_get_active()->shade[3]
#define COLOUR_METER       theme_get_active()->meter[0],  theme_get_active()->meter[1],  theme_get_active()->meter[2],  theme_get_active()->meter[3]
#define COLOUR_OK          theme_get_active()->ok[0],     theme_get_active()->ok[1],     theme_get_active()->ok[2],     theme_get_active()->ok[3]
#define COLOUR_OK_WEAK     theme_get_active()->okw[0],     theme_get_active()->okw[1],     theme_get_active()->okw[2],     theme_get_active()->okw[3]
#define COLOUR_ATTN        theme_get_active()->attn[0],   theme_get_active()->attn[1],   theme_get_active()->attn[2],   theme_get_active()->attn[3]
#define COLOUR_ATTN_WEAK   theme_get_active()->attnw[0],   theme_get_active()->attnw[1],   theme_get_active()->attnw[2],   theme_get_active()->attnw[3]
#define COLOUR_ALARM       theme_get_active()->alarm[0],  theme_get_active()->alarm[1],  theme_get_active()->alarm[2],  theme_get_active()->alarm[3]
#define COLOUR_ALARM_WEAK  theme_get_active()->alarmw[0],  theme_get_active()->alarmw[1],  theme_get_active()->alarmw[2],  theme_get_active()->alarmw[3]
#define COLOUR_DXSPOT      theme_get_active()->dxspot[0],  theme_get_active()->dxspot[1],  theme_get_active()->dxspot[2],  theme_get_active()->dxspot[3]

#endif
