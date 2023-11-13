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
#include <cjson/cJSON.h> 
#include <stdio.h>

#include "appearance.h"
#include "message.h"

int vfo_layout = 0; // chosen lay out
int nr_layouts = 4; // initialize number of layouts in json file

VFO_BAR_LAYOUT vfo_layout_list[MAX_LAYOUTS]; // list of layouts to be filled

char   FNT[32];      // display font
double FSZ[4];       // font sizes
double cl[26][4];    // colours
double PLT[3];       // pan line widths
 
int parse_vfo_layouts( char* filename) {
  char lout[4], lo_name[32], buffer[8192];
  int cnt = 0, status = 0, len;
  const char *error_ptr;
  cJSON *name_j, *lo;
  
  // open the file 
  FILE *fp = fopen(filename, "r"); 
  if (fp == NULL) { 
    t_print("%s; Error: Unable to open vfo_layout.json.\n", __FUNCTION__); 
    status = 1;
    goto end; 
  } 
 
  // read the file contents into a string 

  len = fread(buffer, 1, sizeof(buffer), fp);
  fclose(fp);
  t_print("%s; bytes read from json; %d\n", __FUNCTION__, len);
  
  // parse the JSON data 
  cJSON *json = cJSON_Parse(buffer); 
  if (json == NULL) { 
    error_ptr = cJSON_GetErrorPtr(); 
    if (error_ptr != NULL) { 
      t_print("%s; Error, %s\n", __FUNCTION__, error_ptr); 
    } 
    status = 1;
    goto end; 
  } 

  // access the JSON data elements
  cJSON *number = cJSON_GetObjectItemCaseSensitive(json, "number"); 
  if (cJSON_IsNumber(number) && (number->valueint > 0)) { 
    nr_layouts = number->valueint; // number of layouts found in .json
  } else {
    t_print("%s; Error, no number of layouts found in json.\n", __FUNCTION__);
    status = 1;
    goto end;
  }

  if ( nr_layouts > MAX_LAYOUTS ) {
    nr_layouts = MAX_LAYOUTS;
    t_print("%s; Warning: layouts limited to %d\n", __FUNCTION__, nr_layouts);
  }

  t_print("%s; nr. of layouts to parse from json: %d\n", __FUNCTION__, nr_layouts);
  //
  // loop through layouts
  //
  // Note the first layout that fits into the actual size of
  // the VFO bar is taken in vfo.c, so the largest one has to come first, and
  // the smallest one last.
  //
  for (cnt = 0; cnt < nr_layouts; cnt++) {
    snprintf(lout, 4, "nr%0d", cnt);
    t_print("%s; Get layout name for key: %s\n", __FUNCTION__, lout);
    name_j = cJSON_GetObjectItemCaseSensitive(json, lout);
    if (!(cJSON_IsString(name_j) && (name_j->valuestring != NULL))) {
      t_print("%s: Error, parsing layout nr: %d from json; key: %s\n", __FUNCTION__, cnt, lout);
      status = 1;
      goto end;    
    }
    snprintf(lo_name, 32, name_j->valuestring);
    t_print("%s; Parsing layout %s\n", __FUNCTION__, lo_name);
    lo = cJSON_GetObjectItemCaseSensitive(json, lo_name);
    if (lo == NULL) { 
      error_ptr = cJSON_GetErrorPtr(); 
      if (error_ptr != NULL) { 
        t_print("%s; Error getting cJSON lo, %s\n", __FUNCTION__, error_ptr); 
      } 
      status = 1;
      goto end; 
    } 
    strncpy(vfo_layout_list[cnt].description, cJSON_GetObjectItemCaseSensitive(lo, "description" )->valuestring, 64);
    vfo_layout_list[cnt].width       = cJSON_GetObjectItemCaseSensitive(lo, "width" )->valueint;
    vfo_layout_list[cnt].height      = cJSON_GetObjectItemCaseSensitive(lo, "height" )->valueint;
    vfo_layout_list[cnt].size1       = cJSON_GetObjectItemCaseSensitive(lo, "size1" )->valueint;
    vfo_layout_list[cnt].size2       = cJSON_GetObjectItemCaseSensitive(lo, "size2" )->valueint;
    vfo_layout_list[cnt].size3       = cJSON_GetObjectItemCaseSensitive(lo, "size3" )->valueint;
    vfo_layout_list[cnt].vfo_a_x     = cJSON_GetObjectItemCaseSensitive(lo, "vfo_a_x" )->valueint;
    vfo_layout_list[cnt].vfo_a_y     = cJSON_GetObjectItemCaseSensitive(lo, "vfo_a_y" )->valueint;
    vfo_layout_list[cnt].vfo_b_x     = cJSON_GetObjectItemCaseSensitive(lo, "vfo_b_x" )->valueint;
    vfo_layout_list[cnt].vfo_b_y     = cJSON_GetObjectItemCaseSensitive(lo, "vfo_b_y" )->valueint;
    vfo_layout_list[cnt].mode_x      = cJSON_GetObjectItemCaseSensitive(lo, "mode_x" )->valueint;
    vfo_layout_list[cnt].mode_y      = cJSON_GetObjectItemCaseSensitive(lo, "mode_y" )->valueint;
    vfo_layout_list[cnt].agc_x       = cJSON_GetObjectItemCaseSensitive(lo, "agc_x" )->valueint;
    vfo_layout_list[cnt].agc_y       = cJSON_GetObjectItemCaseSensitive(lo, "agc_y" )->valueint;
    vfo_layout_list[cnt].nr_x        = cJSON_GetObjectItemCaseSensitive(lo, "nr_x" )->valueint;
    vfo_layout_list[cnt].nr_y        = cJSON_GetObjectItemCaseSensitive(lo, "nr_y" )->valueint;
    vfo_layout_list[cnt].nb_x        = cJSON_GetObjectItemCaseSensitive(lo, "nb_x" )->valueint;
    vfo_layout_list[cnt].nb_y        = cJSON_GetObjectItemCaseSensitive(lo, "nb_y" )->valueint;
    vfo_layout_list[cnt].anf_x       = cJSON_GetObjectItemCaseSensitive(lo, "anf_x" )->valueint;
    vfo_layout_list[cnt].anf_y       = cJSON_GetObjectItemCaseSensitive(lo, "anf_y" )->valueint;
    vfo_layout_list[cnt].snb_x       = cJSON_GetObjectItemCaseSensitive(lo, "snb_x" )->valueint;
    vfo_layout_list[cnt].snb_y       = cJSON_GetObjectItemCaseSensitive(lo, "snb_y" )->valueint;
    vfo_layout_list[cnt].div_x       = cJSON_GetObjectItemCaseSensitive(lo, "div_x" )->valueint;
    vfo_layout_list[cnt].div_y       = cJSON_GetObjectItemCaseSensitive(lo, "div_y" )->valueint;
    vfo_layout_list[cnt].eq_x        = cJSON_GetObjectItemCaseSensitive(lo, "eq_x" )->valueint;
    vfo_layout_list[cnt].eq_y        = cJSON_GetObjectItemCaseSensitive(lo, "eq_y" )->valueint;
    vfo_layout_list[cnt].cat_x       = cJSON_GetObjectItemCaseSensitive(lo, "cat_x" )->valueint;
    vfo_layout_list[cnt].cat_y       = cJSON_GetObjectItemCaseSensitive(lo, "cat_y" )->valueint;
    vfo_layout_list[cnt].cmpr_x      = cJSON_GetObjectItemCaseSensitive(lo, "cmpr_x" )->valueint;
    vfo_layout_list[cnt].cmpr_y      = cJSON_GetObjectItemCaseSensitive(lo, "cmpr_y" )->valueint;
    vfo_layout_list[cnt].ps_x        = cJSON_GetObjectItemCaseSensitive(lo, "ps_x" )->valueint;
    vfo_layout_list[cnt].ps_y        = cJSON_GetObjectItemCaseSensitive(lo, "ps_y" )->valueint;
    vfo_layout_list[cnt].vox_x       = cJSON_GetObjectItemCaseSensitive(lo, "vox_x" )->valueint;
    vfo_layout_list[cnt].vox_y       = cJSON_GetObjectItemCaseSensitive(lo, "vox_y" )->valueint;
    vfo_layout_list[cnt].dup_x       = cJSON_GetObjectItemCaseSensitive(lo, "dup_x" )->valueint;
    vfo_layout_list[cnt].dup_y       = cJSON_GetObjectItemCaseSensitive(lo, "dup_y" )->valueint;
    vfo_layout_list[cnt].lock_x      = cJSON_GetObjectItemCaseSensitive(lo, "lock_x" )->valueint;
    vfo_layout_list[cnt].lock_y      = cJSON_GetObjectItemCaseSensitive(lo, "lock_y" )->valueint;
    vfo_layout_list[cnt].zoom_x      = cJSON_GetObjectItemCaseSensitive(lo, "zoom_x" )->valueint;
    vfo_layout_list[cnt].zoom_y      = cJSON_GetObjectItemCaseSensitive(lo, "zoom_y" )->valueint;
    vfo_layout_list[cnt].ctun_x      = cJSON_GetObjectItemCaseSensitive(lo, "ctun_x" )->valueint;
    vfo_layout_list[cnt].ctun_y      = cJSON_GetObjectItemCaseSensitive(lo, "ctun_y" )->valueint;
    vfo_layout_list[cnt].step_x      = cJSON_GetObjectItemCaseSensitive(lo, "step_x" )->valueint;
    vfo_layout_list[cnt].step_y      = cJSON_GetObjectItemCaseSensitive(lo, "step_y" )->valueint;
    vfo_layout_list[cnt].split_x     = cJSON_GetObjectItemCaseSensitive(lo, "split_x" )->valueint;
    vfo_layout_list[cnt].split_y     = cJSON_GetObjectItemCaseSensitive(lo, "split_y" )->valueint;
    vfo_layout_list[cnt].sat_x       = cJSON_GetObjectItemCaseSensitive(lo, "sat_x" )->valueint;
    vfo_layout_list[cnt].sat_y       = cJSON_GetObjectItemCaseSensitive(lo, "sat_y" )->valueint;
    vfo_layout_list[cnt].rit_x       = cJSON_GetObjectItemCaseSensitive(lo, "rit_x" )->valueint;
    vfo_layout_list[cnt].rit_y       = cJSON_GetObjectItemCaseSensitive(lo, "rit_y" )->valueint;
    vfo_layout_list[cnt].xit_x       = cJSON_GetObjectItemCaseSensitive(lo, "xit_x" )->valueint;
    vfo_layout_list[cnt].xit_y       = cJSON_GetObjectItemCaseSensitive(lo, "xit_y" )->valueint;
    vfo_layout_list[cnt].filter_x    = cJSON_GetObjectItemCaseSensitive(lo, "filter_x" )->valueint;
    vfo_layout_list[cnt].filter_y    = cJSON_GetObjectItemCaseSensitive(lo, "filter_y" )->valueint;
  }
end:
  cJSON_Delete(json);
  return status;
}

int parse_cairo_layout( char* filename) {
  int cnt = 0, status = 0;
  cJSON *fsz, *color, *plt;
  char buffer[8192];
  const char *error_ptr;
  // open the file 
  FILE *fp = fopen(filename, "r"); 
  if (fp == NULL) { 
    t_print("%s: Error: Unable to open cairo_layout.json.\n", __FUNCTION__); 
    status = 1;
    goto end; 
  } 
 
  // read the file contents into a string 
  int len = fread(buffer, 1, sizeof(buffer), fp);
  fclose(fp); 
  t_print("%s: bytes read from json: %d\n", __FUNCTION__, len);
    
  // parse the JSON data 
  cJSON *json = cJSON_Parse(buffer); 
  if (json == NULL) { 
    const char *error_ptr = cJSON_GetErrorPtr(); 
    if (error_ptr != NULL) { 
      t_print("%s: Error, %s\n", __FUNCTION__, error_ptr); 
    } 
    status = 1;
    goto end; 
  } 
  
  // access Font
  snprintf(FNT, 32, cJSON_GetObjectItemCaseSensitive(json, "FNT")->valuestring);
  t_print("%s: Font: %s\n", __FUNCTION__, FNT);
   
  // access Font sizes
  char sizes[4][4] = { "SZ1" , "SZ2", "SZ3", "SZ4" };
  fsz = cJSON_GetObjectItemCaseSensitive(json, "FSZ");
  if (fsz == NULL) { 
    error_ptr = cJSON_GetErrorPtr(); 
    if (error_ptr != NULL) { 
      t_print("%s; Error getting FSZ, %s\n", __FUNCTION__, error_ptr); 
    } 
    status = 1;
    goto end; 
  } 
    
  for (cnt = 0; cnt < 4; cnt++) {
    FSZ[cnt] = cJSON_GetObjectItemCaseSensitive(fsz, sizes[cnt])->valuedouble;
  }
  t_print("%s: Fontsizes: %f, %f, %f, %f\n", __FUNCTION__, FSZ[0], FSZ[1], FSZ[2], FSZ[3]); 

  // access Colours
  char clrs[26][4] = {
  "ALW", "ALM", "ATW", "ATT", "OKW", "COK", "PFI", "PLW",
  "PLI", "P60", "MBG", "PBG", "VBG", "SHD", "MTR", "G1W",
  "G2W", "G3W", "G4W", "GR1", "GR2", "GR3", "GR4", "PF1",
  "PF2", "PF3" 
  };
  char rgba[4][2] = { "r", "g", "b", "a"};
  for (cnt = 0; cnt < 26; cnt++) {
    color = cJSON_GetObjectItemCaseSensitive(json, clrs[cnt]);
    for (int yc = 0; yc < 4; yc++) {
      cl[cnt][yc] = cJSON_GetObjectItemCaseSensitive(color, rgba[yc])->valuedouble;
    }
  } 
  
  // access Pan Line Thicknesses
  char lth[3][4] = { "LTN" , "LTH", "LXT" };
  plt = cJSON_GetObjectItemCaseSensitive(json, "PLT");
  if (plt == NULL) { 
    error_ptr = cJSON_GetErrorPtr(); 
    if (error_ptr != NULL) { 
      t_print("%s; Error getting PLT, %s\n", __FUNCTION__, error_ptr); 
    } 
    status = 1;
    goto end; 
  } 
  for (cnt = 0; cnt < 3; cnt++) {
    PLT[cnt] = cJSON_GetObjectItemCaseSensitive(plt, lth[cnt])->valuedouble;
  }
  t_print("%s: Pan Line thicknesses: %f, %f, %f\n", __FUNCTION__, PLT[0], PLT[1], PLT[2]);
  status = 0;
  
end:
  cJSON_Delete(json);
  return status;
}   

