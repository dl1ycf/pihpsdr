/* Copyright (C)
*  2023 - Christoph van Wüllen, DL1YCF
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
 * Hook for logging messages to the output file
 *
 * This can be redirected with any method g_print()
 * can be re-directed.
 *
 * t_print
 *           is a g_print() but it puts a time stamp in front.
 * t_perror
 *           is a perror() replacement, it puts a time stamp in font
 *           and reports via g_print
 *
 * Note ALL messages of the program should go through these two functions
 * so it is easy to either silence them completely, or routing them to
 * a separate window for debugging purposes.
 */

#include <gdk/gdk.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/time.h>

void t_print(const gchar *format, ...) {
  va_list(args);
  va_start(args, format);
  struct timeval tv;
  char line[1024];
  gettimeofday(&tv, NULL);
  //
  // convert to hh:mm:ss.mmm. Wrap around at midnight
  //
  int sec = (int) (tv.tv_sec % 86400);
  int hrs = sec / 3600;
  sec -= 3600 * hrs;
  int min = sec / 60;
  sec -= 60 * min;
  int ms = (int) (tv.tv_usec / 1000);
  //
  // We have to use vsnprintf to handle the varargs stuff
  // g_print() seems to be thread-safe but call it only ONCE.
  //
  vsnprintf(line, sizeof(line), format, args);
  g_print("%02d:%02d:%02d.%03d %s", hrs, min, sec, ms, line);
}

void t_perror(const gchar *string) {
  t_print("%s: %s\n", string, strerror(errno));
}
