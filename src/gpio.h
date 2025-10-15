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

#ifndef _GPIO_H_
#define _GPIO_H_

#ifdef GPIO
#include <gtk/gtk.h>
#include <stdint.h>

enum _controller_enum {
  NO_CONTROLLER = 0,
  CONTROLLER1,
  CONTROLLER2_V1,
  CONTROLLER2_V2,
  G2_FRONTPANEL
};

extern int controller;

#define MAX_ENCODERS 5
#define MAX_SWITCHES 16

typedef struct _switch {
  int enabled;
  int pullup;
  int address;
  int function;
  uint32_t debounce;
} SWITCH;

typedef struct _singleencoder {
  int enabled;
  int pullup;
  int address_a;
  int a_value;
  int address_b;
  int b_value;
  int pos;
  int function;
  int state;
} SINGLEENCODER;

typedef struct _encoder {
  SINGLEENCODER bottom;
  SINGLEENCODER top;
  SWITCH button;
} ENCODER;
  
extern ENCODER encoders[MAX_ENCODERS];
extern SWITCH switches[MAX_SWITCHES];

extern void gpio_default_encoder_actions(int ctrlr);
extern void gpio_default_switch_actions(int ctrlr);
extern void gpio_set_defaults(int ctrlr);
extern void gpioRestoreActions(void);
extern void gpioRestoreState(void);
extern void gpioSaveState(void);
extern void gpioSaveActions(void);
extern void gpio_init(void);
extern void gpio_close(void);
extern void gpio_set_ptt(int state);
extern void gpio_set_cw(int state);

#endif
#endif
