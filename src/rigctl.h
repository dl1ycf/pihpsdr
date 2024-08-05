/* Copyright (C)
*  2016 Steve Wilson <wevets@gmail.com>
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

#ifndef RIGCTL_H
#define RIGCTL_H

struct _SERIALPORT {
  //
  // parity and bits are not included, since we
  // always use 8 bits and "no parity"
  //
  char port[64];    // e.g. "/dev/ttyACM0"
  int  baud;        // baud rate
  int  enable;      // is it enabled?
  int  andromeda;   // flag for handling ANDROMEDA console
  int  autoreporting;
};

typedef struct _SERIALPORT SERIALPORT;

#define MAX_SERIAL 2
extern SERIALPORT SerialPorts[2];
extern gboolean rigctl_debug;

void launch_tcp_rigctl (void);
int launch_serial_rigctl (int id);
void disable_serial_rigctl (int id);

void  shutdown_tcp_rigctl(void);
int   rigctlGetMode(void);
int   lookup_band(int);
char * rigctlGetFilter(void);
void set_freqB(long long);
extern int cat_control;
int set_alc(gpointer);
extern int rigctl_busy;

extern unsigned int rigctl_tcp_port;
extern int rigctl_tcp_enable;
extern int rigctl_tcp_andromeda;
extern int rigctl_tcp_autoreporting;

#endif // RIGCTL_H
