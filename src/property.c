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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "main.h"
#include "message.h"
#include "property.h"
#include "radio.h"

PROPERTY* properties = NULL;

//
// Set when a configuration file written by a different PROPERTY_VERSION was
// loaded. In that case the settings are NOT discarded (they are migrated
// forward) but a timestamped backup of the original file is written and the
// operator is notified.
//
int property_version_mismatch = 0;
double property_old_version = 0.0;
char property_backup_path[512] = "";

//
// Write a timestamped, verbatim backup copy of a property file. This is used
// before we accept a configuration file that was written by a different
// PROPERTY_VERSION, so the operator can always recover the original settings.
//
static void backup_property_file(const char* filename, double oldversion) {
  char backup[512];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char stamp[32];

  if (tm != NULL) {
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
  } else {
    snprintf(stamp, sizeof(stamp), "%ld", (long) now);
  }

  snprintf(backup, sizeof(backup), "%s.v%0.2f.%s.bak", filename, oldversion, stamp);
  FILE* in = fopen(filename, "rb");

  if (in == NULL) { return; }

  FILE* out = fopen(backup, "wb");

  if (out == NULL) {
    fclose(in);
    return;
  }

  char buf[4096];
  size_t n;

  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) { break; }
  }

  fclose(in);
  fclose(out);
  snprintf(property_backup_path, sizeof(property_backup_path), "%s", backup);
}

//
// Deferred GTK notification (runs in the GTK main loop). It informs the
// operator that the configuration version changed, that the settings were
// kept, and where the backup was written.
//
static gboolean property_version_warning_cb(gpointer data) {
  GtkWidget *msg = gtk_message_dialog_new(NULL,
                   GTK_DIALOG_MODAL,
                   GTK_MESSAGE_WARNING,
                   GTK_BUTTONS_OK,
                   "piHPSDR: configuration version changed");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(msg),
      "One or more configuration files were written by a different version of "
      "piHPSDR (property version %0.2f, this program expects %0.2f).\n\n"
      "Your settings have NOT been discarded: they were kept and a timestamped "
      "backup of the previous configuration was saved next to the original "
      "file:\n%s\n\n"
      "If some settings behave unexpectedly you can restore the backup or "
      "adjust them in the menus.",
      property_old_version, (double) PROPERTY_VERSION,
      property_backup_path[0] ? property_backup_path : "(backup could not be written)");
  gtk_dialog_run(GTK_DIALOG(msg));
  gtk_widget_destroy(msg);
  return G_SOURCE_REMOVE;
}

void clearProperties(void) {
  if (properties != NULL) {
    // free all the properties
    PROPERTY *next;

    while (properties != NULL) {
      next = properties->next_property;
      g_free(properties);
      properties = next;
    }
  }
}

/* --------------------------------------------------------------------------*/
/**
* @brief Load Properties
*
* @param filename
*/
void loadProperties(const char* filename) {
  FILE* f = fopen(filename, "r");
  PROPERTY* property;
  int lines = 0;
  clearProperties();

  /////////////////////////////////////////////////////////////////////////////////////////
  //
  // TEMPORARY HOOK:
  // On Saturn XDMA, the name of the props file has originally been derived from
  // the mac address of its eth0 card. This has been changed to a fixed name now,
  // namely saturn.xdma.props.
  //
  // So, if this file does not exists, we try to load from a file derived from the mac
  // address. saveProperties later will use the new file name so this hook should be
  // used only once. So after some time, all users will have their props file
  // converted to the new name.
  //
  if (f == NULL && !strcmp(filename, "saturn.xdma.props")) {
    char oldstyle_path[128];
    snprintf(oldstyle_path, sizeof(oldstyle_path), "%02X-%02X-%02X-%02X-%02X-%02X.props",
             radio->network.mac_address[0],
             radio->network.mac_address[1],
             radio->network.mac_address[2],
             radio->network.mac_address[3],
             radio->network.mac_address[4],
             radio->network.mac_address[5]);
    f = fopen(oldstyle_path, "r");
  }

  //
  /////////////////////////////////////////////////////////////////////////////////////////

  if (f) {
    const char* value;
    const char* name;
    char string[256];
    double version = -1;

    while (fgets(string, sizeof(string), f)) {
      lines++;

      if (string[0] != '#') {
        name = strtok(string, "=");
        value = strtok(NULL, "\n");

        // Beware of "illegal" lines in corrupted files
        if (name != NULL && value != NULL) {
          property = g_new(PROPERTY, 1);

          if (!property) {
            fatal_error("FATAL: property alloc");
          } else {
            property->name = g_strdup(name);
            property->value = g_strdup(value);
            property->next_property = properties;
            properties = property;
          }

          if (strcmp(name, "property_version") == 0) {
            version = atof(value);
          }
        }
      }
    }

    if (version >= 0.0 && version != PROPERTY_VERSION) {
      //
      // The configuration file was written by a different PROPERTY_VERSION.
      // Do NOT discard the user's settings: keep the loaded properties so they
      // are migrated forward, but first write a timestamped backup of the
      // original file and remember the mismatch so the operator can be told.
      //
      static gboolean warning_scheduled = FALSE;
      backup_property_file(filename, version);
      property_version_mismatch = 1;
      property_old_version = version;
      t_print("loadProperties: %s version=%f expected=%f: settings KEPT, backup saved to %s\n",
              filename, version, PROPERTY_VERSION,
              property_backup_path[0] ? property_backup_path : "(none)");

      if (!warning_scheduled) {
        warning_scheduled = TRUE;
        g_idle_add(property_version_warning_cb, NULL);
      }
    }

    fclose(f);
  }

  t_print("loadProperties: %s, lines read: %d\n", filename, lines);
}

/* --------------------------------------------------------------------------*/
/**
* @brief Save Properties
*
* @param filename
*/
void saveProperties(const char* filename) {
  const PROPERTY* property;
  FILE* f = fopen(filename, "w+");
  char line[1024];

  if (!f) {
    t_print("can't open %s\n", filename);
    return;
  }

  snprintf(line, sizeof(line), "%0.2f", PROPERTY_VERSION);
  setProperty("property_version", line);
  property = properties;

  while (property) {
    if (*property->value) {
      snprintf(line, sizeof(line), "%s=%s\n", property->name, property->value);
      fwrite(line, 1, strlen(line), f);
    }

    property = property->next_property;
  }

  fclose(f);
}

/* --------------------------------------------------------------------------*/
/**
* @brief Get Properties
*
* @param name
*
* @return
*/
char* getProperty(const char* name) {
  char* value = NULL;
  PROPERTY* property = properties;

  while (property) {
    if (strcmp(name, property->name) == 0) {
      value = property->value;
      break;
    }

    property = property->next_property;
  }

  return value;
}

/* --------------------------------------------------------------------------*/
/**
* @brief Set Properties
*
* @param name
* @param value
*/
void setProperty(const char* name, const char* value) {
  PROPERTY* property = properties;

  while (property) {
    if (strcmp(name, property->name) == 0) {
      break;
    }

    property = property->next_property;
  }

  if (property) {
    // just update
    g_free(property->value);
    property->value = g_strdup(value);
  } else {
    // new property
    property = g_new(PROPERTY, 1);

    if (!property) {
      fatal_error("FATAL: property alloc");
    } else {
      property->name = g_strdup(name);
      property->value = g_strdup(value);
      property->next_property = properties;
      properties = property;
    }
  }
}

//
// Utility function myatof
//
// Now we force the C locale, but data in the props file may still have been written
// out using local conventions (e.g. a comma instead of a decimal point in Germany)
// To handle (at least) this case, all commas in the input string are replaced by
// decimal points and then this is fed to atof()
//
double myatof(const char* string) {
  char *lstr = g_strdup(string);
  double ret;

  //
  // Emergency fallback (will work in 99.99% of the cases)
  //
  if (lstr == NULL) {
    return atof(string);
  }

  for (char *cp = lstr; *cp; cp++) {
    if (*cp == ',') { *cp = '.'; }
  }

  ret = atof(lstr);
  g_free(lstr);
  return ret;
}

