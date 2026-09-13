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
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>

#include "appearance.h"
#include "message.h"
#include "json.h"

#define MAX_VFO_LAYOUTS  32
#define VFO_LAYOUT_FILE  "vfo_layouts.json"

//
// The runtime VFO-bar layout table. At startup this is filled from the
// compiled-in defaults (builtin_vfo_layout_list[] below) and then, if present,
// overridden by the runtime "vfo_layouts.json" file (see vfo_layout_init /
// vfo_layout_reload). The list is terminated by a sentinel whose width is
// negative, exactly like the original compiled-in table, so that the search in
// radio.c/choose_vfo_layout() keeps working unchanged.
//
VFO_BAR_LAYOUT vfo_layout_list[MAX_VFO_LAYOUTS + 1];
int num_vfo_layouts = 0;
//
// Optional user override: when non-empty and it names a layout that fits the
// available width, that layout is pinned instead of the automatic
// (largest-that-fits) choice. Persisted in the props file.
//
char forced_vfo_layout[64] = "";

const VFO_BAR_LAYOUT *current_vfo_layout = vfo_layout_list;
//
// When a VFO bar layout that fits is searched in this list,
// first mathing layout is taken,
// so the largest one come first and the smallest one last.
//
static const VFO_BAR_LAYOUT builtin_vfo_layout_list[] = {
  //
  // Our largest layout. Hopefully suitable for those
  // with impaired vision using a 1920px Screen
  //
  {
    .description = "VFO for LARGE Screens",
    .width = 1420,
    .height = 170,
    .size1 = 20,
    .size2 = 34,
    .size3 = 45,

    .vfo_a_l = 1090,
    .vfo_a_r = 1410,
    .vfo_a_y = 80,

    .vfo_b_l = 1090,
    .vfo_b_r = 1410,
    .vfo_b_y = 130,

    .mode_x = 1090,
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
    .eq_x = 450,
    .eq_y = 85,
    .cat_x = 630,
    .cat_y = 85,

    .cmpr_x = 450,
    .cmpr_y = 125,
    .ps_x = 450,
    .ps_y = 160,
    .dexp_x = 790,
    .dexp_y = 85,

    .vox_x = 630,
    .vox_y = 125,
    .dup_x = 790,
    .dup_y = 125,

    .lock_x = 880,
    .lock_y = 160,
    .zoom_x = 160,
    .zoom_y = 160,
    .ctun_x = 880,
    .ctun_y = 125,
    .step_x = 880,
    .step_y = 85,
    .split_x = 630,
    .split_y = 160,
    .sat_x = 790,
    .sat_y = 160,
    .rit_x = 1235,
    .rit_y = 160,
    .xit_x = 1335,
    .xit_y = 160,
    .filter_x = 1100,
    .filter_y = 160,
    .multifn_x = 1260,
    .multifn_y = 160,

    .lat_x = 5,
    .lat_y = 75
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

//
// Descriptor table for the integer fields of VFO_BAR_LAYOUT, listed once so
// that reading (from JSON) and writing (the default JSON) can never fall out
// of sync. The "description" string is handled separately.
//
typedef struct {
  const char *key;
  size_t offset;
} VfoField;

#define VF(field) { #field, offsetof(VFO_BAR_LAYOUT, field) }

static const VfoField vfo_fields[] = {
  VF(width),    VF(height),   VF(size1),    VF(size2),    VF(size3),
  VF(vfo_a_l),  VF(vfo_a_r),  VF(vfo_a_y),
  VF(vfo_b_l),  VF(vfo_b_r),  VF(vfo_b_y),
  VF(mode_x),   VF(mode_y),   VF(zoom_x),   VF(zoom_y),
  VF(ps_x),     VF(ps_y),     VF(rit_x),    VF(rit_y),    VF(xit_x),   VF(xit_y),
  VF(nb_x),     VF(nb_y),     VF(nr_x),     VF(nr_y),     VF(anf_x),   VF(anf_y),
  VF(snb_x),    VF(snb_y),    VF(agc_x),    VF(agc_y),
  VF(cmpr_x),   VF(cmpr_y),   VF(eq_x),     VF(eq_y),     VF(div_x),   VF(div_y),
  VF(step_x),   VF(step_y),   VF(ctun_x),   VF(ctun_y),
  VF(cat_x),    VF(cat_y),    VF(dexp_x),   VF(dexp_y),
  VF(vox_x),    VF(vox_y),    VF(lock_x),   VF(lock_y),
  VF(split_x),  VF(split_y),  VF(sat_x),    VF(sat_y),
  VF(dup_x),    VF(dup_y),    VF(filter_x), VF(filter_y),
  VF(multifn_x), VF(multifn_y), VF(lat_x),  VF(lat_y)
};

static const int num_vfo_fields = (int)(sizeof(vfo_fields) / sizeof(vfo_fields[0]));

//
// Number of compiled-in layouts (excluding the negative-width sentinel).
//
static int count_builtin_vfo_layouts(void) {
  int n = 0;

  while (builtin_vfo_layout_list[n].width >= 0) { n++; }

  return n;
}

//
// Release the heap-allocated layout descriptions and mark the table empty.
//
static void free_runtime_vfo_layouts(void) {
  for (int i = 0; i < num_vfo_layouts; i++) {
    g_free((char *) vfo_layout_list[i].description);
    vfo_layout_list[i].description = NULL;
  }

  num_vfo_layouts = 0;
}

//
// Append the negative-width sentinel that terminates the list.
//
static void terminate_vfo_layouts(void) {
  memset(&vfo_layout_list[num_vfo_layouts], 0, sizeof(vfo_layout_list[0]));
  vfo_layout_list[num_vfo_layouts].width = -1;
  vfo_layout_list[num_vfo_layouts].description = NULL;
}

//
// Populate the runtime table from the compiled-in defaults.
//
static void load_builtin_vfo_layouts(void) {
  free_runtime_vfo_layouts();
  int n = count_builtin_vfo_layouts();

  for (int i = 0; i < n && num_vfo_layouts < MAX_VFO_LAYOUTS; i++) {
    vfo_layout_list[num_vfo_layouts] = builtin_vfo_layout_list[i];
    vfo_layout_list[num_vfo_layouts].description =
      g_strdup(builtin_vfo_layout_list[i].description != NULL
               ? builtin_vfo_layout_list[i].description : "");
    num_vfo_layouts++;
  }

  terminate_vfo_layouts();
}

//
// Make sure the runtime table is never empty (a valid layout is needed before
// vfo_layout_init() runs).
//
void vfo_layout_ensure(void) {
  if (num_vfo_layouts == 0) {
    load_builtin_vfo_layouts();
    current_vfo_layout = vfo_layout_list;
  }
}

//
// Parse "vfo_layouts.json" into the runtime table. Accepts either a top-level
// array of layout objects, or an object of the form { "layouts": [ ... ] }.
// Returns 1 on success (at least one layout loaded), 0 otherwise.
//
static int load_vfo_layouts_from_json(const char *path) {
  JsonValue *root = json_parse_file(path);

  if (root == NULL) { return 0; }

  const JsonValue *arr = root;

  if (root->type == JSON_OBJECT) {
    arr = json_object_get(root, "layouts");
  }

  if (arr == NULL || arr->type != JSON_ARRAY) {
    json_free(root);
    return 0;
  }

  free_runtime_vfo_layouts();
  const JsonValue *e;

  JSON_FOREACH(e, arr) {
    if (num_vfo_layouts >= MAX_VFO_LAYOUTS) { break; }

    if (e->type != JSON_OBJECT) { continue; }

    //
    // Start from the first built-in layout so any field omitted from the file
    // gets a sane default instead of zero.
    //
    VFO_BAR_LAYOUT lay = builtin_vfo_layout_list[0];
    lay.description = NULL;

    for (int f = 0; f < num_vfo_fields; f++) {
      int *slot = (int *)((char *) &lay + vfo_fields[f].offset);
      *slot = (int) json_as_number(json_object_get(e, vfo_fields[f].key), *slot);
    }

    const char *desc = json_as_string(json_object_get(e, "description"), NULL);
    vfo_layout_list[num_vfo_layouts] = lay;
    vfo_layout_list[num_vfo_layouts].description =
      g_strdup(desc != NULL ? desc : "Unnamed layout");
    num_vfo_layouts++;
  }

  json_free(root);

  if (num_vfo_layouts == 0) { return 0; }

  terminate_vfo_layouts();
  return 1;
}

//
// Write one layout object into the default JSON file.
//
static void write_vfo_layout_json(FILE *f, const VFO_BAR_LAYOUT *lay, int last) {
  fprintf(f, "    {\n");
  fprintf(f, "      \"description\": \"%s\",\n",
          lay->description != NULL ? lay->description : "Unnamed layout");

  for (int i = 0; i < num_vfo_fields; i++) {
    const int *v = (const int *)((const char *) lay + vfo_fields[i].offset);
    fprintf(f, "      \"%s\": %d%s\n",
            vfo_fields[i].key, *v,
            (i == num_vfo_fields - 1) ? "" : ",");
  }

  fprintf(f, "    }%s\n", last ? "" : ",");
}

//
// Create a default "vfo_layouts.json" from the compiled-in layouts so the
// operator has a ready-to-edit template.
//
static void write_default_vfo_layout_file(const char *path) {
  FILE *f = fopen(path, "w");

  if (f == NULL) {
    t_print("vfo_layout: could not write default %s\n", path);
    return;
  }

  int n = count_builtin_vfo_layouts();
  fprintf(f, "{\n");
  fprintf(f, "  \"_comment\": \"piHPSDR VFO-bar layouts. All coordinates are pixels. At startup the largest layout whose 'width' fits the available space is used; you may pin one with the 'vfo_layout' property. Edit and use the Reload menu button to apply.\",\n");
  fprintf(f, "  \"layouts\": [\n");

  for (int i = 0; i < n; i++) {
    write_vfo_layout_json(f, &builtin_vfo_layout_list[i], i == n - 1);
  }

  fprintf(f, "  ]\n}\n");
  fclose(f);
}

//
// One-time startup initialisation: load vfo_layouts.json if present, otherwise
// write a default one generated from the built-in layouts.
//
void vfo_layout_init(void) {
  load_builtin_vfo_layouts();

  FILE *test = fopen(VFO_LAYOUT_FILE, "r");

  if (test != NULL) {
    fclose(test);

    if (!load_vfo_layouts_from_json(VFO_LAYOUT_FILE)) {
      load_builtin_vfo_layouts();
      t_print("vfo_layout_init: %s could not be parsed, using built-in layouts\n", VFO_LAYOUT_FILE);
    } else {
      t_print("vfo_layout_init: loaded %d layout(s) from %s\n", num_vfo_layouts, VFO_LAYOUT_FILE);
    }
  } else {
    write_default_vfo_layout_file(VFO_LAYOUT_FILE);
    t_print("vfo_layout_init: wrote default %s\n", VFO_LAYOUT_FILE);
  }

  current_vfo_layout = vfo_layout_list;
}

//
// Re-read vfo_layouts.json. The actual re-selection and screen rebuild is done
// by the caller (radio_reload_json_configs -> radio_reconfigure_screen), which
// re-runs choose_vfo_layout() over the refreshed table.
//
void vfo_layout_reload(void) {
  load_builtin_vfo_layouts();

  FILE *test = fopen(VFO_LAYOUT_FILE, "r");

  if (test != NULL) {
    fclose(test);

    if (!load_vfo_layouts_from_json(VFO_LAYOUT_FILE)) {
      load_builtin_vfo_layouts();
      t_print("vfo_layout_reload: %s could not be parsed, using built-in layouts\n", VFO_LAYOUT_FILE);
    } else {
      t_print("vfo_layout_reload: loaded %d layout(s) from %s\n", num_vfo_layouts, VFO_LAYOUT_FILE);
    }
  }

  current_vfo_layout = vfo_layout_list;
}

