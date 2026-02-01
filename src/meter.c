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
#include <math.h>

#include "appearance.h"
#include "band.h"
#include "meter.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "radio.h"
#include "receiver.h"
#include "version.h"
#include "vfo.h"
#include "vox.h"

static GtkWidget *meter;
static cairo_surface_t *meter_surface = NULL;

static int last_meter_type = SMETER;

#define min_rxlvl -200.0
#define min_alc   -100.0
#define min_pwr      0.0

static double max_rxlvl = min_rxlvl;
static double max_alc   = min_alc;
static double max_pwr   = min_pwr;

static int max_count = 0;

static gboolean
meter_configure_event_cb (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data) {
  if (meter_surface) {
    cairo_surface_destroy (meter_surface);
  }

  meter_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                  CAIRO_CONTENT_COLOR, METER_WIDTH, VFO_HEIGHT);
  /* Initialise the surface to black */
  cairo_t *cr;
  cr = cairo_create (meter_surface);
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_paint (cr);
  cairo_destroy (cr);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
meter_draw_cb (GtkWidget *widget, cairo_t   *cr, gpointer   data) {
  cairo_set_source_surface (cr, meter_surface, 0.0, 0.0);
  cairo_paint (cr);
  return FALSE;
}

// cppcheck-suppress constParameterCallback
static gboolean meter_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  start_meter_menu();
  return TRUE;
}

GtkWidget* meter_init(int width, int height) {
  t_print("%s: width=%d height=%d\n", __func__, width, height);
  meter = gtk_drawing_area_new ();
  gtk_widget_set_size_request (meter, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (meter, "draw",
                    G_CALLBACK (meter_draw_cb), NULL);
  g_signal_connect (meter, "configure-event",
                    G_CALLBACK (meter_configure_event_cb), NULL);
  /* Event signals */
  g_signal_connect (meter, "button-press-event",
                    G_CALLBACK (meter_press_event_cb), NULL);
  gtk_widget_set_events (meter, gtk_widget_get_events (meter)
                         | GDK_BUTTON_PRESS_MASK);
  return meter;
}

void meter_update(RECEIVER *rx, int meter_type, double value, double alc, double swr) {
  double rxlvl;   // only used for RX input level, clones "value"
  double pwr;     // only used for TX power, clones "value"
  char sf[32];
  cairo_t *cr = cairo_create (meter_surface);
  int txvfo = vfo_get_tx_vfo();
  int txmode = vfo[txvfo].mode;
  int cwmode = (txmode == modeCWU || txmode == modeCWL);
  const BAND *band = band_get_band(vfo[txvfo].band);
  double smtr = 0.0, smtr2;

  //
  // First, do all the work that  does not depend on whether the
  // meter is analog or digital.
  //
  if (last_meter_type != meter_type) {
    last_meter_type = meter_type;
    //
    // reset max values
    //
    max_rxlvl = min_rxlvl;
    max_pwr   = min_pwr;
    max_alc   = min_alc;
    max_count =    0;
  }

  //
  // Only the values max_rxlvl/max_pwr/max_alc are "on display"
  // The algorithm to calculate these "sedated" values from the
  // (possibly fluctuating)  input ones is as follows:
  //
  // - if counter > CNTMAX then move max_value towards current_value by exponential averaging
  //                            with parameter EXPAV1, EXPAV2 (but never go below the minimum value)
  // - if current_value >  max_value then set max_value to current_value and reset counter
  //
  // A new max value will therefore be immediately visible, the display stays (if not surpassed) for
  // CNTMAX cycles and then the displayed value will gradually approach the new one(s).
#define CNTMAX 5
#define EXPAV1 0.75
#define EXPAV2 0.25

  switch (meter_type) {
  case POWER:
    pwr = value;

    if (max_count > CNTMAX) {
      max_pwr = EXPAV1 * max_pwr + EXPAV2 * pwr;
      max_alc = EXPAV1 * max_alc + EXPAV2 * alc;

      // This is perhaps not necessary ...
      if (max_pwr < min_pwr) { max_pwr = min_pwr; }

      // ... but alc goes to -Infinity during CW
      if (max_alc < min_alc) { max_alc = min_alc; }
    }

    if (pwr > max_pwr) {
      max_pwr = pwr;
      max_count = 0;
    }

    if (alc > max_alc) {
      max_alc = alc;
      max_count = 0;
    }

    break;

  case SMETER:
    rxlvl = value; // all corrections now in receiver.c

    if (max_count > CNTMAX) {
      max_rxlvl = EXPAV1 * max_rxlvl + EXPAV2 * rxlvl;

      if (max_rxlvl < min_rxlvl) { max_rxlvl = min_rxlvl; }
    }

    if (rxlvl > max_rxlvl) {
      max_rxlvl = rxlvl;
      max_count = 0;
    }
    //
    // Calculate S-meter reflection smtr
    // smtr : goes from  0.0 to  72.0 for S0 ... S9
    //        and  from 72.0 to 114.0 for S9 ... S9+60
    //
    if (vfo[active_receiver->id].frequency > 30000000LL) {
      if (smeter3dB) {
        smtr = 2.66666666 * (fmax(-120.0, max_rxlvl) + 120.0);
      } else {
        smtr = 1.33333333 * (fmax(-147.0, max_rxlvl) + 147.0);
      }
      smtr2 = 0.7 * (max_rxlvl + 93.0);
    } else {
      if (smeter3dB) {
        smtr = 2.66666666 * (fmax(-100.0, max_rxlvl) + 100.0);
      } else {
        smtr = 1.33333333 * (fmax(-127.0, max_rxlvl) + 127.0);
      }
      smtr2 = 0.7 * (max_rxlvl + 73);
    }

    if (smtr > 72.0) { smtr = 72.0; }
    if (smtr2 <  0.0) { smtr2 =  0.0; }
    if (smtr2 > 42.0) { smtr2 = 42.0; }

    smtr += smtr2;
    break;
  }

  max_count++;

  //
  // From now on, DO NOT USE rxlvl,pwr,alc but use max_rxlvl etc.
  //
  if (analog_meter) {
    cairo_text_extents_t extents;
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    switch (meter_type) {
    case SMETER: {
      //
      // Analog RX display
      //
      int i;
      double x;
      double y;
      double angle;
      double radians;
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 25.0;
      double min_angle, max_angle, bydb;

      if (cx - 0.342 * radius < VFO_HEIGHT - 5) {
        min_angle = 200.0;
        max_angle = 340.0;
      } else if (cx - 0.5 * radius < VFO_HEIGHT - 5) {
        min_angle = 210.0;
        max_angle = 330.0;
      } else {
        min_angle = 220.0;
        max_angle = 320.0;
      }

      bydb = (max_angle - min_angle) / 114.0;
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      // This arc encompasses the whole scale from S0 to S9+60
      cairo_arc(cr, cx, cx, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      // This arc encompasses the scale from S9 to S9+60
      cairo_arc(cr, cx, cx, radius + 2, (min_angle + 72.0 * bydb) * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      for (i = 1; i < 10; i++) {
        angle = ((double)i * 8.0 * bydb) + min_angle;
        radians = angle * M_PI / 180.0;

        if ((i % 2) == 1) {
          // major ticks with label at S1, S3, S5, S7, S9
          cairo_arc(cr, cx, cx, radius + 4, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
          snprintf(sf, sizeof(sf), "%d", i);
          cairo_text_extents(cr, sf, &extents);
          cairo_arc(cr, cx, cx, radius + 5, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_new_path(cr);
          //
          // At x=0, move left the whole width, at x==cx half of the width, and at x=2*cx do not move
          //
          x += extents.width * (x / (2.0 * cx) - 1.0);
          cairo_move_to(cr, x, y);
          cairo_show_text(cr, sf);
        } else {
          // minor ticks without label at S2, S4, S6, S8
          cairo_arc(cr, cx, cx, radius + 2, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
        }

        cairo_new_path(cr);
      }

      for (i = 1; i <= 3; i++) {
        // major tick with label +20/+40/+60
        angle = bydb * (double)(14 * i + 72) + min_angle;
        radians = angle * M_PI / 180.0;
        cairo_arc(cr, cx, cx, radius + 4, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_arc(cr, cx, cx, radius, radians, radians);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
        snprintf(sf, sizeof(sf), "+%d", 20 * i);
        cairo_text_extents(cr, sf, &extents);
        cairo_arc(cr, cx, cx, radius + 5, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_new_path(cr);
        x += extents.width * (x / (2.0 * cx) - 1.0);
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, sf);
        cairo_new_path(cr);
      }

      // draw "needle"
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_set_source_rgba(cr, COLOUR_METER);
      angle = min_angle + smtr * bydb;
      radians = angle * M_PI / 180.0;
      cairo_arc(cr, cx, cx, radius + 8, radians, radians);
      cairo_line_to(cr, cx, cx);
      cairo_stroke(cr);
      // write level as dBm in text form
      cairo_set_source_rgba(cr, COLOUR_METER);
      snprintf(sf, sizeof(sf), "%d dBm", (int)(max_rxlvl - 0.5)); // assume max_rxlvl < 0 in roundig

      if (METER_WIDTH < 210) {
        cairo_set_font_size(cr, 16);
        cairo_move_to(cr, cx - 32, cx - radius + 30);
      } else {
        cairo_set_font_size(cr, 20);
        cairo_move_to(cr, cx - 40, cx - radius + 34);
      }

      cairo_show_text(cr, sf);
    }
    break;

    case POWER: {
      //
      // Analog TX display
      //
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 25.0;

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // There is now Fwd/SWR data for Soapy radios
        //
        int  units;          // 1: x.y W, 2: xxx W
        double interval;     // 1/10 of full reflection
        double angle;
        double radians;
        double min_angle, max_angle;
        double x;
        double y;

        if (band->disablePA || !pa_enabled) {
          units = 1;
          interval = 0.1;
        } else {
          int pp = pa_power_list[pa_power];
          units = (pp <= 1) ? 1 : 2;
          interval = 0.1 * pp;
        }

        if (cx - 0.342 * radius < VFO_HEIGHT - 5) {
          min_angle = 200.0;
          max_angle = 340.0;
        } else if (cx - 0.5 * radius < VFO_HEIGHT - 5) {
          min_angle = 210.0;
          max_angle = 330.0;
        } else {
          min_angle = 220.0;
          max_angle = 320.0;
        }

        cairo_set_line_width(cr, PAN_LINE_THICK);
        cairo_set_source_rgba(cr, COLOUR_METER);
        cairo_arc(cr, cx, cx, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
        cairo_stroke(cr);
        cairo_set_line_width(cr, PAN_LINE_THICK);
        cairo_set_source_rgba(cr, COLOUR_METER);

        for (int i = 0; i <= 100; i++) {
          angle = (double)i * 0.01 * max_angle + (double)(100 - i) * 0.01 * min_angle;
          radians = angle * M_PI / 180.0;

          if ((i % 10) == 0) {
            cairo_arc(cr, cx, cx, radius + 4, radians, radians);
            cairo_get_current_point(cr, &x, &y);
            cairo_arc(cr, cx, cx, radius, radians, radians);
            cairo_line_to(cr, x, y);
            cairo_stroke(cr);

            if ((i % 20) == 0) {
              switch (units) {
              case 1:
                snprintf(sf, sizeof(sf), "%0.1f", 0.1 * interval * i);
                break;

              case 2: {
                int p = (int) (0.1 * interval * i);

                // "1000" overwrites the right margin, replace by "1K"
                if (p == 1000) {
                  snprintf(sf, sizeof(sf), "1K");
                } else {
                  snprintf(sf, sizeof(sf), "%d", p);
                }
              }
              break;
              }

              cairo_text_extents(cr, sf, &extents);
              cairo_arc(cr, cx, cx, radius + 5, radians, radians);
              cairo_get_current_point(cr, &x, &y);
              cairo_new_path(cr);
              x += extents.width * (x / (2.0 * cx) - 1.0);
              cairo_move_to(cr, x, y);
              cairo_show_text(cr, sf);
            }
          }

          cairo_new_path(cr);
        }

        cairo_set_line_width(cr, PAN_LINE_EXTRA);
        cairo_set_source_rgba(cr, COLOUR_METER);
        angle = max_pwr * (max_angle - min_angle) / (10.0 * interval) + min_angle;

        if (angle > max_angle + 5) { angle = max_angle + 5; }

        radians = angle * M_PI / 180.0;
        cairo_arc(cr, cx, cx, radius + 8, radians, radians);
        cairo_line_to(cr, cx, cx);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, COLOUR_METER);

        switch (pa_power) {
        case PA_1W:
          snprintf(sf, sizeof(sf), "%dmW", (int)(1000.0 * max_pwr + 0.5));
          break;

        case PA_5W:
        case PA_10W:
          snprintf(sf, sizeof(sf), "%0.1fW", max_pwr);
          break;

        default:
          snprintf(sf, sizeof(sf), "%dW", (int)(max_pwr + 0.5));
          break;
        }

        cairo_move_to(cr, cx - 20, cx - radius + 15);
        cairo_show_text(cr, sf);

        if (can_transmit) {
          if (swr > transmitter->swr_alarm) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
          } else {
            cairo_set_source_rgba(cr, COLOUR_METER); // display SWR in white color
          }
        }

        snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
        cairo_move_to(cr, cx - 40, cx - radius + 28);
        cairo_show_text(cr, sf);
      }

      if (!cwmode) {
        cairo_set_source_rgba(cr, COLOUR_METER);
        snprintf(sf, sizeof(sf), "ALC %2.1f dB", max_alc);
        cairo_move_to(cr, cx - 40, cx - radius + 41);
        cairo_show_text(cr, sf);
      }
    }
    break;
    }

    //
    // The mic level meter is not shown in CW modes,
    // otherwise it is always shown while TX, and shown
    // during RX only if VOX is enabled.
    //
    // The MicLvl is now in dB, the scale going from -40 to 0 dB.
    //
    if (((meter_type == POWER) || vox_enabled) && !cwmode) {
      double offset = ((double)METER_WIDTH - 100.0) / 2.0;
      double peak = vox_get_peak();

      //
      // Map peak value to a scale -40 ... 0 dB.
      // 21.714 = 50 / ln(10)
      //
      if (peak > 1.0) {
        peak = 100.0;
      } else if (peak < 0.01) {
        peak = 0.0;
      } else {
        peak = 21.714 * log(peak) + 100.0; //  mapped to 0 .. 100
      }

      //
      // 92.50 = -3.0 dB, Amplitude 0.71
      // 98.75 = -0.5 dB, Amplitude 0.94
      //
      if (peak < 92.5) {
        cairo_set_source_rgba(cr, COLOUR_OK);
      } else if (peak < 98.75) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      } else {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
      }

      cairo_rectangle(cr, offset, 0.0, peak, 5.0);
      cairo_fill(cr);
      cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_move_to(cr, offset + 105.0, 10.0);
      cairo_show_text(cr, "Mic Lvl");
      cairo_move_to(cr, offset, 0.0);
      cairo_line_to(cr, offset, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset + 50.0, 0.0);
      cairo_line_to(cr, offset + 50.0, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset + 100.0, 0.0);
      cairo_line_to(cr, offset + 100.0, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset, 5.0);
      cairo_line_to(cr, offset + 100.0, 5.0);
      cairo_stroke(cr);

      if (vox_enabled) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, offset + (vox_threshold * 100.0), 0.0);
        cairo_line_to(cr, offset + (vox_threshold * 100.0), 5.0);
        cairo_stroke(cr);
      }
    }
  } else {
    //
    // Digital meter, both RX and TX:
    // Mic level display
    //
    int text_location;
    int Y0 = (VFO_HEIGHT - 65) / 2;
    int Y1 = Y0 + 15;
    int Y2 = Y1 + 24;
    int Y4 = Y2 + 24;
    int size;
    cairo_text_extents_t extents;
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_line_width(cr, PAN_LINE_THICK);

    if (((meter_type == POWER) || vox_enabled) && !cwmode) {
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_move_to(cr, 5.0, Y1);
      cairo_line_to(cr, 5.0, Y1 - 10);
      cairo_move_to(cr, 5.0 + 25.0, Y1);
      cairo_line_to(cr, 5.0 + 25.0, Y1 - 5);
      cairo_move_to(cr, 5.0 + 50.0, Y1);
      cairo_line_to(cr, 5.0 + 50.0, Y1 - 10);
      cairo_move_to(cr, 5.0 + 75.0, Y1);
      cairo_line_to(cr, 5.0 + 75.0, Y1 - 5);
      cairo_move_to(cr, 5.0 + 100.0, Y1);
      cairo_line_to(cr, 5.0 + 100.0, Y1 - 10);
      cairo_stroke(cr);
      double peak = vox_get_peak();

      //
      // Map peak value to a scale -40 ... 0 dB.
      // 21.714 = 50 / ln(10)
      //
      if (peak > 1.0) {
        peak = 100.0;
      } else if (peak < 0.01) {
        peak = 0.0;
      } else {
        peak = 21.714 * log(peak) + 100.0; //  mapped to 0 .. 100
      }

      //
      // 92.50 = -3.0 dB, Amplitude 0.71
      // 98.75 = -0.5 dB, Amplitude 0.94
      //
      if (peak < 92.5) {
        cairo_set_source_rgba(cr, COLOUR_OK);
      } else if (peak < 98.75) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      } else {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
      }

      cairo_rectangle(cr, 5.0, Y1 - 10, peak, 5);
      cairo_fill(cr);

      if (vox_enabled) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, 5.0 + (vox_threshold * 100.0), Y1 - 10);
        cairo_line_to(cr, 5.0 + (vox_threshold * 100.0), Y1);
        cairo_stroke(cr);
      }

      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      cairo_move_to(cr, 110.0, Y1);
      cairo_show_text(cr, "Mic Lvl");
    }

    cairo_set_source_rgba(cr, COLOUR_METER);

    switch (meter_type) {
    case SMETER:
      //
      // Digital meter, RX
      //
      // value is dBm
      text_location = 10;

      if (METER_WIDTH >= 150) {
        int i;
        cairo_set_line_width(cr, PAN_LINE_THICK);
        cairo_set_source_rgba(cr, COLOUR_METER);

        for (i = 0; i < 72; i += 8) {
          cairo_move_to(cr, 5 + i, Y4 - 10);

          if (i % 24 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 20);
          } else if (i % 8 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 15);
          }
        }

        cairo_stroke(cr);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_move_to(cr, 25, Y4);
        cairo_show_text(cr, "3");
        cairo_move_to(cr, 49, Y4);
        cairo_show_text(cr, "6");
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, 5 + 72, Y4 - 10);
        cairo_line_to(cr, 5 + 72, Y4 - 20);
        cairo_move_to(cr, 5 + 86, Y4 - 10);
        cairo_line_to(cr, 5 + 86, Y4 - 20);
        cairo_move_to(cr, 5 + 100, Y4 - 10);
        cairo_line_to(cr, 5 + 100, Y4 - 20);
        cairo_move_to(cr, 5 + 114, Y4 - 10);
        cairo_line_to(cr, 5 + 114, Y4 - 20);
        cairo_stroke(cr);
        cairo_move_to(cr, 73, Y4);
        cairo_show_text(cr, "9");
        cairo_move_to(cr, 5 + 80, Y4);
        cairo_show_text(cr, "20");
        cairo_move_to(cr, 5 + 96, Y4);
        cairo_show_text(cr, "40");
        cairo_move_to(cr, 5 + 112, Y4);
        cairo_show_text(cr, "60");
        //
        // use colours from the "gradient" panadapter display,
        // but use no gradient: S0-S9 first colour, beyond S9 last colour
        //
        cairo_pattern_t *pat = cairo_pattern_create_linear(0.0, 0.0, 119.0, 0.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.00000, COLOUR_GRAD1);
        cairo_pattern_add_color_stop_rgba(pat, 0.64706, COLOUR_GRAD1);
        cairo_pattern_add_color_stop_rgba(pat, 0.64706, COLOUR_GRAD4);
        cairo_pattern_add_color_stop_rgba(pat, 1.00000, COLOUR_GRAD4);
        cairo_set_source(cr, pat);
        cairo_rectangle(cr, 5, Y2 - 20, smtr, 20.0);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        //
        // Mark right edge of S-meter bar with a line in ATTN colour
        //
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_move_to(cr, 5 + smtr, (double)Y2);
        cairo_line_to(cr, 5 + smtr, (double)(Y2 - 20));
        cairo_stroke(cr);
        text_location = 124;
      }

      //
      // Compute largest size for the digital S-meter reading such that
      // it fits, both horizontally and vertically
      //
      size = (METER_WIDTH - text_location) / 5;

      if (size > VFO_HEIGHT / 3) { size = VFO_HEIGHT / 3; }

      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_set_font_size(cr, size);
      snprintf(sf, sizeof(sf), "%-3d dBm", (int)(max_rxlvl - 0.5));  // assume max_rxlvl < 0 in rounding
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - extents.width - 5, Y2);
      cairo_show_text(cr, sf);
      break;

    case POWER:
      cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

      if (!cwmode) {
        cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        cairo_set_source_rgba(cr, COLOUR_METER);  // revert to white color
        snprintf(sf, sizeof(sf), "ALC %2.1f dB", max_alc);
        cairo_move_to(cr, METER_WIDTH / 2, Y4);
        cairo_show_text(cr, sf);
      }

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // Power/SWR not available for SOAPY.
        //
        switch (pa_power) {
        case PA_1W:
          snprintf(sf, sizeof(sf), "FWD %dmW", (int)(1000.0 * max_pwr + 0.5));
          break;

        case PA_5W:
        case PA_10W:
          snprintf(sf, sizeof(sf), "FWD %0.1fW", max_pwr);
          break;

        default:
          snprintf(sf, sizeof(sf), "FWD %dW", (int)(max_pwr + 0.5));
          break;
        }

        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_move_to(cr, 5, Y2);
        cairo_show_text(cr, sf);

        if (can_transmit) {
          if (swr > transmitter->swr_alarm) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
          } else {
            cairo_set_source_rgba(cr, COLOUR_OK); // display SWR in white color
          }
        }

        snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
        cairo_move_to(cr, METER_WIDTH / 2, Y2);
        cairo_show_text(cr, sf);
      }

      break;
    }
  }

  //
  // This is the same for analog and digital metering
  //
  cairo_destroy(cr);
  gtk_widget_queue_draw (meter);
}
