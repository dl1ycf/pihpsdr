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
 * Header file to use t_print()
 */

#include <gdk/gdk.h>

//
// if TPRINTDEBUG is defined, then t_print() and t_perror() are converted to simple
// printf() and perror() calls, then the compiler can check for the correct ordering
// of the var-args
//
//#define TPRINTDEBUG 1

#ifdef TPRINTDEBUG
#define t_print printf
#define t_perror perror
#else
extern void t_print(const gchar *format, ...);
extern void t_perror(const gchar *string);
#endif
