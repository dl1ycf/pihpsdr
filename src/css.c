/* Copyright (C)
*  2017 - John Melton, G0ORX/N6LYT
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

#include "css.h"
#include "message.h"

//////////////////////////////////////////////////////////////////////////////////////////
//
// Normally one wants to inherit everything from the GTK theme.
// In some cases, however, this does not work too well. But the
// principle here is to change as little as possible.
//
// Here is a list of CSS definitions we make here:
//
// boldlabel           This is used to write texts in menus and the slider area,
//                     therefore it contains 3px border
//
// slider1             Used for slider and zoompan areas for small  screen width
// slider2             Used for slider and zoompan areas for medium screen width
// slider3             Used for slider and zoompan areas for large  screen width
// slider4             Used for slider and zoompan areas for huge   screen width
//
// big_txt             This is a large bold text. Used for the "pi label" on the
//                     discovery screen, and the "Start" button there
//
// med_txt             This is a large text. Used for the status bar, etc.
//
// small_txt           This is a small text, used where space is scarce.
//
// close_button        Style for the "Close" button in the menus, so it can be
//                     easily recognized
//
// small_button        15px text with minimal paddding. Used in menus where one wants
//                     to make the buttons narrow.
//
// medium_button       the same as small_button but with 20px
//
// large_button        the same as small_button but with 25px
//
// small_toggle_button Used for the buttons in action dialogs, and the filter etc.
//                     menus where the current choice needs proper high-lighting
//
// popup_scale         Used to define the slider that "pops up" when e.g. AF volume
//                     is changed via GPIO/MIDI but no sliders are on display
//
// checkbutton         The standard button is very difficult to see on RaspPi with
//                     a light GTK theme. So we use our own, and draw a grey border
//                     so this should be OK for both the light and dark theme.
//
// radiobutton         see checkbutton.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// Note on cssfonts[]:
//
// Many fonts have a wider spacing than those listed below. These have been tested
// and it has been verified that they do not mess up the VFO bar layout.
// Most critical is the "extended meter" whose minimum width has been chosen as
// small as possible simply to fit on small screens.
// So when adding fonts to the list below, DOUBLE CHECK whether the VFO bar and the
// extended meter still looks good.
//
// ATTN: Nunito Sans is the "default" font on Raspi for the lastest systems
//       (from "Trixie" onwards) but not availabler on older OS versions.
//       It is possible to choose, but it will not look good since you get "something".
//
//////////////////////////////////////////////////////////////////////////////////////////

#ifdef __APPLE__
char *cssfont[] = {"system-ui", "Helvetica Neue", "Monaco", "Arial", "Geneva" };
#else
char *cssfont[] = {"Open Sans", "Nunito Sans", "FreeSans", "Roboto", "Roboto Mono", "Piboto" };
#endif

const int num_css_fonts = sizeof(cssfont) / sizeof(char *);
int which_css_font = 0;

char *css =
  "  combobox { font-size: 15px; }\n"
  "  button   { font-size: 15px; }\n"
  "  #redbutton   { background-image: none; background-color: rgb(100%, 20%, 20%); color: rgb(100%,100%,100%);}\n"
  "  #orangebutton   { background-image: none; background-color: rgb(100%, 50%, 20%); color: rgb(0%,0%,0%);}\n"
  "  #yellowbutton   { background-image: none; background-color: rgb(100%, 100%, 20%); color: rgb(0%,0%,0%);}\n"
  "  #greenbutton { background-image: none; background-color: rgb(20%, 100%, 20%); color: rgb(0%,0%,0%);}\n"
  "  #bluebutton { background-image: none; background-color: rgb(20%, 20%, 100%); color: rgb(100%,100%,100%);}\n"
  "  checkbutton label { font-size: 15px; }\n"
  "  spinbutton { font-size: 15px; }\n"
  "  radiobutton label  { font-size: 15px; }\n"
  "  scale { font-size: 15px; }\n"
  "  entry { font-size: 15px; }\n"
  "  notebook { font-size: 15px; }\n"
  "  #hidebutton {\n"
  "    padding: 0px;\n"
  "    border: 0px;\n"
  "    font-weight: bold;\n"
  "    font-size: 15px;\n"
  "  }\n"
  "  #menubutton {\n"
  "  background-image:none; background-color: rgb(100%, 50%, 20%); color: rgb(0%, 0%, 0%); \n"
  "    padding: 0px;\n"
  "    border: 0px;\n"
  "    font-weight: bold;\n"
  "    font-size: 15px;\n"
  "  }\n"
  "  #boldlabel {\n"
  "    padding: 3px;\n"
  "    font-weight: 500;\n"
  "    font-size: 15px;\n"
  "  }\n"
  "  #slider1   {\n"
  "    padding: 3px;\n"
  "    font-weight: bold;\n"
  "    font-size: 16px;\n"
  "  }\n"
  "  #slider2   {\n"
  "    padding: 3px;\n"
  "    font-weight: normal;\n"
  "    font-size: 18px;\n"
  "  }\n"
  "  #slider3   {\n"
  "    padding: 3px;\n"
  "    font-weight: normal;\n"
  "    font-size: 22px;\n"
  "  }\n"
  "  #slider4   {\n"
  "    padding: 3px;\n"
  "    font-weight: normal;\n"
  "    font-size: 26px;\n"
  "  }\n"
  "  #big_txt {\n"
  "    font-size: 22px;\n"
  "    font-weight: bold;\n"
  "    }\n"
  "  #med_txt {\n"
  "    font-size: 18px;\n"
  "    font-weight: normal;\n"
  "    }\n"
  "  #small_txt {\n"
  "    font-weight: bold;\n"
  "    font-size: 12px;\n"
  "    }\n"
  "  #close_button {\n"
  "    padding: 5px;\n"
  "    font-size: 15px;\n"
  "    font-weight: bold;\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  #small_button {\n"
  "    padding: 1px;\n"
  "    font-size: 15px;\n"
  "    }\n"
  "  #medium_button {\n"
  "    padding: 1px;\n"
  "    font-size: 20px;\n"
  "    }\n"
  "  #large_button {\n"
  "    padding: 1px;\n"
  "    font-size: 25px;\n"
  "    }\n"
  "  #small_button_with_border {\n"
  "    padding: 3px;\n"
  "    font-size: 15px;\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  #small_toggle_button {\n"
  "    padding: 1px;\n"
  "    font-size: 15px;\n"
  "    background-image: none;\n"
  "    }\n"
  "  #small_toggle_button:checked {\n"
  "    padding: 1px;\n"
  "    font-size: 15px;\n"
  "    background-image: none;\n"
  "    background-color: rgb(100%, 20%, 20%);\n"    // background if selected
  "    color: rgb(100%,100%,100%);\n"               // text if selected
  "    }\n"
  "  #popup_scale slider {\n"
  "    background: rgb(  0%,  0%, 100%);\n"         // Slider handle
  "    }\n"
  "  #popup_scale trough {\n"
  "    background: rgb( 50%,50%, 100%);\n"         // Slider bar
  "    }\n"
  "  #popup_scale value {\n"
  "    color: rgb(100%, 10%, 10%);\n"              // digits
  "    font-size: 15px;\n"
  "    }\n"
  "  checkbutton check {\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  radiobutton radio {\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  headerbar { min-height: 0px; padding: 0px; margin: 0px; font-size: 15px; }\n"
  ;

//
// This function adds a global "font family" statement to the CSS
// data (possibliy shadowing previous selections)
// and is called whenever a new font is selected
// in the screen menu. No clean-up is done so it is assumed
// that the font is not changed thousands of times during
// one instance of piHPSDR.
//
void load_font(int font) {
  GtkCssProvider *provider;
  GdkDisplay *display;
  GdkScreen *screen;
  GError *error;
  char str[256];
  if (font < 0) { font = 0; }
  if (font >= num_css_fonts) { font = num_css_fonts - 1; }
  //
  // Typeset the sample string "CWU 500P 16 wpm 800 Hz" with 13.0 point font size,
  // and look whether it fits within 180.0 pixels (this is already generous).
  // Reject fonts wider than that.
  // The main reason to do so is to prevent that a broad fall-back font is used
  // in case one of the fonts in cssfonts[] is not available.
  // In case of failure, take the first font in the list.
  // For example, the "Nunito Sans" font is a good one if it is installed, but
  // on older systems it is just not present and then replaced by an unsuitable one.
  //
  // In the "Screen" menu, check which_css_font after calling load_font() and
  // set the font selection combobox to the "selected" font.
  //
  cairo_text_extents_t extents;
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 250, 50);
  cairo_t *cr = cairo_create(surface);
  cairo_select_font_face(cr, cssfont[font], CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 13.0);
  cairo_text_extents(cr, "CWU 500P 16 wpm 800 Hz", &extents);
  t_print("%s: %s: Width=%5.1f Height=%5.1f\n", __func__, cssfont[font], extents.width, extents.height);
  if (extents.width >= 180.0) { font = 0; }
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  which_css_font = font;
  provider = gtk_css_provider_new ();
  display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);
  gtk_style_context_add_provider_for_screen (screen,
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  error = NULL;
  snprintf(str, sizeof(str), "  * { font-family: %s; }\n", cssfont[which_css_font]);
  (void) gtk_css_provider_load_from_data(provider, str, -1, &error);
  if (error != NULL) {
    t_print("%s: %s\n", __func__, error->message);
    g_clear_error(&error);
  }
  g_object_unref (provider);
}

//
// If CSS loading from a file named default.css is successful, take that
// Otherwise load the default settings hard-coded above.
// If the CSS comes from a file, its font family name is not changed,
// but the argument "font" in any case applies to the VFO bar
//
void load_css(void) {
  GtkCssProvider *provider;
  GdkDisplay *display;
  GdkScreen *screen;
  GError *error;
  provider = gtk_css_provider_new ();
  display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);
  gtk_style_context_add_provider_for_screen (screen,
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  error = NULL;
  (void) gtk_css_provider_load_from_path (provider, "default.css", &error);
  if (error != NULL) {
    //
    // The average user does not provide a default.css file, here
    // the following error message will always appear although this does
    // not indicate a problem.
    //
    t_print("%s: No default.css file\n", __func__);
    g_clear_error(&error);
    (void) gtk_css_provider_load_from_data(provider, css, -1, &error);
    if (error != NULL) {
      //
      // If this error message appears, this usually flags an error
      // in the hard-wired CSS data.
      //
      t_print("%s: %s\n", __func__, error->message);
      g_clear_error(&error);
    }
  }
  g_object_unref (provider);
}
