/* Copyright (C)
*  2015 - John Melton, G0ORX/N6LYT
*  2025 - Christoph van Wüllen, DL1YCF
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
#include "client_server.h"
#include "meter.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "radio.h"
#include "receiver.h"
#include "theme.h"
#include "version.h"
#include "vfo.h"
#include "vox.h"

static GtkWidget *meter;
static cairo_surface_t *meter_surface = NULL;

//
// rxtxstate "detects" RX/TX transitions
// to reset exponential averaging and peak values
//
static int rxtxstate = 0;

static gboolean meter_configure_event_cb (GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
  if (meter_surface) {
    cairo_surface_destroy (meter_surface);
  }

  meter_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, METER_WIDTH, VFO_HEIGHT);
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
static gboolean meter_draw_cb (GtkWidget *widget, cairo_t   *cr, gpointer   data) {
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
  g_signal_connect (meter, "draw", G_CALLBACK (meter_draw_cb), NULL);
  g_signal_connect (meter, "configure-event", G_CALLBACK (meter_configure_event_cb), NULL);
  /* Event signals */
  g_signal_connect (meter, "button-press-event", G_CALLBACK (meter_press_event_cb), NULL);
  gtk_widget_set_events (meter, gtk_widget_get_events (meter) | GDK_BUTTON_PRESS_MASK);
  return meter;
}

//
// ----------------------------------------------------------------------------
// Additional RX meter styles: edgewise moving-coil and dual-scale bar.
// Selected through analog_meter (0=digital, 1=analog arc, 2=edgewise, 3=dual).
// ----------------------------------------------------------------------------
//
static void meter_zone_rgb(double f, double *r, double *g, double *b) {
  if (f < 0.40) {
    double t = f / 0.40;
    *r = 0.22 + t * (0.55 - 0.22);
    *g = 0.83 - t * (0.83 - 0.80);
    *b = 0.33 - t * 0.33;
  } else if (f < 0.647) {
    double t = (f - 0.40) / 0.247;
    *r = 0.55 + t * (0.941 - 0.55);
    *g = 0.80 - t * (0.80 - 0.647);
    *b = 0.0;
  } else {
    double t = (f - 0.647) / 0.353;
    *r = 0.941 + t * 0.032;
    *g = 0.647 - t * (0.647 - 0.318);
    *b = t * 0.286;
  }
}

static void meter_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
  if (r > 0.5 * h) { r = 0.5 * h; }

  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2.0, 0.0);
  cairo_arc(cr, x + w - r, y + h - r, r,  0.0,         M_PI / 2.0);
  cairo_arc(cr, x + r,     y + h - r, r,  M_PI / 2.0,  M_PI);
  cairo_arc(cr, x + r,     y + r,     r,  M_PI,        1.5 * M_PI);
  cairo_close_path(cr);
}

/* ───────────────────────── Dual-scale bar ───────────────────────── */
static void rxmeter_dualscale(cairo_t *cr, double smtr, int sval, int sval2,
                              double max_rxlvl, double pk, double ref9, double perS) {
  char sf[32];
  cairo_text_extents_t extents;
  double r, g, b;
  const double x0  = (double) ADD_METER_WIDTH;
  const double w   = (double)(METER_WIDTH - ADD_METER_WIDTH);
  const double scalfac = w * 0.00625;            // 1.0 at the 160 px minimum width
  const double bx  = x0 + 22.0 * scalfac;
  const double bw  = w - 36.0 * scalfac;
  const double bh  = 13.0 * scalfac;
  const double by  = VFO_HEIGHT * 0.46;
  const double frac = smtr / 114.0;

  const double pfrac = pk / 114.0;
  cairo_set_line_width(cr, 1.0);
  //
  // Top readout: S-value (left) and dBm (right)
  //
  cairo_set_source_rgba(cr, COLOUR_METER);
  cairo_set_font_size(cr, 13.0 * scalfac);

  if (sval2 > 0) { snprintf(sf, sizeof(sf), "S%d+%d", sval, sval2); }
  else           { snprintf(sf, sizeof(sf), "S%d", sval); }

  cairo_move_to(cr, bx, 15.0 * scalfac);
  cairo_show_text(cr, sf);
  snprintf(sf, sizeof(sf), "%d dBm", (int)(max_rxlvl - 0.5));
  cairo_text_extents(cr, sf, &extents);
  cairo_set_source_rgba(cr, COLOUR_ATTN);
  cairo_move_to(cr, bx + bw - extents.width, 15.0 * scalfac);
  cairo_show_text(cr, sf);
  //
  // dBm tick scale ABOVE the bar (positioned on the S grid so it always
  // agrees with the bar, whatever the band / 3 dB-per-S setting)
  //
  cairo_set_font_size(cr, 8.5 * scalfac);

  for (int s = 1; s <= 9; s += 4) {            // S1 / S5 / S9 only: keeps dBm labels apart
    double f  = (double)(s * 8) / 114.0;
    double tx = bx + f * bw;
    int  dbm  = (int)(ref9 - (9 - s) * perS);
    cairo_set_source_rgba(cr, COLOUR_METER);
    cairo_move_to(cr, tx, by - 3.0 * scalfac);
    cairo_line_to(cr, tx, by - 8.0 * scalfac);
    cairo_stroke(cr);
    snprintf(sf, sizeof(sf), "%d", dbm);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, tx - 0.5 * extents.width, by - 10.0 * scalfac);
    cairo_show_text(cr, sf);
  }

  //
  // Trough
  //
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
  meter_rounded_rect(cr, bx, by, bw, bh, 4.0 * scalfac);
  cairo_fill(cr);
  //
  // Graded fill up to the needle, clipped to the rounded trough
  //
  cairo_save(cr);
  meter_rounded_rect(cr, bx, by, bw, bh, 4.0 * scalfac);
  cairo_clip(cr);
  const int nsteps = 96;

  for (int i = 0; i < nsteps; i++) {
    double f = (double)i / nsteps;

    if (f > frac) { break; }

    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.95);
    cairo_rectangle(cr, bx + f * bw, by, bw / nsteps + 0.6, bh);
    cairo_fill(cr);
  }

  cairo_restore(cr);
  //
  // Peak marker
  //
  cairo_set_source_rgba(cr, COLOUR_METER);
  cairo_set_line_width(cr, 2.0 * scalfac);
  double px = bx + pfrac * bw;
  cairo_move_to(cr, px, by - 2.0 * scalfac);
  cairo_line_to(cr, px, by + bh + 2.0 * scalfac);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 1.0);
  //
  // S-unit ticks + labels BELOW the bar
  //
  cairo_set_font_size(cr, 9.0 * scalfac);

  for (int s = 1; s <= 9; s += 2) {
    double f  = (double)(s * 8) / 114.0;
    double tx = bx + f * bw;
    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.85);
    cairo_move_to(cr, tx, by + bh + 2.0 * scalfac);
    cairo_line_to(cr, tx, by + bh + 7.0 * scalfac);
    cairo_stroke(cr);
    snprintf(sf, sizeof(sf), "%d", s);        // no "S" prefix: the readout already shows it
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, tx - 0.5 * extents.width, by + bh + 17.0 * scalfac);
    cairo_show_text(cr, sf);
  }

  cairo_set_font_size(cr, 8.0 * scalfac);

  for (int k = 1; k <= 3; k++) {
    double f  = (double)(72 + 14 * k) / 114.0;
    double tx = bx + f * bw;
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_move_to(cr, tx, by + bh + 2.0 * scalfac);
    cairo_line_to(cr, tx, by + bh + 7.0 * scalfac);
    cairo_stroke(cr);
    snprintf(sf, sizeof(sf), "+%d", 20 * k);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, tx - 0.5 * extents.width, by + bh + 17.0 * scalfac);
    cairo_show_text(cr, sf);
  }
}

/* ───────────────────── Edgewise moving-coil ───────────────────── */
static void rxmeter_edgewise(cairo_t *cr, double smtr, int sval, int sval2,
                             double max_rxlvl, double pk) {
  char sf[32];
  cairo_text_extents_t extents;
  double r, g, b, x, y, angle, radians;
  const double w   = (double)(METER_WIDTH - ADD_METER_WIDTH);
  const double scalfac = w * 0.00625;
  const double cx  = (w / 2.0) + (double) ADD_METER_WIDTH - 5.0;
  //
  // A far-below pivot gives the shallow "edgewise" curve. The half-span is
  // chosen so the scale fills the available width but is clamped to keep the
  // arc visually flat.
  //
  const double pivot_y = VFO_HEIGHT * 1.95;
  const double radius  = VFO_HEIGHT * 1.58;
  double half = asin(fmin(0.9, ((w / 2.0) - 14.0 * scalfac) / radius)) * 180.0 / M_PI;

  if (half > 30.0) { half = 30.0; }

  const double min_angle = 270.0 - half;
  const double max_angle = 270.0 + half;
  const double bydb = (max_angle - min_angle) / 114.0;
  const double frac = smtr / 114.0;

#if 0
  //
  // Brushed-dark face for a little depth (over the VFO background)
  // deactivated -- colours must be calculated from COLOUR_VFO_BACKGROUND
  //
  {
    cairo_pattern_t *face = cairo_pattern_create_linear(cx, 0, cx, VFO_HEIGHT);
    cairo_pattern_add_color_stop_rgba(face, 0.0, 0.09, 0.11, 0.14, 0.55);
    cairo_pattern_add_color_stop_rgba(face, 1.0, 0.02, 0.03, 0.04, 0.0);
    cairo_set_source(cr, face);
    cairo_rectangle(cr, ADD_METER_WIDTH, 0, w, VFO_HEIGHT);
    cairo_fill(cr);
    cairo_pattern_destroy(face);
  }
#endif
  //
  // Dim full-scale track
  //
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_width(cr, 12.0 * scalfac);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
  cairo_arc(cr, cx, pivot_y, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
  cairo_stroke(cr);
  //
  // Graded fill up to the needle
  //
  {
    const int nsteps = 80;

    for (int i = 0; i < nsteps; i++) {
      double f = (double)i / nsteps;

      if (f > frac) { break; }

      double a1 = (min_angle + f * (max_angle - min_angle)) * M_PI / 180.0;
      double a2 = (min_angle + (f + 1.0 / nsteps) * (max_angle - min_angle)) * M_PI / 180.0;
      meter_zone_rgb(f, &r, &g, &b);
      cairo_set_line_width(cr, 12.0 * scalfac);
      cairo_set_source_rgba(cr, r, g, b, 0.9);
      cairo_arc(cr, cx, pivot_y, radius, a1, a2);
      cairo_stroke(cr);
    }
  }
  //
  // Tick marks + S labels (outside the band, i.e. towards the top of screen)
  //
  cairo_set_line_width(cr, 1.4 * scalfac);
  cairo_set_font_size(cr, 9.0 * scalfac);

  for (int s = 1; s <= 9; s++) {
    angle   = min_angle + (double)(s * 8) * bydb;
    radians = angle * M_PI / 180.0;
    double f = (double)(s * 8) / 114.0;
    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.8);
    double ri = radius + 7.0 * scalfac;
    double ro = radius + (s % 2 ? 14.0 : 11.0) * scalfac;
    cairo_move_to(cr, cx + ri * cos(radians), pivot_y + ri * sin(radians));
    cairo_line_to(cr, cx + ro * cos(radians), pivot_y + ro * sin(radians));
    cairo_stroke(cr);

    if (s % 2) {
      snprintf(sf, sizeof(sf), "%d", s);
      cairo_text_extents(cr, sf, &extents);
      x = cx + (ro + 8.0 * scalfac) * cos(radians);
      y = pivot_y + (ro + 8.0 * scalfac) * sin(radians);
      cairo_move_to(cr, x - 0.5 * extents.width, y + 0.35 * extents.height);
      cairo_show_text(cr, sf);
    }
  }

  //
  // Red over-S9 ticks (+20/+40/+60)
  //
  cairo_set_source_rgba(cr, COLOUR_ALARM);
  cairo_set_font_size(cr, 8.0 * scalfac);

  for (int k = 1; k <= 3; k++) {
    angle   = min_angle + (double)(72 + 14 * k) * bydb;
    radians = angle * M_PI / 180.0;
    double ri = radius + 7.0 * scalfac;
    double ro = radius + 13.0 * scalfac;
    cairo_move_to(cr, cx + ri * cos(radians), pivot_y + ri * sin(radians));
    cairo_line_to(cr, cx + ro * cos(radians), pivot_y + ro * sin(radians));
    cairo_stroke(cr);
    snprintf(sf, sizeof(sf), "+%d", 20 * k);
    cairo_text_extents(cr, sf, &extents);
    x = cx + (ro + 7.0 * scalfac) * cos(radians);
    y = pivot_y + (ro + 7.0 * scalfac) * sin(radians);
    cairo_move_to(cr, x - 0.5 * extents.width, y + 0.35 * extents.height);
    cairo_show_text(cr, sf);
  }

  //
  // White peak-hold needle: as long as live needle
  //
  angle   = min_angle + pk * bydb;
  radians = angle * M_PI / 180.0;
  double cosr = cos(radians);  // to be re-used
  double sinr = sin(radians);  // to be re-used
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width(cr, 1.4 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr,
                pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr,
                pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  //
  // Live needle (short, stubby), coloured by zone
  //
  angle   = min_angle + smtr * bydb;
  radians = angle * M_PI / 180.0;
  cosr = cos(radians);  // to be re-used
  sinr = sin(radians);  // to be re-used
  meter_zone_rgb(frac, &r, &g, &b);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgba(cr, r, g, b, 0.25);          // glow
  cairo_set_line_width(cr, 4.0 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr,
                pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr,
                pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  cairo_set_source_rgba(cr, r, g, b, 0.96);          // needle
  cairo_set_line_width(cr, 2.2 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr,
                pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr,
                pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  //
  // Readouts: S-value bottom-left, dBm bottom-right
  //
  cairo_set_source_rgba(cr, COLOUR_METER);
  cairo_set_font_size(cr, 15.0 * scalfac);

  if (sval2 > 0) { snprintf(sf, sizeof(sf), "S%d+%d", sval, sval2); }
  else           { snprintf(sf, sizeof(sf), "S%d", sval); }

  cairo_move_to(cr, ADD_METER_WIDTH + 6.0 * scalfac, VFO_HEIGHT - 6.0 * scalfac);
  cairo_show_text(cr, sf);
  cairo_set_source_rgba(cr, COLOUR_ATTN);
  cairo_set_font_size(cr, 12.0 * scalfac);
  snprintf(sf, sizeof(sf), "%d dBm", (int)(max_rxlvl - 0.5));
  cairo_text_extents(cr, sf, &extents);
  cairo_move_to(cr, METER_WIDTH - 6.0 * scalfac - extents.width, VFO_HEIGHT - 6.0 * scalfac);
  cairo_show_text(cr, sf);
}

/* ───────────────────── Edgewise TX power meter ───────────────────── */
static void txmeter_edgewise(cairo_t *cr, double frac, double pk, const char *pwrstr,
                             double swr, int swr_alarm, double interval, int units,
                             double alc, int cwmode) {
  char sf[32];
  cairo_text_extents_t extents;
  double r, g, b, x, y, angle, radians;
  const double w   = (double)(METER_WIDTH - ADD_METER_WIDTH);
  const double scalfac = w * 0.00625;
  const double cx  = (w / 2.0) + (double) ADD_METER_WIDTH;
  const double pivot_y = VFO_HEIGHT * 1.95;
  const double radius  = VFO_HEIGHT * 1.58;
  double half = asin(fmin(0.9, (0.37 * w / radius))) * 180.0 / M_PI;

  if (half > 30.0) { half = 30.0; }

  const double min_angle = 270.0 - half;
  const double max_angle = 270.0 + half;

#if 0
  //
  // Brushed-dark face
  // deactivated -- colours must be calculated from COLOUR_VFO_BACKGROUND
  //
  {
    cairo_pattern_t *face = cairo_pattern_create_linear(cx, 0, cx, VFO_HEIGHT);
    cairo_pattern_add_color_stop_rgba(face, 0.0, 0.09, 0.11, 0.14, 0.55);
    cairo_pattern_add_color_stop_rgba(face, 1.0, 0.02, 0.03, 0.04, 0.0);
    cairo_set_source(cr, face);
    cairo_rectangle(cr, ADD_METER_WIDTH, 0, w, VFO_HEIGHT);
    cairo_fill(cr);
    cairo_pattern_destroy(face);
  }
#endif
  //
  // Dim track
  //
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_width(cr, 12.0 * scalfac);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
  cairo_arc(cr, cx, pivot_y, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
  cairo_stroke(cr);
  //
  // Graded fill up to the needle
  //
  {
    const int nsteps = 80;

    for (int i = 0; i < nsteps; i++) {
      double f = (double)i / nsteps;

      if (f > frac) { break; }

      double a1 = (min_angle + f * (max_angle - min_angle)) * M_PI / 180.0;
      double a2 = (min_angle + (f + 1.0 / nsteps) * (max_angle - min_angle)) * M_PI / 180.0;
      meter_zone_rgb(f, &r, &g, &b);
      cairo_set_line_width(cr, 12.0 * scalfac);
      cairo_set_source_rgba(cr, r, g, b, 0.9);
      cairo_arc(cr, cx, pivot_y, radius, a1, a2);
      cairo_stroke(cr);
    }
  }
  //
  // Tick marks + power labels (0 .. full scale)
  //
  cairo_set_line_width(cr, 1.4 * scalfac);
  cairo_set_font_size(cr, 8.0 * scalfac);

  for (int t = 0; t <= 10; t++) {
    double f = (double)t / 10.0;
    angle   = min_angle + f * (max_angle - min_angle);
    radians = angle * M_PI / 180.0;
    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.8);
    double ri = radius + 7.0 * scalfac;
    double ro = radius + ((t % 2 == 0) ? 14.0 : 11.0) * scalfac;
    cairo_move_to(cr, cx + ri * cos(radians), pivot_y + ri * sin(radians));
    cairo_line_to(cr, cx + ro * cos(radians), pivot_y + ro * sin(radians));
    cairo_stroke(cr);

    if (t % 2 == 0) {
      double val = f * 10.0 * interval;

      if (units == 1) {
        snprintf(sf, sizeof(sf), "%0.1f", val);
      } else {
        int p = (int)(val + 0.5);

        if (p == 1000) { snprintf(sf, sizeof(sf), "1K"); }
        else           { snprintf(sf, sizeof(sf), "%d", p); }
      }

      cairo_text_extents(cr, sf, &extents);
      x = cx + (ro + 7.0 * scalfac) * cos(radians);
      y = pivot_y + (ro + 7.0 * scalfac) * sin(radians);
      cairo_move_to(cr, x - 0.5 * extents.width, y + 0.35 * extents.height);
      cairo_show_text(cr, sf);
    }
  }

  //
  // White peak-hold needle
  //
  angle   = min_angle + pk * (max_angle - min_angle);
  radians = angle * M_PI / 180.0;
  double cosr=cos(radians);
  double sinr=sin(radians);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width(cr, 1.4 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr, pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr, pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  //
  // Live needle
  //
  angle   = min_angle + frac * (max_angle - min_angle);
  radians = angle * M_PI / 180.0;
  cosr=cos(radians);
  sinr=sin(radians);
  meter_zone_rgb(frac, &r, &g, &b);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgba(cr, r, g, b, 0.25);
  cairo_set_line_width(cr, 4.0 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr, pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr, pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  cairo_set_source_rgba(cr, r, g, b, 0.96);
  cairo_set_line_width(cr, 2.2 * scalfac);
  cairo_move_to(cr, cx + (radius - 30.0 * scalfac) * cosr, pivot_y + (radius - 30.0 * scalfac) * sinr);
  cairo_line_to(cr, cx + (radius + 5.0 * scalfac) * cosr, pivot_y + (radius + 5.0 * scalfac) * sinr);
  cairo_stroke(cr);
  //
  // Centered text stack: power, SWR, (ALC)
  //
  cairo_set_source_rgba(cr, COLOUR_METER);
  cairo_set_font_size(cr, 12.0 * scalfac);
  cairo_text_extents(cr, pwrstr, &extents);
  cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 32.0 * scalfac);
  cairo_show_text(cr, pwrstr);

  if (swr_alarm) { cairo_set_source_rgba(cr, COLOUR_ALARM); }
  else           { cairo_set_source_rgba(cr, COLOUR_METER); }

  snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
  cairo_text_extents(cr, sf, &extents);
  cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 17.0 * scalfac);
  cairo_show_text(cr, sf);

  if (!cwmode && ADD_METER_WIDTH == 0) {
    cairo_set_source_rgba(cr, COLOUR_METER);
    snprintf(sf, sizeof(sf), "ALC %2.0f dB", alc);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 4.0 * scalfac);
    cairo_show_text(cr, sf);
  }
}

/* ───────────────────── Dual-scale TX power bar ───────────────────── */
static void txmeter_powerbar(cairo_t *cr, double frac, double pk, const char *pwrstr,
                             double swr, int swr_alarm, double interval, int units) {
  char sf[32];
  cairo_text_extents_t extents;
  double r, g, b;
  const double x0  = (double) ADD_METER_WIDTH;
  const double w   = (double)(METER_WIDTH - ADD_METER_WIDTH);
  const double scalfac = w * 0.00625;
  const double bx  = x0 + 22.0 * scalfac;
  const double bw  = w - 36.0 * scalfac;
  const double bh  = 13.0 * scalfac;
  const double by  = VFO_HEIGHT * 0.50;

  //
  // Top readout: power (left), SWR (right)
  //
  cairo_set_source_rgba(cr, COLOUR_METER);
  cairo_set_font_size(cr, 13.0 * scalfac);
  cairo_move_to(cr, bx, 15.0 * scalfac);
  cairo_show_text(cr, pwrstr);

  if (swr_alarm) { cairo_set_source_rgba(cr, COLOUR_ALARM); }
  else           { cairo_set_source_rgba(cr, COLOUR_OK); }

  snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
  cairo_text_extents(cr, sf, &extents);
  cairo_move_to(cr, bx + bw - extents.width, 15.0 * scalfac);
  cairo_show_text(cr, sf);
  //
  // Percent scale ABOVE the bar (0 / 50 / 100)
  //
  cairo_set_font_size(cr, 8.0 * scalfac);

  for (int t = 0; t <= 10; t += 5) {
    double f  = (double)t / 10.0;
    double tx = bx + f * bw;
    cairo_set_source_rgba(cr, COLOUR_METER);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, tx, by - 3.0 * scalfac);
    cairo_line_to(cr, tx, by - 8.0 * scalfac);
    cairo_stroke(cr);
    snprintf(sf, sizeof(sf), "%d%%", t * 10);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, tx - 0.5 * extents.width, by - 10.0 * scalfac);
    cairo_show_text(cr, sf);
  }

  //
  // Trough + graded fill (clipped to the rounded trough)
  //
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
  meter_rounded_rect(cr, bx, by, bw, bh, 4.0 * scalfac);
  cairo_fill(cr);
  cairo_save(cr);
  meter_rounded_rect(cr, bx, by, bw, bh, 4.0 * scalfac);
  cairo_clip(cr);
  const int nsteps = 96;

  for (int i = 0; i < nsteps; i++) {
    double f = (double)i / nsteps;

    if (f > frac) { break; }

    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.95);
    cairo_rectangle(cr, bx + f * bw, by, bw / nsteps + 0.6, bh);
    cairo_fill(cr);
  }

  cairo_restore(cr);
  //
  // White peak marker
  //
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width(cr, 2.0 * scalfac);
  double px = bx + pk * bw;
  cairo_move_to(cr, px, by - 2.0 * scalfac);
  cairo_line_to(cr, px, by + bh + 2.0 * scalfac);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 1.0);
  //
  // Native scale (W, or 0..1 when PA disabled) BELOW the bar
  //
  cairo_set_font_size(cr, 8.0 * scalfac);

  for (int t = 0; t <= 10; t += 2) {
    double f  = (double)t / 10.0;
    double tx = bx + f * bw;
    meter_zone_rgb(f, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.85);
    cairo_move_to(cr, tx, by + bh + 2.0 * scalfac);
    cairo_line_to(cr, tx, by + bh + 7.0 * scalfac);
    cairo_stroke(cr);
    double val = f * 10.0 * interval;

    if (units == 1) {
      snprintf(sf, sizeof(sf), "%0.1f", val);
    } else {
      int p = (int)(val + 0.5);

      if (p == 1000) { snprintf(sf, sizeof(sf), "1K"); }
      else           { snprintf(sf, sizeof(sf), "%d", p); }
    }

    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, tx - 0.5 * extents.width, by + bh + 16.0 * scalfac);
    cairo_show_text(cr, sf);
  }
}

void rxmeter_update(int fps, double rxlvl, double peak, double gain, double out) {
  if (!meter_surface) { return; }

  const double min_rxlvl = -200.0;
  const double min_gain  =  -99.0;
  const double min_out   =  -99.0;
  const double min_peak  =  -99.0;
  static double max_rxlvl = min_rxlvl;
  static double max_gain  = min_gain;
  static double max_out   = min_out;
  static double max_peak  = min_peak;
  static double pk        = 0;
  static int max_cnt_lvl  = 0;
  static int max_cnt_gain = 0;
  static int max_cnt_out  = 0;
  static int max_cnt_peak = 0;
  static int pk_count     = 0;
  char sf[32];
  cairo_t *cr = cairo_create (meter_surface);
  cairo_text_extents_t extents;
  double smtr = 0.0, smtr2, perS, ref9;
  int sval = 0, sval2 = 0;

  if (rxtxstate == 1) {
    max_cnt_lvl = 0;
    max_cnt_gain = 0;
    max_cnt_out = 0;
    max_cnt_peak = 0;
    max_rxlvl = min_rxlvl;
    max_gain = min_gain;
    max_out = min_out;
    max_peak = min_peak;
    pk = 0.0;
    pk_count = 0;
    rxtxstate = 0;
  }

  //
  // calculate parameters of "fast averaging" such that
  // it scales well with the frame rate
  //
  int CNTMAX = (fps / 2);
  double EXPAV1 = exp(-2.88/fps);
  double EXPAV2 = 1.0  - EXPAV1;
  //
  // Only the time-averaged values are "on display"
  // The algorithm to calculate these "sedated" values from the
  // (possibly fluctuating)  input ones is as follows:
  //
  // - if counter > CNTMAX then move max_value towards current_value by exponential averaging
  //                            with parameter EXPAV1, EXPAV2 (but never go below the minimum value)
  // - if current_value >  max_value then set max_value to current_value and reset counter
  //
  // A new max value will therefore be immediately visible, the display stays (if not surpassed) for
  // CNTMAX cycles and then the displayed value will gradually approach the new one(s).

  //
  // Map peak value to a "dB" scale. 8.68589 = 20 / log(10)
  //
  if (peak > 0.00001) {
    peak = 8.68589 * log(peak);
  } else {
    peak = -99.0;
  }

  if (++max_cnt_lvl  > CNTMAX) { max_rxlvl = EXPAV1 * max_rxlvl + EXPAV2 * rxlvl;}

  if (++max_cnt_peak > CNTMAX) { max_peak  = EXPAV1 * max_peak  + EXPAV2 * peak ;}

  if (++max_cnt_gain > CNTMAX) { max_gain  = EXPAV1 * max_gain  + EXPAV2 * gain ;}

  if (++max_cnt_out  > CNTMAX) { max_out   = EXPAV1 * max_out   + EXPAV2 * out  ;}

  if (max_rxlvl < min_rxlvl) { max_rxlvl = min_rxlvl; }

  if (max_peak  < min_peak ) { max_peak  = min_peak; }

  if (max_gain  < min_gain ) { max_gain  = min_gain; }

  if (max_out   < min_out  ) { max_out   = min_out; }

  // Fast attack

  if (rxlvl > max_rxlvl) { max_rxlvl = rxlvl; max_cnt_lvl = 0; }

  if (peak  > max_peak ) { max_peak  = peak;  max_cnt_peak = 0; }

  if (gain  > max_gain ) { max_gain  = gain;  max_cnt_gain = 0; }

  if (out   > max_out  ) { max_out   = out;   max_cnt_out = 0; }

  //
  // Calculate S-meter reflection smtr
  // smtr : goes from  0.0 to  72.0 for S0 ... S9
  //        and  from 72.0 to 114.0 for S9 ... S9+60
  // sval:  goes from 0 to 9 for S0 ... S9
  // sval2: if > 0, adds +<sval>
  //
  if (vfo[active_receiver->id].frequency > 30000000LL) {
    ref9 = -93.0;
    if (smeter3dB) {
      perS = 3.0;
      smtr = 2.66666666 * (fmax(-120.0, max_rxlvl) + 120.0);
    } else {
      perS = 6.0;
      smtr = 1.33333333 * (fmax(-147.0, max_rxlvl) + 147.0);
    }

    smtr2 = 0.7 * (max_rxlvl + 93.0);
  } else {
    ref9 = -73.0;
    if (smeter3dB) {
      perS = 3.0;
      smtr = 2.66666666 * (fmax(-100.0, max_rxlvl) + 100.0);
    } else {
      perS = 6.0;
      smtr = 1.33333333 * (fmax(-127.0, max_rxlvl) + 127.0);
    }

    smtr2 = 0.7 * (max_rxlvl + 73);
  }

  if (smtr > 72.0) { smtr = 72.0; }

  if (smtr2 <  0.0) { smtr2 =  0.0; }

  if (smtr2 > 42.0) { smtr2 = 42.0; }

  sval = (smtr + 4.0) * 0.125;
  sval2 = 5 * (int)((smtr2 * 0.28571428571428571428) + 0.5);
  smtr += smtr2;

  //
  // peak-hold on the needle position:
  //  - instant attack
  //  - hold a while (1.5 sec)
  //  - then slow release (S9 --> S0 in 6 sec)
  //

  if (smtr > pk) {
    pk = smtr;
    if (pk > 114.0)  { pk = 114.0; }
    pk_count = (3 * fps) / 2;
  } else {
    if (pk_count > 0) {
      pk_count--;
    } else {
      pk -= 12.0 / fps;
      if (pk < 0.0) { pk = 0.0; }
    }
  }

  //
  // From now on, use time-averaged value (max_rxlvl)
  //
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_paint (cr);

  if (ADD_METER_WIDTH > 0) {
    double scalfac = ADD_METER_WIDTH * 0.01333;
    double Y1 =  (0.5 * VFO_HEIGHT) + 8.0 * scalfac;
    double Y0 =  Y1 - 20.0 * scalfac;
    double Y2 =  Y1 + 20.0 * scalfac;
    cairo_set_source_rgba(cr, COLOUR_OK);
    cairo_set_font_size(cr, 16.0 * scalfac);
    //
    // RX info on additional meter
    //
    snprintf(sf, sizeof(sf), "Mic");
    cairo_move_to(cr, 5, Y0);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "Gain");
    cairo_move_to(cr, 5, Y1);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "Out");
    cairo_move_to(cr, 5, Y2);
    cairo_show_text(cr, sf);
    //
    // BTW %0.0f takes care of correct rounding
    //
    snprintf(sf, sizeof(sf), "%0.0f", max_peak);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y0);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "%0.0f", max_gain);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y1);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "%0.0f", max_out);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y2);
    cairo_show_text(cr, sf);
    cairo_stroke(cr);
  }

  switch (meter_type) {
  case ANALOG: {
    //
    // Analog RX — Option 2 face (dark glass + filled arc) with Option 3 text
    //
    int i;
    double x;
    double y;
    double angle;
    double radians;
    double cy  = (double)(METER_WIDTH - ADD_METER_WIDTH) / 2;
    double scalfac = cy * 0.0125;
    double radius = cy - 35.0 * scalfac;
    double min_angle, max_angle, bydb;
    double cx = cy + ADD_METER_WIDTH - 5;

    if (cy - 0.342 * radius < VFO_HEIGHT - 5) {
      min_angle = 200.0;
      max_angle = 340.0;
    } else if (cy - 0.5 * radius < VFO_HEIGHT - 5) {
      min_angle = 210.0;
      max_angle = 330.0;
    } else {
      min_angle = 220.0;
      max_angle = 320.0;
    }

    bydb = (max_angle - min_angle) / 114.0;
#if 0
    // ── Radial face gradient (dark glass look) ──────────────────────────────
    // deactivated -- colours must be calculated from COLOUR_VFO_BACKGROUND
    {
      cairo_pattern_t *face = cairo_pattern_create_radial(cx, cy - 10, 5, cx, cy, radius + 30);
      cairo_pattern_add_color_stop_rgba(face, 0.0, 0.11, 0.13, 0.16, 0.90);
      cairo_pattern_add_color_stop_rgba(face, 0.6, 0.05, 0.07, 0.09, 0.70);
      cairo_pattern_add_color_stop_rgba(face, 1.0, 0.03, 0.04, 0.05, 0.00);
      cairo_set_source(cr, face);
      cairo_paint(cr);
      cairo_pattern_destroy(face);
    }
#endif
    // ── Dim arc track (full scale background) ──────────────────────────────
    //cairo_set_line_width(cr, 10.0);
    cairo_set_line_width(cr, 10.0 * scalfac);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
    cairo_arc(cr, cx, cy, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
    cairo_stroke(cr);
    // ── Filled coloured arc up to needle position (80 micro-segments) ──────
    {
      int   ARC_STEPS = 80;
      double needle_pcnt = smtr / 114.0;

      for (int si = 0; si < ARC_STEPS; si++) {
        double pcnt = (double)si / ARC_STEPS;

        if (pcnt > needle_pcnt) { break; }

        double a1 = (min_angle + pcnt * (max_angle - min_angle)) * M_PI / 180.0;
        double a2 = (min_angle + (pcnt + 1.0 / ARC_STEPS) * (max_angle - min_angle)) * M_PI / 180.0;
        // Colour: green(S0) → yellow-green(S5) → amber(S9) → red(S9+60)
        double r_col, g_col, b_col;

        if (pcnt < 0.40) {
          double t = pcnt / 0.40;
          r_col = 0.22 + t * (0.55 - 0.22);
          g_col = 0.83 - t * (0.83 - 0.80);
          b_col = 0.33 - t * 0.33;
        } else if (pcnt < 0.647) {
          double t = (pcnt - 0.40) / 0.247;
          r_col = 0.55 + t * (0.941 - 0.55);
          g_col = 0.80 - t * (0.80 - 0.647);
          b_col = 0.0;
        } else {
          double t = (pcnt - 0.647) / 0.353;
          r_col = 0.941 + t * 0.032;
          g_col = 0.647 - t * (0.647 - 0.318);
          b_col = t * 0.286;
        }

        cairo_set_line_width(cr, 10.0 * scalfac);
        cairo_set_source_rgba(cr, r_col, g_col, b_col, 0.88);
        cairo_arc(cr, cx, cy, radius, a1, a2);
        cairo_stroke(cr);
      }
    }
    // ── Tick marks (outside the arc band, colour-coded) ────────────────────
    cairo_set_line_width(cr, 1.5 * scalfac);

    for (i = 1; i < 10; i++) {
      angle   = ((double)i * 8.0 * bydb) + min_angle;
      radians = angle * M_PI / 180.0;
      double pcnt = (double)(i * 8) / 114.0;
      double rc, gc, bc;

      if (pcnt < 0.40)        { rc = 0.22;  gc = 0.83;  bc = 0.33;  }
      else if (pcnt < 0.647)  { rc = 0.941; gc = 0.647; bc = 0.0;   }
      else                    { rc = 0.973; gc = 0.318; bc = 0.286;  }

      cairo_set_source_rgba(cr, rc, gc, bc, 0.75);
      double r_in  = (i % 2 == 1) ? radius + 12.0 * scalfac : radius + 14.0 * scalfac;
      double r_out = radius + 18.0 * scalfac;
      cairo_arc(cr, cx, cy, r_in,  radians, radians);
      cairo_get_current_point(cr, &x, &y);
      cairo_arc(cr, cx, cy, r_out, radians, radians);
      cairo_line_to(cr, x, y);
      cairo_stroke(cr);

      if (i % 2 == 1) {
        // major tick label
        snprintf(sf, sizeof(sf), "%d", i);
        cairo_text_extents(cr, sf, &extents);
        cairo_arc(cr, cx, cy, r_out + 4.0 * scalfac, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_new_path(cr);
        x += extents.width * (x - (cx + cy)) / (2.0 * cy);
        cairo_move_to(cr, x, y);
        cairo_set_source_rgba(cr, rc, gc, bc, 0.80);
        cairo_set_font_size(cr, 12.0 * scalfac);
        cairo_show_text(cr, sf);
      }

      cairo_new_path(cr);
    }

    for (i = 1; i <= 3; i++) {
      angle   = bydb * (double)(14 * i + 72) + min_angle;
      radians = angle * M_PI / 180.0;
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_line_width(cr, 1.5 * scalfac);
      cairo_arc(cr, cx, cy, radius + 12.0 * scalfac, radians, radians);
      cairo_get_current_point(cr, &x, &y);
      cairo_arc(cr, cx, cy, radius + 18.0 * scalfac, radians, radians);
      cairo_line_to(cr, x, y);
      cairo_stroke(cr);
      snprintf(sf, sizeof(sf), "+%d", 20 * i);
      cairo_text_extents(cr, sf, &extents);
      cairo_arc(cr, cx, cy, radius + 22.0 * scalfac, radians, radians);
      cairo_get_current_point(cr, &x, &y);
      cairo_new_path(cr);
      x += extents.width * (x - (cx + cy)) / (2.0 * cy);
      cairo_move_to(cr, x, y);
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_font_size(cr, 12.0 * scalfac);
      cairo_show_text(cr, sf);
      cairo_new_path(cr);
    }

    // ── Slim needle with radial-gradient pivot dot ──────────────────────────
    {
      double needle_pcnt = smtr / 114.0;
      double rc, gc, bc;

      if (needle_pcnt < 0.40)       { rc = 0.22;  gc = 0.83;  bc = 0.33; }
      else if (needle_pcnt < 0.647) { rc = 0.941; gc = 0.647; bc = 0.0;  }
      else                          { rc = 0.973; gc = 0.318; bc = 0.286; }

      angle   = min_angle + smtr * bydb;
      radians = angle * M_PI / 180.0;
      double tip_x = cx + (radius + 10) * cos(radians);
      double tip_y = cy + (radius + 10) * sin(radians);
      double base_x = cx - 8.0 * cos(radians);
      double base_y = cy - 8.0 * sin(radians);
      // glow shadow
      cairo_set_line_width(cr, 4.0 * scalfac);
      cairo_set_source_rgba(cr, rc, gc, bc, 0.20);
      cairo_move_to(cr, base_x, base_y);
      cairo_line_to(cr, tip_x, tip_y);
      cairo_stroke(cr);
      // needle
      cairo_set_line_width(cr, 1.5 * scalfac);
      cairo_set_source_rgba(cr, rc, gc, bc, 0.95);
      cairo_move_to(cr, base_x, base_y);
      cairo_line_to(cr, tip_x, tip_y);
      cairo_stroke(cr);
      // pivot dot — radial gradient
      cairo_pattern_t *pivot = cairo_pattern_create_radial(cx, cy, 0, cx, cy, 6);
      cairo_pattern_add_color_stop_rgba(pivot, 0.0, 1.0, 1.0, 1.0, 0.70);
      cairo_pattern_add_color_stop_rgba(pivot, 1.0, rc, gc, bc, 0.55);
      cairo_set_source(cr, pivot);
      cairo_arc(cr, cx, cy, 5, 0, 2 * M_PI);
      cairo_fill(cr);
      cairo_pattern_destroy(pivot);
    }
    // ── Text: dBm (Option 3 placement) ─────────────────────────────────────
    cairo_set_source_rgba(cr, COLOUR_METER);
    snprintf(sf, sizeof(sf), "%d dBm", (int)(max_rxlvl - 0.5));
    cairo_text_extents(cr, sf, &extents);
    cairo_set_font_size(cr, 16.0 * scalfac);
    cairo_move_to(cr, cx - 0.5 * extents.width, cy - radius + 30.0 * scalfac);
    cairo_show_text(cr, sf);
    }
    break;
  case DIGITAL: {
    //
    // Digital RX meter
    //
    double scalfac = (METER_WIDTH - ADD_METER_WIDTH) * 0.00625;
    double Y4 = VFO_HEIGHT - 10.0 * scalfac;
    double Y2 = Y4 - 24.0 * scalfac;
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_set_source_rgba(cr, COLOUR_METER);
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, 18.0 * scalfac);
    snprintf(sf, sizeof(sf), "%-3d dBm", (int)(max_rxlvl - 0.5));  // assume max_rxlvl < 0 in rounding
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, METER_WIDTH - 5 - extents.width, Y4);
    cairo_show_text(cr, sf);
    cairo_set_font_size(cr, 32.0 * scalfac);
    snprintf(sf, sizeof(sf), "S9+55");
    cairo_text_extents(cr, sf, &extents);

    if (sval2 > 0) {
      snprintf(sf, sizeof(sf), "S%d+%d", sval, sval2);
    } else {
      snprintf(sf, sizeof(sf), "S%d", sval);
    }

    cairo_move_to(cr, METER_WIDTH - 5 - extents.width, Y2);
    cairo_show_text(cr, sf);
    }
    break;
  case EDGEWISE:
   rxmeter_edgewise(cr, smtr, sval, sval2, max_rxlvl, pk);
   break;
  case DUALSCALE:
    rxmeter_dualscale(cr, smtr, sval, sval2, max_rxlvl, pk, ref9, perS);
    break;
  }

  cairo_destroy(cr);
  gtk_widget_queue_draw (meter);
}

void txmeter_update(int fps, double pwr, double alc, double swr, double mic, double out) {
  if (!meter_surface || !can_transmit) { return; }

  const double min_alc    = -99.0;
  const double min_pwr    =   0.0;
  const double min_mic    = -99.0;
  const double min_out    = -99.0;
  static double max_alc   = min_alc;
  static double max_pwr   = min_pwr;
  static double max_mic   = min_mic;
  static double max_out   = min_out;
  static double pk        = 0.0;
  static int max_pwrcount = 0;
  static int max_alccount = 0;
  static int max_miccount = 0;
  static int max_outcount = 0;
  static int pk_count     = 0;
  char sf[32];
  cairo_t *cr = cairo_create (meter_surface);
  cairo_text_extents_t extents;
  int txvfo = vfo_get_tx_vfo();
  int txmode = vfo[txvfo].mode;
  int cwmode = (txmode == modeCWU || txmode == modeCWL) && !transmitter->tune && !transmitter->twotone;
  const BAND *band = band_get_band(vfo[txvfo].band);

  if (rxtxstate == 0) {
    max_pwrcount = 0;
    max_alccount = 0;
    max_miccount = 0;
    max_outcount = 0;
    max_alc = min_alc;
    max_pwr = min_pwr;
    max_mic = min_mic;
    max_out = min_out;
    pk = 0.0;
    pk_count = 0;
    rxtxstate = 1;
  }

  //
  // calculate parameters of "fast averaging" such that
  // it scales well with the frame rate
  //
  int CNTMAX = (fps / 2);
  double EXPAV1 = exp(-2.88/fps);
  double EXPAV2 = 1.0  - EXPAV1;

  if (max_pwrcount > CNTMAX) { max_pwr = EXPAV1 * max_pwr + EXPAV2 * pwr; }

  if (max_alccount > CNTMAX) { max_alc = EXPAV1 * max_alc + EXPAV2 * alc; }

  if (max_miccount > CNTMAX) { max_mic = EXPAV1 * max_mic + EXPAV2 * mic; }

  if (max_outcount > CNTMAX) { max_out = EXPAV1 * max_out + EXPAV2 * out; }

  if (max_pwr < min_pwr) { max_pwr = min_pwr; }

  if (max_alc < min_alc) { max_alc = min_alc; }

  if (max_out < min_out) { max_out = min_out; }

  if (max_mic < min_mic) { max_mic = min_mic; }

  if (pwr > max_pwr) { max_pwr = pwr; max_pwrcount = 0; }

  if (alc > max_alc) { max_alc = alc; max_alccount = 0; }

  if (mic > max_mic) { max_mic = mic; max_miccount = 0; }

  if (out > max_out) { max_out = out; max_outcount = 0; }

  max_pwrcount++;
  max_outcount++;
  max_miccount++;
  max_outcount++;

  //
  // From now on, ONLY use time-averaged values
  //
  cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
  cairo_select_font_face(cr, DISPLAY_FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_paint (cr);

  if (ADD_METER_WIDTH > 0 && !cwmode) {
    double scalfac = ADD_METER_WIDTH * 0.01333;
    double Y1 =  (0.5 * VFO_HEIGHT) + 8.0 * scalfac;
    double Y0 =  Y1 - 20.0 * scalfac;
    double Y2 =  Y1 + 20.0 * scalfac;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, 16.0 * scalfac);
    //
    // TX info on additional meter
    //
    snprintf(sf, sizeof(sf), "Mic");
    cairo_move_to(cr, 5, Y0);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "Alc");
    cairo_move_to(cr, 5, Y1);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "Out");
    cairo_move_to(cr, 5, Y2);
    cairo_show_text(cr, sf);
    //
    snprintf(sf, sizeof(sf), "%0.0f", max_mic);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y0);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "%0.0f", max_alc);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y1);
    cairo_show_text(cr, sf);
    snprintf(sf, sizeof(sf), "%0.0f", max_out);
    cairo_text_extents(cr, sf, &extents);
    cairo_move_to(cr, ADD_METER_WIDTH - extents.width - 2.0, Y2);
    cairo_show_text(cr, sf);
    cairo_stroke(cr);
  }

  //
  // Some data common to power meters (only needed and valid for HPSDR)
  //
  int units;
  int swr_alarm;
  char pwrstr[32];
  double interval;
  double frac;

  if (band->disablePA || !pa_enabled) {
    units = 1;
    interval = 0.1;
  } else {
    int pp = pa_power_list[pa_power];
    units = (pp <= 1) ? 1 : 2;
    interval = 0.1 * pp;
  }

  frac = max_pwr / (10.0 * interval);
  swr_alarm = (swr > transmitter->swr_alarm);

  if (frac < 0.0) { frac = 0.0; }

  if (frac > 1.0) { frac = 1.0; }

  //
  // peak-hold on the needle position:
  //  - instant attack
  //  - hold a while (1.5 sec)
  //  - then slow release (full --> 0 in 8 seconds)
  //
  if (frac > pk) {
    pk = frac;
    pk_count = (3 * fps) / 2;
  } else {
    if (pk_count > 0) {
      pk_count--;
    } else {
      pk -= 0.125 / fps;
      if (pk < 0.0) { pk = 0.0; }
    }
  }

  switch (pa_power) {
  case PA_1W:
    snprintf(pwrstr, sizeof(pwrstr), "%dmW",   (int)(1000.0 * max_pwr + 0.5));
    break;

  case PA_5W:
  case PA_10W:
    snprintf(pwrstr, sizeof(pwrstr), "%0.1fW", max_pwr);
    break;

  default:
    snprintf(pwrstr, sizeof(pwrstr), "%dW",    (int)(max_pwr + 0.5));
    break;
  }

  switch (meter_type) {
  case ANALOG: {
    //
    // Analog TX — Option 2 face (dark glass + filled arc) with Option 3 text
    //
    double cy  = (double)(METER_WIDTH - ADD_METER_WIDTH) / 2;
    double scalfac = 0.0125 * cy;
    double radius = cy - 35.0 * scalfac;
    //
    double cx = cy + ADD_METER_WIDTH - 5;

    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      double angle, radians, min_angle, max_angle;
      double x, y;

      if (cy - 0.342 * radius < VFO_HEIGHT - 5) {
        min_angle = 200.0;
        max_angle = 340.0;
      } else if (cy - 0.5 * radius < VFO_HEIGHT - 5) {
        min_angle = 210.0;
        max_angle = 330.0;
      } else {
        min_angle = 220.0;
        max_angle = 320.0;
      }

#if 0
      // ── Radial face gradient ──────────────────────────────────────────────
      // deactivated -- colours must be calculated from COLOUR_VFO_BACKGROUND
      {
        cairo_pattern_t *face = cairo_pattern_create_radial(cx, cy - 10, 5, cx, cy, radius + 30);
        cairo_pattern_add_color_stop_rgba(face, 0.0, 0.11, 0.13, 0.16, 0.90);
        cairo_pattern_add_color_stop_rgba(face, 0.6, 0.05, 0.07, 0.09, 0.70);
        cairo_pattern_add_color_stop_rgba(face, 1.0, 0.03, 0.04, 0.05, 0.00);
        cairo_set_source(cr, face);
        cairo_paint(cr);
        cairo_pattern_destroy(face);
      }
#endif
      // ── Dim arc track ─────────────────────────────────────────────────────
      cairo_set_line_width(cr, 10.0 * scalfac);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
      cairo_arc(cr, cx, cy, radius, min_angle * M_PI / 180.0, max_angle * M_PI / 180.0);
      cairo_stroke(cr);
      // ── Radial face gradient ──────────────────────────────────────────────
      // ── Filled coloured arc up to needle position ─────────────────────────
      // TX power colour: green(low) → amber(mid) → red(high)
      {
        double needle_angle = max_pwr * (max_angle - min_angle) / (10.0 * interval) + min_angle;

        if (needle_angle > max_angle + 5) { needle_angle = max_angle + 5; }

        double needle_pcnt = (needle_angle - min_angle) / (max_angle - min_angle);
        int ARC_STEPS = 80;

        for (int si = 0; si < ARC_STEPS; si++) {
          double fr = (double)si / ARC_STEPS;

          if (fr > needle_pcnt) { break; }

          double a1 = (min_angle + fr * (max_angle - min_angle)) * M_PI / 180.0;
          double a2 = (min_angle + (fr + 1.0 / ARC_STEPS) * (max_angle - min_angle)) * M_PI / 180.0;
          double rc, gc, bc;

          if (fr < 0.50) {
            double t = fr / 0.50;
            rc = 0.22 * t;
            gc = 0.83 - 0.18 * t;
            bc = 0.20 * (1.0 - t);
          } else if (fr < 0.80) {
            double t = (fr - 0.50) / 0.30;
            rc = 0.22 + t * (0.941 - 0.22);
            gc = 0.65 - t * 0.003;
            bc = 0.0;
          } else {
            double t = (fr - 0.80) / 0.20;
            rc = 0.941 + t * 0.032;
            gc = 0.647 - t * (0.647 - 0.318);
            bc = t * 0.286;
          }

          cairo_set_line_width(cr, 10.0 * scalfac);
          cairo_set_source_rgba(cr, rc, gc, bc, 0.88);
          cairo_arc(cr, cx, cy, radius, a1, a2);
          cairo_stroke(cr);
        }
      }
      // ── Tick marks (colour-coded, outside arc band) ───────────────────────
      cairo_set_line_width(cr, 1.5 * scalfac);

      for (int i = 0; i <= 100; i++) {
        angle   = (double)i * 0.01 * max_angle + (double)(100 - i) * 0.01 * min_angle;
        radians = angle * M_PI / 180.0;

        if ((i % 10) == 0) {
          double fr = (double)i / 100.0;
          double rc = fr < 0.5 ? 0.22 * fr / 0.5 : 0.22 + (fr - 0.5) / 0.5 * (0.941 - 0.22);
          double gc = fr < 0.5 ? 0.83 : 0.65 - (fr - 0.5) / 0.5 * 0.003;
          double bc = fr < 0.5 ? 0.20 * (1 - fr / 0.5) : 0.0;
          cairo_set_source_rgba(cr, rc, gc, bc, 0.75);
          double r_in  = radius + 12.0 * scalfac;
          double r_out = radius + 18.0 * scalfac;
          cairo_arc(cr, cx, cy, r_in,  radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cy, r_out, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);

          if ((i % 20) == 0) {
            switch (units) {
            case 1:
              snprintf(sf, sizeof(sf), "%0.1f", 0.1 * interval * i);
              break;

            case 2: {
              int p = (int)(0.1 * interval * i);

              if (p == 1000) { snprintf(sf, sizeof(sf), "1K"); }
              else           { snprintf(sf, sizeof(sf), "%d", p); }
            }
            break;
            }

            cairo_text_extents(cr, sf, &extents);
            cairo_arc(cr, cx, cy, r_out + 4 * scalfac, radians, radians);
            cairo_get_current_point(cr, &x, &y);
            cairo_new_path(cr);
            x += extents.width * (x - (cx + cy)) / (2.0 * cy);
            cairo_move_to(cr, x, y);
            cairo_set_font_size(cr, 12.0 * scalfac);
            cairo_set_source_rgba(cr, rc, gc, bc, 0.80);
            cairo_show_text(cr, sf);
          }
        }

        cairo_new_path(cr);
      }

      // ── Slim needle with pivot dot ────────────────────────────────────────
      {
        angle = max_pwr * (max_angle - min_angle) / (10.0 * interval) + min_angle;

        if (angle > max_angle + 5) { angle = max_angle + 5; }

        radians = angle * M_PI / 180.0;
        double needle_pcnt = (angle - min_angle) / (max_angle - min_angle);
        double rc, gc, bc;

        if (needle_pcnt < 0.50)      { rc = 0.22 * needle_pcnt / 0.50; gc = 0.83; bc = 0.20 * (1.0 - needle_pcnt / 0.50); }
        else if (needle_pcnt < 0.80) { rc = 0.22 + (needle_pcnt - 0.50) / 0.30 * (0.941 - 0.22); gc = 0.65; bc = 0.0; }
        else                         { rc = 0.973; gc = 0.318; bc = 0.286; }

        double tip_x  = cx + (radius + 10) * cos(radians);
        double tip_y  = cy + (radius + 10) * sin(radians);
        double base_x = cx - 8.0 * cos(radians);
        double base_y = cy - 8.0 * sin(radians);
        cairo_set_line_width(cr, 4.0 * scalfac);
        cairo_set_source_rgba(cr, rc, gc, bc, 0.20);
        cairo_move_to(cr, base_x, base_y);
        cairo_line_to(cr, tip_x, tip_y);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 1.5 * scalfac);
        cairo_set_source_rgba(cr, rc, gc, bc, 0.95);
        cairo_move_to(cr, base_x, base_y);
        cairo_line_to(cr, tip_x, tip_y);
        cairo_stroke(cr);
        cairo_pattern_t *pivot = cairo_pattern_create_radial(cx, cy, 0, cx, cy, 6);
        cairo_pattern_add_color_stop_rgba(pivot, 0.0, 1.0, 1.0, 1.0, 0.70);
        cairo_pattern_add_color_stop_rgba(pivot, 1.0, rc, gc, bc, 0.55);
        cairo_set_source(cr, pivot);
        cairo_arc(cr, cx, cy, 5, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(pivot);
      }
      // ── Text: power value (Option 3 placement) ────────────────────────────
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_set_font_size(cr, 12.0 * scalfac);

      switch (pa_power) {
      case PA_1W:
        snprintf(sf, sizeof(sf), "%dmW",   (int)(1000.0 * max_pwr + 0.5));
        break;

      case PA_5W:
      case PA_10W:
        snprintf(sf, sizeof(sf), "%0.1fW", max_pwr);
        break;

      default:
        snprintf(sf, sizeof(sf), "%dW",    (int)(max_pwr + 0.5));
        break;
      }

      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 32 * scalfac);
      cairo_show_text(cr, sf);

      if (swr > transmitter->swr_alarm) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
      } else {
        cairo_set_source_rgba(cr, COLOUR_METER);
      }

      snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 17 * scalfac);
      cairo_show_text(cr, sf);
    }

    if (!cwmode && ADD_METER_WIDTH == 0) {
      cairo_set_source_rgba(cr, COLOUR_METER);
      snprintf(sf, sizeof(sf), "ALC %2.0f dB", max_alc);
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, cx - 0.5 * extents.width, VFO_HEIGHT - 5);
      cairo_show_text(cr, sf);
    }
    }
    break;
  case DIGITAL: {
    //
    // Digital TX meter
    //
    double scalfac = (METER_WIDTH - ADD_METER_WIDTH) * 0.00625;
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_set_source_rgba(cr, COLOUR_METER);
    double Y4 = VFO_HEIGHT - 10.0 * scalfac;

    if (!cwmode && ADD_METER_WIDTH == 0) {
      cairo_set_font_size(cr, 16.0 * scalfac);
      cairo_set_source_rgba(cr, COLOUR_METER);  // revert to white color
      snprintf(sf, sizeof(sf), "ALC %2.0f", max_alc);
      cairo_move_to(cr, 5, Y4);
      cairo_show_text(cr, sf);
    }

    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      double Y2 = Y4 - 24.0 * scalfac;

      //
      // Power/SWR not available for SOAPY.
      //
      if (swr > transmitter->swr_alarm) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
      } else {
        cairo_set_source_rgba(cr, COLOUR_OK); // display SWR in white color
      }

      cairo_set_font_size(cr, 18.0 * scalfac);
      snprintf(sf, sizeof(sf), "SWR %1.1f:1", swr);
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - 5 - extents.width, Y4);
      cairo_show_text(cr, sf);

      switch (pa_power) {
      case PA_1W:
        snprintf(sf, sizeof(sf), "%d mW", (int)(1000.0 * max_pwr + 0.5));
        break;

      case PA_5W:
      case PA_10W:
        snprintf(sf, sizeof(sf), "%0.1f W", max_pwr);
        break;

      default:
        snprintf(sf, sizeof(sf), "%d W", (int)(max_pwr + 0.5));
        break;
      }

      cairo_set_font_size(cr, 32.0 * scalfac);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_text_extents(cr, sf, &extents);
      cairo_move_to(cr, METER_WIDTH - 5 - extents.width, Y2);
      cairo_show_text(cr, sf);
      }
    }
    break;
  case EDGEWISE:
   txmeter_edgewise(cr, frac, pk, pwrstr, swr, swr_alarm, interval, units, max_alc, cwmode);
   break;
  case DUALSCALE:
    txmeter_powerbar(cr, frac, pk, pwrstr, swr, swr_alarm, interval, units);
    break;
  }

  cairo_destroy(cr);
  gtk_widget_queue_draw (meter);
}
