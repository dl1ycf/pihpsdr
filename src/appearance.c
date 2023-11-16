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
#include <stdio.h>
#include <cjson/cJSON.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "appearance.h"
#include "message.h"

int vfo_layout = 0; // chosen lay out
int nr_layouts = 4; // initialize number of layouts in json file

VFO_BAR_LAYOUT vfo_layout_list[MAX_LAYOUTS]; // list of layouts to be filled

int parse_vfo_layouts( char* filename) {
  char lout[4], lo_name[32];
  int cnt = 0, status = 0, len, it, fd, fmt;
  const char *error_ptr;
  struct stat buf;
  off_t fsize;
  cJSON *name_j, *lo, *color;
  char vkey[NR_ITEMS][9] = { 
    "min_w",    "min_h",
    "size1", "size2", "size3",
    "vfo_a_x",  "vfo_a_y",
    "vfo_b_x",  "vfo_b_y",
    "mode_x",   "mode_y",
    "zoom_x",   "zoom_y",
    "ps_x",     "ps_y",
    "rit_x",    "rit_y",
    "xit_x",    "xit_y",
    "nb_x",     "nb_y",
    "nr_x",     "nr_y",
    "anf_x",    "anf_y",
    "snb_x",    "snb_y",
    "agc_x",    "agc_y",
    "cmpr_x",   "cmpr_y",
    "eq_x",     "eq_y",
    "div_x",    "div_y",
    "step_x",   "step_y",
    "ctun_x",   "ctun_y",
    "cat_x",    "cat_y",
    "vox_x",    "vox_y",
    "lock_x",   "lock_y",
    "split_x",  "split_y",
    "sat_x",    "sat_y",
    "dup_x",    "dup_y",
    "filter_x", "filter_y" 
  };
  char fsz[NR_FONTSIZES][5] = {
    "fSZ1" , "fSZ2", "fSZ3", "fSZ4"
  };
  char ckey[NR_COLOURS][5] = {   
    "cALW", "cALM", "cATW",
    "cATT", "cOKW", "cCOK",
    "cPFI", "cPLW", "cPLI",
    "cP60", "cMBG", "cPBG",
    "cVBG", "cSHD", "cMTR",
    "cG1W", "cG2W", "cG3W",
    "cG4W", "cGR1", "cGR2",
    "cGR3", "cGR4", "cPF1",
    "cPF2", "cPF3"
  };
  char rgba[4][2] = { 
    "r", "g", "b", "a"
  };
  char lth[NR_LINE_THICKNESSES][5] = {
    "tLTN" , "tLTH", "tLXT"
  };
  
  // open the file 
  FILE *fp = fopen(filename, "r"); 
  if (fp == NULL) { 
    t_print("%s; Error: Unable to open vfo_layout.json.\n", __FUNCTION__); 
    status = 1;
    goto end; 
  }
  
  // get size of json file from file system
  fd = fileno(fp);
  fstat(fd, &buf);
  fsize = buf.st_size;
  t_print("%s; Size of vfo_layout.json: %d B\n", __FUNCTION__, fsize);
  
  // create and allocate a buffer that can fit the json file
  char *buffer = (char*) malloc(sizeof(char) * fsize); // allocate buffer.
  // read the file contents into a string 
  len = fread(buffer, 1, fsize, fp);
  fclose(fp);
  t_print("%s; Bytes read from json; %d\n", __FUNCTION__, len);
  
  // parse the JSON data 
  cJSON *json = cJSON_Parse(buffer);
  free(buffer);
  if (json == NULL) { 
    error_ptr = cJSON_GetErrorPtr(); 
    if (error_ptr != NULL) { 
      t_print("%s; Error parsing json data; %s\n", __FUNCTION__, error_ptr); 
    }
    status = 1;
    goto end; 
  } 

  // Check json format compatibility with this version of pihpsdr
  cJSON *format = cJSON_GetObjectItemCaseSensitive(json, "format");   
  if (cJSON_IsNumber(format) && (format->valueint > 0)) {
    fmt = format->valueint;
    if ((fmt < FORMAT_MIN) || (fmt > FORMAT_MAX)) {
      t_print("%s; Error, json format found: %d\n", __FUNCTION__, fmt);
      t_print("%s; Required: more or equal %d, less or equal %d\n", __FUNCTION__,
        FORMAT_MIN, FORMAT_MAX);
      status = 1;
      goto end;       
    }
    t_print("%s; Compatible layout format found in json.\n", __FUNCTION__);
  } else {
    t_print("%s; Error, no format version found in json.\n", __FUNCTION__);
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
    // get description of layout
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(lo, "description" ))) {
      strncpy(vfo_layout_list[cnt].description, cJSON_GetObjectItemCaseSensitive(lo, "description" )->valuestring, 64);
    } else {
        t_print("%s; Error getting description %s\n", __FUNCTION__, lo_name);
        status = 1;
        goto end;    
    }
    
    // get 'int' type data values
    for ( it = 0; it < NR_ITEMS; it++) {
      if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(lo, vkey[it]))) {
        vfo_layout_list[cnt].itm[it] = cJSON_GetObjectItemCaseSensitive(lo, vkey[it] )->valueint;
      } else {
        t_print("%s; Error getting item %s\n", __FUNCTION__, vkey[it]);
        status = 1;
        goto end;
      }
    }
    
    // get font name
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(lo, "FNT"))) {
      snprintf(vfo_layout_list[cnt].FNT, 32, cJSON_GetObjectItemCaseSensitive(lo, "FNT")->valuestring);
    } else {
      t_print("%s; Error getting font name in layout nr. %d\n", __FUNCTION__, cnt + 1);
      status = 1;
      goto end;
    }

    // get font sizes
    for ( it = 0; it < NR_FONTSIZES; it++) {
      if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(lo, fsz[it]))) {
        vfo_layout_list[cnt].FSZ[it] = cJSON_GetObjectItemCaseSensitive(lo, fsz[it] )->valuedouble;
      } else {
        t_print("%s; Error getting font size %d in layout nr. %d\n", __FUNCTION__, fsz[it], cnt + 1);
        status = 1;
        goto end;
      }
    }

    // get colour definitions
    for (it = 0; it < NR_COLOURS; it++) {
      if (cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(lo, ckey[it]))) {
        color = cJSON_GetObjectItemCaseSensitive(lo, ckey[it]);
      } else {
        t_print("%s; Error getting colour %s in layout nr. %d\n", __FUNCTION__, ckey[it], cnt + 1);
        status = 1;
        goto end;
      }      
      for (int yc = 0; yc < 4; yc++) {
        if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(color, rgba[yc]))) {
          vfo_layout_list[cnt].CLR[it][yc] = cJSON_GetObjectItemCaseSensitive(color, rgba[yc])->valuedouble;
        } else {
          t_print("%s; Error getting colour element %s in colour %s in layout nr. %d\n", __FUNCTION__, rgba[yc], ckey[it], cnt + 1);
          status = 1;
          goto end;        
        }
      }
    } 

    // get line widths
    for ( it = 0; it < NR_LINE_THICKNESSES; it++) {
      if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(lo, lth[it]))) {
        vfo_layout_list[cnt].PLT[it] = cJSON_GetObjectItemCaseSensitive(lo, lth[it] )->valuedouble;
      } else {
        t_print("%s; Error getting line thickness %s in layout nr. %d\n", __FUNCTION__, lth[it], cnt + 1);
        status = 1;
        goto end;
      }
    }
  }
  t_print("%s; Success parsing %s\n",__FUNCTION__, filename);
end:
  cJSON_Delete(json);
  return status;
}
