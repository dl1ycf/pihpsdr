/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

// Note that all pin numbers are now "GPIO numbers"

#ifdef GPIO

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>

#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <sys/ioctl.h>

#include "actions.h"
#include "band.h"
#include "bandstack.h"
#include "channel.h"
#include "discovered.h"
#include "ext.h"
#include "filter.h"
#include "gpio.h"
#include "i2c.h"
#include "iambic.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "new_protocol.h"
#include "property.h"
#include "radio.h"
#include "sliders.h"
#include "toolbar.h"
#include "vfo.h"

///////////////////////////////////////////////////////////////////////////
//
// ATTENTION: this code is intended to work both with the V1 and V2
// API of the libgpiod library. V1-specific code is encapsulated
// with "ifdef GPIOV1" and V2-specific code with "ifdef GPIOV2".
// "ifdef GPIO" is a synonym for "if defined(GPIOV1) || defined(GPIOV2)"
//
// As of now, the code is not yet instrumented to work with GPIOV2
//
///////////////////////////////////////////////////////////////////////////
//
// for controllers which have spare GPIO lines,
// these lines can be associated to certain
// functions, namely
//
// CWL:      input:  left paddle for internal (iambic) keyer
// CWR:      input:  right paddle for internal (iambic) keyer
// CWKEY:    input:  key-down from external keyer
// PTTIN:    input:  PTT from external keyer or microphone
// PTTOUT:   output: PTT output (indicating TX status)
//
// a value < 0 indicates "do not use". All inputs are active-low,
// but PTTOUT is active-high
//
// Avoid using GPIO lines 18, 19, 20, 21 since they are used for I2S
// by some GPIO-connected audio output "hats"
//
//
///////////////////////////////////////////////////////////////////////////

static int CWL_LINE = -1;
static int CWR_LINE = -1;
static int CWKEY_LINE = -1;
static int PTTIN_LINE = -1;
static int PTTOUT_LINE = -1;
static int CWOUT_LINE = -1;

#ifdef GPIOV1
  static struct gpiod_line *pttout_line = NULL;
  static struct gpiod_line *cwout_line = NULL;
#endif
#ifdef GPIOV2
  static struct gpiod_line_request *output_request = NULL;
  static struct gpiod_line_request *input_request = NULL;
#endif

void gpio_set_ptt(int state) {
#ifdef GPIOV1
  if (pttout_line) {
    if (gpiod_line_set_value(pttout_line, NOT(state)) < 0) {
      t_print("%s failed: %s\n", __FUNCTION__, g_strerror(errno));
    }
  }
#endif

#ifdef GPIOV2
  if (output_request && PTTOUT_LINE >= 0) {
    gpiod_line_request_set_value(output_request, PTTOUT_LINE,
              state ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
  }
#endif
}

void gpio_set_cw(int state) {
#ifdef GPIOV1
  if (cwout_line) {
    if (gpiod_line_set_value(cwout_line, NOT(state)) < 0) {
      t_print("%s failed: %s\n", __FUNCTION__, g_strerror(errno));
    }
  }
#endif

#ifdef GPIOV2
  if (output_request && CWOUT_LINE >= 0) {
    gpiod_line_request_set_value(output_request, CWOUT_LINE,
              state ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
  }
#endif
}

enum {
  TOP_ENCODER,
  BOTTOM_ENCODER
};

enum {
  A,
  B
};

#define DIR_NONE 0x0
// Clockwise step.
#define DIR_CW 0x10
// Anti-clockwise step.
#define DIR_CCW 0x20

//
// Encoder states for a "full cycle"
//
#define R_START     0x00
#define R_CW_FINAL  0x01
#define R_CW_BEGIN  0x02
#define R_CW_NEXT   0x03
#define R_CCW_BEGIN 0x04
#define R_CCW_FINAL 0x05
#define R_CCW_NEXT  0x06

//
// Encoder states for a "half cycle"
//
#define R_START1    0x07
#define R_START0    0x08
#define R_CW_BEG1   0x09
#define R_CW_BEG0   0x0A
#define R_CCW_BEG1  0x0B
#define R_CCW_BEG0  0x0C

//
// Few general remarks on the state machine:
// - if the levels do not change, the machinestate does not change
// - if there is bouncing on one input line, the machine oscillates
//   between two "adjacent" states but generates at most one tick
// - if both input lines change level, move to a suitable new
//   starting point but do not generate a tick
// - if one or both of the AB lines are inverted, the same cycles
//   are passed but with a different starting point. Therefore,
//   it still works.
//
guchar encoder_state_table[13][4] = {
  //
  // A "full cycle" has the following state changes
  // (first line: line levels AB, 1=pressed, 0=released,
  //  2nd   line: state names
  //
  // clockwise:  11   -->   10   -->    00    -->    01     -->  11
  //            Start --> CWbeg  -->  CWnext  -->  CWfinal  --> Start
  //
  // ccw:        11   -->   01    -->   00     -->   10      -->  11
  //            Start --> CCWbeg  --> CCWnext  --> CCWfinal  --> Start
  //
  // Emit the "tick" when moving from "final" to "start".
  //
  //                   00           10           01          11
  // -----------------------------------------------------------------------------
  /* R_START     */ {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  /* R_CW_FINAL  */ {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  /* R_CW_BEGIN  */ {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  /* R_CW_NEXT   */ {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  /* R_CCW_BEGIN */ {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  /* R_CCW_FINAL */ {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
  /* R_CCW_NEXT  */ {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
  //
  // The same sequence can be interpreted as two "half cycles"
  //
  // clockwise1:   11    -->   10   -->   00
  //             Start1  --> CWbeg1 --> Start0
  //
  // clockwise2:   00    -->   01   -->   11
  //             Start0  --> CWbeg0 --> Start1
  //
  // ccw1:         11    -->   01    -->   00
  //             Start1  --> CCWbeg1 --> Start0
  //
  // ccw2:         00    -->   10    -->   11
  //             Start0  --> CCWbeg0 --> Start1
  //
  // If both lines change, this is interpreted as a two-step move
  // without changing the orientation and without emitting a "tick".
  //
  // Emit the "tick" each time when moving from "beg" to "start".
  //
  //                   00                    10          01         11
  // -----------------------------------------------------------------------------
  /* R_START1    */ {R_START0,           R_CW_BEG1,  R_CCW_BEG1, R_START1},
  /* R_START0    */ {R_START0,           R_CCW_BEG0, R_CW_BEG0,  R_START1},
  /* R_CW_BEG1   */ {R_START0 | DIR_CW,  R_CW_BEG1,  R_CW_BEG0,  R_START1},
  /* R_CW_BEG0   */ {R_START0,           R_CW_BEG1,  R_CW_BEG0,  R_START1 | DIR_CW},
  /* R_CCW_BEG1  */ {R_START0 | DIR_CCW, R_CCW_BEG0, R_CCW_BEG1, R_START1},
  /* R_CCW_BEG0  */ {R_START0,           R_CCW_BEG0, R_CCW_BEG1, R_START1 | DIR_CCW},
};

  const char *consumer = "pihpsdr";

  //
  // gpio_init() tries several chips, until success.
  // gpio_device is then the path to the first device sucessfully opened.
  //
  static char *gpio_device = NULL;
  static struct gpiod_chip *chip = NULL;

//
// The "static const" data is the DEFAULT assignment for encoders,
// and for Controller2 and G2 front panel switches
// These defaults are read-only and copied to my_encoders and my_switches
// when restoring default values
//
// Controller1 has 3 small encoders + VFO, and  8 switches in 6 layers
// Controller2 has 4 small encoders + VFO, and 16 switches
// G2 panel    has 4 small encoders + VFO, and 16 switches
//
// The controller1 switches are hard-wired to the toolbar buttons
//

//
// RPI5: GPIO line 20 not available, replace "20" by "14" at four places in the following lines
//       and re-wire the controller connection from GPIO20 to GPIO14
//
static const ENCODER encoders_no_controller[MAX_ENCODERS] = {
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
  {FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0L},
};

static const ENCODER encoders_controller1[MAX_ENCODERS] = {
  {TRUE,  TRUE, 20, 1, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 25, MENU_BAND,       0L},
  {TRUE,  TRUE, 16, 1, 19, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE,  8, MENU_BANDSTACK,  0L},
  {TRUE,  TRUE,  4, 1, 21, 1, 0, DRIVE,    R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE,  7, MENU_MODE,       0L},
  {TRUE,  TRUE, 18, 1, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,       0L},
  {FALSE, TRUE, 0, 1,  0, 0, 1, NO_ACTION, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,       0L},
};

static const ENCODER encoders_controller2_v1[MAX_ENCODERS] = {
  {TRUE, TRUE, 20, 1, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 22, MENU_BAND,      0L},
  {TRUE, TRUE,  4, 1, 21, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 27, MENU_BANDSTACK, 0L},
  {TRUE, TRUE, 16, 1, 19, 1, 0, IF_WIDTH, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 23, MENU_MODE,      0L},
  {TRUE, TRUE, 25, 1,  8, 1, 0, RIT,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L},
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE,  0, NO_ACTION,      0L},
};

static const ENCODER encoders_controller2_v2[MAX_ENCODERS] = {
  {TRUE, TRUE,  5, 1,  6, 1, 0, AGC_GAIN_RX1, R_START1, TRUE,  TRUE, 26, 1, 20, 1, 0, AF_GAIN_RX1, R_START1, TRUE,  TRUE, 22, RX1,            0L}, //ENC2
  {TRUE, TRUE,  9, 1,  7, 1, 0, AGC_GAIN_RX2, R_START1, TRUE,  TRUE, 21, 1,  4, 1, 0, AF_GAIN_RX2, R_START1, TRUE,  TRUE, 27, RX2,            0L}, //ENC3
  {TRUE, TRUE, 11, 1, 10, 1, 0, DIV_GAIN,     R_START1, TRUE,  TRUE, 19, 1, 16, 1, 0, DIV_PHASE,   R_START1, TRUE,  TRUE, 23, DIV,            0L}, //ENC4
  {TRUE, TRUE, 13, 1, 12, 1, 0, XIT,          R_START1, TRUE,  TRUE,  8, 1, 25, 1, 0, RIT,         R_START1, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L}, //ENC5
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,          R_START,  FALSE, TRUE,  0, 0,  0, 0, 0, NO_ACTION,   R_START, FALSE,  TRUE,  0, NO_ACTION,      0L}, //ENC1/VFO
};

static const ENCODER encoders_g2_frontpanel[MAX_ENCODERS] = {
  {TRUE, TRUE,  5, 1,  6, 1, 0, DRIVE,    R_START1, TRUE,  TRUE, 26, 1, 20, 1, 0, MIC_GAIN,  R_START1, TRUE,  TRUE, 22, PS,             0L}, //ENC1
  {TRUE, TRUE,  9, 1,  7, 1, 0, AGC_GAIN, R_START1, TRUE,  TRUE, 21, 1,  4, 1, 0, AF_GAIN,   R_START1, TRUE,  TRUE, 27, MUTE,           0L}, //ENC3
  {TRUE, TRUE, 11, 1, 10, 1, 0, DIV_GAIN, R_START1, TRUE,  TRUE, 19, 1, 16, 1, 0, DIV_PHASE, R_START1, TRUE,  TRUE, 23, DIV,            0L}, //ENC7
  {TRUE, TRUE, 13, 1, 12, 1, 0, XIT,      R_START1, TRUE,  TRUE,  8, 1, 25, 1, 0, RIT,       R_START1, TRUE,  TRUE, 24, MENU_FREQUENCY, 0L}, //ENC5
  {TRUE, TRUE, 18, 1, 17, 1, 0, VFO,      R_START,  FALSE, TRUE,  0, 0,  0, 0, 0, 0,         R_START, FALSE,  TRUE,  0, NO_ACTION,      0L}, //VFO
};

static const SWITCH switches_no_controller[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L}
};

//
// All toolbar-related stuff is now removed from gpio.c:
// The eight push-buttons of controller1 are
// hard-wired to the commands TOOLBAR1-7 and FUNCTION
//
static const SWITCH switches_controller1[MAX_SWITCHES] = {
  {TRUE,  TRUE, 27, TOOLBAR1,  0L},
  {TRUE,  TRUE, 13, TOOLBAR2,  0L},
  {TRUE,  TRUE, 12, TOOLBAR3,  0L},
  {TRUE,  TRUE,  6, TOOLBAR4,  0L},
  {TRUE,  TRUE,  5, TOOLBAR5,  0L},
  {TRUE,  TRUE, 24, TOOLBAR6,  0L},
  {TRUE,  TRUE, 23, TOOLBAR7,  0L},
  {TRUE,  TRUE, 22, FUNCTION,  0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, NO_ACTION, 0L}
};

static const SWITCH switches_controller2_v1[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, MOX,              0L},
  {FALSE, FALSE, 0, TUNE,             0L},
  {FALSE, FALSE, 0, PS,               0L},
  {FALSE, FALSE, 0, TWO_TONE,         0L},
  {FALSE, FALSE, 0, NR,               0L},
  {FALSE, FALSE, 0, A_TO_B,           0L},
  {FALSE, FALSE, 0, B_TO_A,           0L},
  {FALSE, FALSE, 0, MODE_MINUS,       0L},
  {FALSE, FALSE, 0, BAND_MINUS,       0L},
  {FALSE, FALSE, 0, MODE_PLUS,        0L},
  {FALSE, FALSE, 0, BAND_PLUS,        0L},
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},
  {FALSE, FALSE, 0, NB,               0L},
  {FALSE, FALSE, 0, SNB,              0L},
  {FALSE, FALSE, 0, LOCK,             0L},
  {FALSE, FALSE, 0, CTUN,             0L}
};

static const SWITCH switches_controller2_v2[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, MOX,              0L},  //GPB7 SW2
  {FALSE, FALSE, 0, TUNE,             0L},  //GPB6 SW3
  {FALSE, FALSE, 0, PS,               0L},  //GPB5 SW4
  {FALSE, FALSE, 0, TWO_TONE,         0L},  //GPB4 SW5
  {FALSE, FALSE, 0, NR,               0L},  //GPA3 SW6
  {FALSE, FALSE, 0, NB,               0L},  //GPB3 SW14
  {FALSE, FALSE, 0, SNB,              0L},  //GPB2 SW15
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, MODE_PLUS,        0L},  //GPA5 SW11
  {FALSE, FALSE, 0, BAND_MINUS,       0L},  //GPA4 SW10
  {FALSE, FALSE, 0, MODE_MINUS,       0L},  //GPA0 SW9
  {FALSE, FALSE, 0, A_TO_B,           0L},  //GPA2 SW7
  {FALSE, FALSE, 0, B_TO_A,           0L},  //GPA1 SW8
  {FALSE, FALSE, 0, LOCK,             0L},  //GPB1 SW16
  {FALSE, FALSE, 0, CTUN,             0L}   //GPB0 SW17
};

static const SWITCH switches_g2_frontpanel[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, XIT_ENABLE,       0L},  //GPB7 SW22
  {FALSE, FALSE, 0, RIT_ENABLE,       0L},  //GPB6 SW21
  {FALSE, FALSE, 0, FUNCTION,         0L},  //GPB5 SW20
  {FALSE, FALSE, 0, SPLIT,            0L},  //GPB4 SW19
  {FALSE, FALSE, 0, LOCK,             0L},  //GPA3 SW9
  {FALSE, FALSE, 0, B_TO_A,           0L},  //GPB3 SW18
  {FALSE, FALSE, 0, A_TO_B,           0L},  //GPB2 SW17
  {FALSE, FALSE, 0, MODE_MINUS,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, FILTER_PLUS,      0L},  //GPA5 SW11
  {FALSE, FALSE, 0, MODE_PLUS,        0L},  //GPA4 SW10
  {FALSE, FALSE, 0, MOX,              0L},  //GPA0 SW6
  {FALSE, FALSE, 0, CTUN,             0L},  //GPA2 SW8
  {FALSE, FALSE, 0, TUNE,             0L},  //GPA1 SW7
  {FALSE, FALSE, 0, BAND_MINUS,       0L},  //GPB1 SW16
  {FALSE, FALSE, 0, FILTER_MINUS,     0L}   //GPB0 SW15
};

ENCODER my_encoders[MAX_ENCODERS];
SWITCH  my_switches[MAX_SWITCHES];

ENCODER *encoders = NULL;
SWITCH *switches = NULL;

#define I2C_INTERRUPT  15
#define MAX_LINES 32

static GMutex encoder_mutex;
static GThread *monitor_thread_id;
static GThread *rotary_encoder_thread_id;

static int num_input_lines = 0;
static int input_lines[MAX_LINES];    // GPIO number (offset) of line
static int input_pullup[MAX_LINES];   // has pullup?
static int input_debounce[MAX_LINES]; // debouncing time (msec)

static int num_output_lines = 0;
static int output_lines[MAX_LINES];         // GPIO number (offset) of line
static int output_initial_state[MAX_LINES]; // initial state (high = 1, low = 0)

#ifdef GPIOV1
//
// All the timing is for the "pedestrian way" debouncing with libgpiod V1
//
static uint64_t epochMilli;
static long settle_time = 50; // ms

static void initialiseEpoch() {
  struct timespec ts ;
  clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
  epochMilli = (uint64_t)ts.tv_sec * (uint64_t)1000    + (uint64_t)(ts.tv_nsec / 1000000L) ;
}

static unsigned int millis () {
  uint64_t now ;
  struct  timespec ts ;
  clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
  now  = (uint64_t)ts.tv_sec * (uint64_t)1000 + (uint64_t)(ts.tv_nsec / 1000000L) ;
  return (uint32_t)(now - epochMilli) ;
}
#endif

static gpointer rotary_encoder_thread(gpointer data) {
  int i;
  enum ACTION action;
  enum ACTION_MODE mode;
  int val;
  usleep(250000);

  while (TRUE) {
    g_mutex_lock(&encoder_mutex);

    for (i = 0; i < MAX_ENCODERS; i++) {
      if (encoders[i].bottom_encoder_enabled && encoders[i].bottom_encoder_pos != 0) {
        action = encoders[i].bottom_encoder_function;
        mode = RELATIVE;
        val = encoders[i].bottom_encoder_pos;
        encoders[i].bottom_encoder_pos = 0;
        schedule_action(action, mode, val);
      }

      if (encoders[i].top_encoder_enabled && encoders[i].top_encoder_pos != 0) {
        action = encoders[i].top_encoder_function;
        mode = RELATIVE;
        val = encoders[i].top_encoder_pos;
        encoders[i].top_encoder_pos = 0;
        schedule_action(action, mode, val);
      }
    }

    g_mutex_unlock(&encoder_mutex);
    usleep(100000); // sleep for 100ms
  }

  return NULL;
}

static void process_encoder(int e, int l, int addr, int val) {
  guchar pinstate;
  g_mutex_lock(&encoder_mutex);

  switch (l) {
  case BOTTOM_ENCODER:
    switch (addr) {
    case A:
      encoders[e].bottom_encoder_a_value = val;
      pinstate = (encoders[e].bottom_encoder_b_value << 1) | encoders[e].bottom_encoder_a_value;
      encoders[e].bottom_encoder_state = encoder_state_table[encoders[e].bottom_encoder_state & 0xf][pinstate];

      switch (encoders[e].bottom_encoder_state & 0x30) {
      case DIR_NONE:
        break;

      case DIR_CW:
        encoders[e].bottom_encoder_pos++;
        break;

      case DIR_CCW:
        encoders[e].bottom_encoder_pos--;
        break;

      default:
        break;
      }

      break;

    case B:
      encoders[e].bottom_encoder_b_value = val;
      pinstate = (encoders[e].bottom_encoder_b_value << 1) | encoders[e].bottom_encoder_a_value;
      encoders[e].bottom_encoder_state = encoder_state_table[encoders[e].bottom_encoder_state & 0xf][pinstate];

      switch (encoders[e].bottom_encoder_state & 0x30) {
      case DIR_NONE:
        break;

      case DIR_CW:
        encoders[e].bottom_encoder_pos++;
        break;

      case DIR_CCW:
        encoders[e].bottom_encoder_pos--;
        break;

      default:
        break;
      }

      break;
    }

    break;

  case TOP_ENCODER:
    switch (addr) {
    case A:
      encoders[e].top_encoder_a_value = val;
      pinstate = (encoders[e].top_encoder_b_value << 1) | encoders[e].top_encoder_a_value;
      encoders[e].top_encoder_state = encoder_state_table[encoders[e].top_encoder_state & 0xf][pinstate];

      switch (encoders[e].top_encoder_state & 0x30) {
      case DIR_NONE:
        break;

      case DIR_CW:
        encoders[e].top_encoder_pos++;
        break;

      case DIR_CCW:
        encoders[e].top_encoder_pos--;
        break;

      default:
        break;
      }

      break;

    case B:
      encoders[e].top_encoder_b_value = val;
      pinstate = (encoders[e].top_encoder_b_value << 1) | encoders[e].top_encoder_a_value;
      encoders[e].top_encoder_state = encoder_state_table[encoders[e].top_encoder_state & 0xf][pinstate];

      switch (encoders[e].top_encoder_state & 0x30) {
      case DIR_NONE:
        break;

      case DIR_CW:
        encoders[e].top_encoder_pos++;
        break;

      case DIR_CCW:
        encoders[e].top_encoder_pos--;
        break;

      default:
        break;
      }

      break;
    }

    break;
  }

  g_mutex_unlock(&encoder_mutex);
}

static void process_edge(int offset, int value) {
  //
  // Priority 1 (highst): check encoder
  //
  for (int i = 0; i < MAX_ENCODERS; i++) {
    if (encoders[i].bottom_encoder_enabled && encoders[i].bottom_encoder_address_a == offset) {
      process_encoder(i, BOTTOM_ENCODER, A, SET(value == PRESSED));
      return;
    } else if (encoders[i].bottom_encoder_enabled && encoders[i].bottom_encoder_address_b == offset) {
      process_encoder(i, BOTTOM_ENCODER, B, SET(value == PRESSED));
      return;
    } else if (encoders[i].top_encoder_enabled && encoders[i].top_encoder_address_a == offset) {
      process_encoder(i, TOP_ENCODER, A, SET(value == PRESSED));
      return;
    } else if (encoders[i].top_encoder_enabled && encoders[i].top_encoder_address_b == offset) {
      process_encoder(i, TOP_ENCODER, B, SET(value == PRESSED));
      return;
    } else if (encoders[i].switch_enabled && encoders[i].switch_address == offset) {
#ifdef GPIOV1
      unsigned int t;
      t = millis();

      if (t < encoders[i].switch_debounce) { return; }

      encoders[i].switch_debounce = t + settle_time;
#endif
      schedule_action(encoders[i].switch_function, value, 0);
      return;
    }
  }

  //
  // Priority 2: check "non-controller" inputs
  // take care for "external" debouncing!
  //
  if (offset == CWL_LINE) {
    schedule_action(CW_LEFT, value, 0);
    return;
  }

  if (offset == CWR_LINE) {
    schedule_action(CW_RIGHT, value, 0);
    return;
  }

  if (offset == CWKEY_LINE) {
    schedule_action(CW_KEYER_KEYDOWN, value, 0);
    return;
  }

  if (offset == PTTIN_LINE) {
    schedule_action(CW_KEYER_PTT, value, 0);
    return;
  }

  //
  // Priority 3: handle i2c interrupt and i2c switches
  //
  if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL) {
    if (I2C_INTERRUPT == offset) {
      if (value == PRESSED) {
        i2c_interrupt();
      }

      return;
    }
  }

  //
  // Priority 4: handle "normal" (non-I2C) switches
  //
  for (int i = 0; i < MAX_SWITCHES; i++) {
    if (switches[i].switch_enabled && switches[i].switch_address == offset) {
#ifdef GPIOV1
      unsigned int t;
      t = millis();

      if (t < switches[i].switch_debounce) { return; }

      switches[i].switch_debounce = t + settle_time;
#endif
      schedule_action(switches[i].switch_function, value, 0);
      return;
    }
  }

  t_print("%s: could not find offset=%d\n", __FUNCTION__, offset);
}

#ifdef GPIOV1
// cppcheck-suppress constParameterCallback
static int interrupt_cb(int event_type, unsigned int line, const struct timespec *timeout, void* data) {
  switch (event_type) {
  case GPIOD_CTXLESS_EVENT_CB_TIMEOUT:
    // timeout - ignore
    break;

  case GPIOD_CTXLESS_EVENT_CB_RISING_EDGE:
    process_edge(line, RELEASED);
    break;

  case GPIOD_CTXLESS_EVENT_CB_FALLING_EDGE:
    process_edge(line, PRESSED);
    break;
  }

  return GPIOD_CTXLESS_EVENT_CB_RET_OK;
}

#endif

void gpio_default_encoder_actions(int ctrlr) {
  const ENCODER *default_encoders;

  switch (ctrlr) {
  case NO_CONTROLLER:
  default:
    default_encoders = NULL;
    break;

  case CONTROLLER1:
    default_encoders = encoders_controller1;
    break;

  case CONTROLLER2_V1:
    default_encoders = encoders_controller2_v1;
    break;

  case CONTROLLER2_V2:
    default_encoders = encoders_controller2_v2;
    break;

  case G2_FRONTPANEL:
    default_encoders = encoders_g2_frontpanel;
    break;
  }

  if (default_encoders) {
    //
    // Copy (only) actions
    //
    for (int i = 0; i < MAX_ENCODERS; i++) {
      my_encoders[i].bottom_encoder_function = default_encoders[i].bottom_encoder_function;
      my_encoders[i].top_encoder_function    = default_encoders[i].top_encoder_function;
      my_encoders[i].switch_function         = default_encoders[i].switch_function;
    }
  }
}

void gpio_default_switch_actions(int ctrlr) {
  const SWITCH *default_switches;

  switch (ctrlr) {
  case NO_CONTROLLER:
  case CONTROLLER1:
  default:
    default_switches = NULL;
    break;

  case CONTROLLER2_V1:
    default_switches = switches_controller2_v1;
    break;

  case CONTROLLER2_V2:
    default_switches = switches_controller2_v2;
    break;

  case G2_FRONTPANEL:
    default_switches = switches_g2_frontpanel;
    break;
  }

  if (default_switches) {
    //
    // Copy (only) actions
    //
    for (int i = 0; i < MAX_SWITCHES; i++) {
      my_switches[i].switch_function = default_switches[i].switch_function;
    }
  }
}

//
// If there is non-standard hardware at the GPIO lines
// the code below in the NO_CONTROLLER section must
// be adjusted such that "occupied" GPIO lines are not
// used for CW or PTT.
// For CONTROLLER1 and CONTROLLER2_V1, GPIO
// lines 9,10,11,14 are "free" and can be
// used for CW and PTT.
//
//  At this place, copy complete data structures to my_encoders
//  and my_switches, including GPIO lines etc.
//
void gpio_set_defaults(int ctrlr) {
  t_print("%s: Controller=%d\n", __FUNCTION__, ctrlr);

  switch (ctrlr) {
  case CONTROLLER1:
    //
    // GPIO lines not used by controller: 9, 10, 11, 14, 15
    //
    CWL_LINE = 9;
    CWR_LINE = 11;
    CWKEY_LINE = 10;
    PTTIN_LINE = 14;
    PTTOUT_LINE = 15;
    CWOUT_LINE = -1;
    memcpy(my_encoders, encoders_controller1, sizeof(my_encoders));
    memcpy(my_switches, switches_controller1, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;

  case CONTROLLER2_V1:
    //
    // GPIO lines not used by controller: 5, 6, 7, 9, 10, 11, 12, 13, 14
    //
    CWL_LINE = 9;
    CWR_LINE = 11;
    CWKEY_LINE = 10;
    PTTIN_LINE = 14;
    PTTOUT_LINE = 13;
    CWOUT_LINE = 12;
    memcpy(my_encoders, encoders_controller2_v1, sizeof(my_encoders));
    memcpy(my_switches, switches_controller2_v1, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;

  case CONTROLLER2_V2:
    //
    // GPIO lines not used by controller: 14. Assigned to PTTIN by default
    //
    CWL_LINE = -1;
    CWR_LINE = -1;
    PTTIN_LINE = 14;
    CWKEY_LINE = -1;
    PTTOUT_LINE = -1;
    memcpy(my_encoders, encoders_controller2_v2, sizeof(my_encoders));
    memcpy(my_switches, switches_controller2_v2, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;

  case G2_FRONTPANEL:
    //
    // Regard all GPIO lines as "used"
    //
    CWL_LINE = -1;
    CWR_LINE = -1;
    PTTIN_LINE = -1;
    CWKEY_LINE = -1;
    PTTOUT_LINE = -1;
    memcpy(my_encoders, encoders_g2_frontpanel, sizeof(my_encoders));
    memcpy(my_switches, switches_g2_frontpanel, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;

  case NO_CONTROLLER:
  default:
    //
    // GPIO lines that are not used elsewhere: 5,  6, 12, 16,
    //                                        22, 23, 24, 25, 27
    //
    CWL_LINE = 5;
    CWR_LINE = 6;
    CWKEY_LINE = 12;
    PTTIN_LINE = 16;
    PTTOUT_LINE = 22;
    CWOUT_LINE = 23;

    memcpy(my_encoders, encoders_no_controller, sizeof(my_encoders));
    memcpy(my_switches, switches_no_controller, sizeof(my_switches));
    encoders = my_encoders;
    switches = my_switches;
    break;
  }
  //
  // On some specific hardware, we may not use any of the optional GPIO lines
  //
  if (have_radioberry1) {
    CWL_LINE = 14;
    CWR_LINE = 15;
    CWKEY_LINE = -1;
    PTTIN_LINE = -1;
    PTTOUT_LINE = -1;
    CWOUT_LINE = -1;
  }

  if (have_radioberry2) {
    CWL_LINE = 17;
    CWR_LINE = 21;
    CWKEY_LINE = -1;
    PTTIN_LINE = -1;
    PTTOUT_LINE = -1;
    CWOUT_LINE = -1;
  }

  if (have_saturn_xdma) {
    CWL_LINE = -1;
    CWR_LINE = -1;
    CWKEY_LINE = -1;
    PTTIN_LINE = -1;
    PTTOUT_LINE = -1;
    CWOUT_LINE = -1;
  }
}

void gpioRestoreState() {
  loadProperties("gpio.props");
  GetPropI0("controller",                                         controller);
  gpio_set_defaults(controller);

  for (int i = 0; i < MAX_ENCODERS; i++) {
    GetPropI1("encoders[%d].bottom_encoder_enabled", i,           encoders[i].bottom_encoder_enabled);
    GetPropI1("encoders[%d].bottom_encoder_pullup", i,            encoders[i].bottom_encoder_pullup);
    GetPropI1("encoders[%d].bottom_encoder_address_a", i,         encoders[i].bottom_encoder_address_a);
    GetPropI1("encoders[%d].bottom_encoder_address_b", i,         encoders[i].bottom_encoder_address_b);
    GetPropI1("encoders[%d].top_encoder_enabled", i,              encoders[i].top_encoder_enabled);
    GetPropI1("encoders[%d].top_encoder_pullup", i,               encoders[i].top_encoder_pullup);
    GetPropI1("encoders[%d].top_encoder_address_a", i,            encoders[i].top_encoder_address_a);
    GetPropI1("encoders[%d].top_encoder_address_b", i,            encoders[i].top_encoder_address_b);
    GetPropI1("encoders[%d].switch_enabled", i,                   encoders[i].switch_enabled);
    GetPropI1("encoders[%d].switch_pullup", i,                    encoders[i].switch_pullup);
    GetPropI1("encoders[%d].switch_address", i,                   encoders[i].switch_address);
  }

  for (int i = 0; i < MAX_SWITCHES; i++) {
    GetPropI1("switches[%d].switch_enabled", i,                 switches[i].switch_enabled);
    GetPropI1("switches[%d].switch_pullup", i,                  switches[i].switch_pullup);
    GetPropI1("switches[%d].switch_address", i,                 switches[i].switch_address);
  }
}

void gpioSaveState() {
  clearProperties();
  SetPropI0("controller",                                         controller);

  for (int i = 0; i < MAX_ENCODERS; i++) {
    SetPropI1("encoders[%d].bottom_encoder_enabled", i,           encoders[i].bottom_encoder_enabled);
    SetPropI1("encoders[%d].bottom_encoder_pullup", i,            encoders[i].bottom_encoder_pullup);
    SetPropI1("encoders[%d].bottom_encoder_address_a", i,         encoders[i].bottom_encoder_address_a);
    SetPropI1("encoders[%d].bottom_encoder_address_b", i,         encoders[i].bottom_encoder_address_b);
    SetPropI1("encoders[%d].top_encoder_enabled", i,              encoders[i].top_encoder_enabled);
    SetPropI1("encoders[%d].top_encoder_pullup", i,               encoders[i].top_encoder_pullup);
    SetPropI1("encoders[%d].top_encoder_address_a", i,            encoders[i].top_encoder_address_a);
    SetPropI1("encoders[%d].top_encoder_address_b", i,            encoders[i].top_encoder_address_b);
    SetPropI1("encoders[%d].switch_enabled", i,                   encoders[i].switch_enabled);
    SetPropI1("encoders[%d].switch_pullup", i,                    encoders[i].switch_pullup);
    SetPropI1("encoders[%d].switch_address", i,                   encoders[i].switch_address);
  }

  for (int i = 0; i < MAX_SWITCHES; i++) {
    SetPropI1("switches[%d].switch_enabled", i,                 switches[i].switch_enabled);
    SetPropI1("switches[%d].switch_pullup", i,                  switches[i].switch_pullup);
    SetPropI1("switches[%d].switch_address", i,                 switches[i].switch_address);
  }

  saveProperties("gpio.props");
}

void gpioRestoreActions() {
  int props_controller = NO_CONTROLLER;
  gpio_set_defaults(controller);
  GetPropI0("controller",                                        props_controller);

  //
  // If the props file refers to another controller, skip props data
  //
  if (controller != props_controller) { return; }

  for (int i = 0; i < MAX_ENCODERS; i++) {
    GetPropA1("encoders[%d].bottom_encoder_function", i,         encoders[i].bottom_encoder_function);
    GetPropA1("encoders[%d].top_encoder_function", i,            encoders[i].top_encoder_function);
    GetPropA1("encoders[%d].switch_function", i,                 encoders[i].switch_function);
  }

  if (controller != CONTROLLER1) {
    for (int i = 0; i < MAX_SWITCHES; i++) {
      GetPropA1("switches[%d].switch_function", i,               switches[i].switch_function);
    }
  }
}

void gpioSaveActions() {
  SetPropI0("controller",                                        controller);

  //
  // If there is no controller, there is nothing to store
  //
  if (controller == NO_CONTROLLER) { return; }

  for (int i = 0; i < MAX_ENCODERS; i++) {
    SetPropA1("encoders[%d].bottom_encoder_function", i,         encoders[i].bottom_encoder_function);
    SetPropA1("encoders[%d].top_encoder_function", i,            encoders[i].top_encoder_function);
    SetPropA1("encoders[%d].switch_function", i,                 encoders[i].switch_function);
  }

  for (int i = 0; i < MAX_SWITCHES; i++) {
    SetPropA1("switches[%d].switch_function", i,               switches[i].switch_function);
  }
}

static gpointer monitor_thread(gpointer arg) {
  // thread to monitor gpio events
  t_print("%s: monitoring %d lines.\n", __FUNCTION__, num_input_lines);

  if (gpio_device == NULL) {
    return NULL;
  }

  for (int i = 0; i < num_input_lines; i++) {
    t_print("%s: Line=%u Pullup=%d Debounce=%d\n", __FUNCTION__, input_lines[i], input_pullup[i], input_debounce[i]);
  }

#ifdef GPIOV1
  struct timespec t;
  t.tv_sec = 60;
  t.tv_nsec = 0;
  int ret = gpiod_ctxless_event_monitor_multiple(
              gpio_device, GPIOD_CTXLESS_EVENT_BOTH_EDGES,
              input_lines, num_input_lines, FALSE,
              consumer, &t, NULL, interrupt_cb, NULL);

  if (ret < 0) {
    t_print("%s: ctxless event monitor failed: %s\n", __FUNCTION__, g_strerror(errno));
  }
#endif

#ifdef GPIOV2
  int event_buf_size = MAX_LINES;
  struct gpiod_edge_event_buffer *event_buffer = gpiod_edge_event_buffer_new(event_buf_size);

  if (!event_buffer) {
    t_print("%s: No Event Buffer\n", __FUNCTION__);
    return NULL;
  }

  for (;;) {

    if (!input_request) { break; }  // set to NULL in gpio_close

    int ret = gpiod_line_request_read_edge_events(input_request, event_buffer, event_buf_size);

    if (ret < 0) {
      t_print("%s: read edge returned %d\n", __FUNCTION__, ret);
      continue;
    }

    for (int i = 0; i < ret; i++) {
      struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(event_buffer, i);
      int offset = gpiod_edge_event_get_line_offset(event);

      switch (gpiod_edge_event_get_event_type(event)) {
      case GPIOD_EDGE_EVENT_RISING_EDGE:
        process_edge(offset, PRESSED);
        break;
      case GPIOD_EDGE_EVENT_FALLING_EDGE:
        process_edge(offset, RELEASED);
        break;
      default:
        t_print("%s: Unknown Edge Event\n", __FUNCTION__);
        break;
      }
    }
  }

  gpiod_edge_event_buffer_free(event_buffer);

#endif

  t_print("%s: exit\n", __FUNCTION__);
  return NULL;
}

static void setup_input_lines() {
  //
  // Set up all input lines. If a line fails, delete entry
  // GPIOV1: release lines thereafter, they will be requested again
  //         in the monitor thread.
  //
#ifdef GPIOV1
  struct gpiod_line_request_config config;
  config.consumer = consumer;
  config.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT | GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES;

  for (int i = 0; i < num_input_lines; i++) {
    struct gpiod_line *line = gpiod_chip_get_line(chip, input_lines[i]);

    if (!line) {
      t_print("%s: get line %d failed: %s\n", __FUNCTION__, input_lines[i], g_strerror(errno));
      input_lines[i] = -1;
      continue;
    }

#ifdef OLD_GPIOD
    config.flags = input_pullup[i] ? GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW : 0;
#else
    config.flags = input_pullup[i] ? GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP : GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
#endif
    if (gpiod_line_request(line, &config, 1) < 0) {
      t_print("%s: line %d gpiod_line_request failed: %s\n", __FUNCTION__, input_lines[i], g_strerror(errno));
      input_lines[i] = -1;
      continue;
    }

    gpiod_line_release(line);  // release line since the event monitor will request it later
  }
#endif

#ifdef GPIOV2
  input_request = NULL;
  struct gpiod_line_settings *settings = gpiod_line_settings_new();
  struct gpiod_line_config *lineconfig = gpiod_line_config_new();
  struct gpiod_request_config *reqcfg = gpiod_request_config_new();

  if (settings && lineconfig && reqcfg) {
    gpiod_request_config_set_consumer(reqcfg, consumer);
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);

    for (int i=0; i < num_input_lines; i++) {
      if (input_pullup[i]) {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
      } else {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED);
      }

      gpiod_line_settings_set_debounce_period_us(settings, input_debounce[i]*1000);  // in usec

      if (gpiod_line_config_add_line_settings(lineconfig, &input_lines[i], 1, settings) != 0) {
        input_lines[i] = 0;
      }
    } 

    input_request = gpiod_chip_request_lines(chip, reqcfg, lineconfig);
  }

  if (reqcfg) { gpiod_request_config_free(reqcfg); }
  if (lineconfig) { gpiod_line_config_free(lineconfig); }
  if (settings) { gpiod_line_settings_free(settings); }

#endif

  //
  // Remove any failed lines from input_lines[]
  //
  for (int i = 0; i < num_input_lines; i++) {
    if (input_lines[i] < 0) {
      num_input_lines--;
      input_lines[i] = input_lines[num_input_lines];
    }
  }

  return;
}

static void setup_output_lines() {
  //
  // Setup active-high output lines
  //
#ifdef GPIOV1
  struct gpiod_line_request_config config;
  config.consumer = consumer;
  config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
  config.flags = 0;  // active High

  for (int i = 0; i < num_output_lines; i++) {
    struct gpiod_line *line = gpiod_chip_get_line(chip, output_lines[i]);

    if (!line) {
      t_print("%s: Offset=%d failed: %s\n", __FUNCTION__, output_lines[i], g_strerror(errno));
      output_lines[i] = -1;
      continue;
    }

    if (gpiod_line_request(line, &config, output_initial_state[i]) < 0) {
      t_print("%s: Offset=%d failed: %s\n", __FUNCTION__, output_lines[i], g_strerror(errno));
      output_lines[i] = -1;
      continue;
    }

    if (output_lines[i] == PTTOUT_LINE) { pttout_line = line; }
    if (output_lines[i] == CWOUT_LINE ) { cwout_line = line; }
  }
#endif

#ifdef GPIOV2
  output_request = NULL;
  struct gpiod_line_settings *settings = gpiod_line_settings_new();
  struct gpiod_line_config *lineconfig = gpiod_line_config_new();
  struct gpiod_request_config *reqcfg = gpiod_request_config_new();

  if (settings && lineconfig && reqcfg) {
    gpiod_request_config_set_consumer(reqcfg, consumer);
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);

    for (int i = 0; i < num_output_lines; i++) {
      if (output_initial_state[i]) {
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
      } else {
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
      }

      if (gpiod_line_config_add_line_settings(lineconfig, &output_lines[i], 1, settings) != 0) {
        t_print("%s: Offset=%d failed: %s\n", __FUNCTION__, output_lines[i], g_strerror(errno));
        output_lines[i] = -1;
      }
    }

    output_request = gpiod_chip_request_lines(chip, reqcfg, lineconfig);
  }

  if (reqcfg) { gpiod_request_config_free(reqcfg); }
  if (lineconfig) { gpiod_line_config_free(lineconfig); }
  if (settings) { gpiod_line_settings_free(settings); }
#endif
}

void gpio_init() {
#ifdef GPIOV1
  initialiseEpoch();
#endif
  g_mutex_init(&encoder_mutex);
  gpio_set_defaults(controller);
  chip = NULL;

  //
  // Open GPIO device. Try several devices, until
  // there is success.
  //
  if (chip == NULL) {
    gpio_device = "/dev/gpiochip4";      // works on RPI5
    chip = gpiod_chip_open(gpio_device);
  }

  if (chip == NULL) {
    gpio_device = "/dev/gpiochip0";     // works on RPI4
    chip = gpiod_chip_open(gpio_device);
  }

  //
  // If no success so far, give up
  //
  if (chip == NULL) {
    t_print("%s: open chip failed: %s\n", __FUNCTION__, g_strerror(errno));
    return;
  }

  t_print("%s: GPIO device=%s\n", __FUNCTION__, gpio_device);

  num_input_lines = 0;
  num_output_lines = 0;

  if (controller != NO_CONTROLLER) {
    // setup encoders

    for (int i = 0; i < MAX_ENCODERS; i++) {
      if (encoders[i].bottom_encoder_enabled) {
        input_lines[num_input_lines] =  encoders[i].bottom_encoder_address_a;
        input_pullup[num_input_lines] = encoders[i].bottom_encoder_pullup;
        input_debounce[num_input_lines++] = 0;
      }

      if (encoders[i].top_encoder_enabled) {
        input_lines[num_input_lines] =  encoders[i].top_encoder_address_a;
        input_pullup[num_input_lines] = encoders[i].top_encoder_pullup;
        input_debounce[num_input_lines++] = 0;
      }

      if (encoders[i].switch_enabled) {
        input_lines[num_input_lines] =  encoders[i].switch_address;
        input_pullup[num_input_lines] = encoders[i].switch_pullup;
        input_debounce[num_input_lines++] = 25;
      }
    }

    // setup switches

    for (int i = 0; i < MAX_SWITCHES; i++) {
      if (switches[i].switch_enabled) {
        input_lines[num_input_lines] =  switches[i].switch_address;
        input_pullup[num_input_lines] = switches[i].switch_pullup;
        input_debounce[num_input_lines++] = 25;
      }
    }
  }

  if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL) {
    i2c_init();
    input_lines[num_input_lines] =  I2C_INTERRUPT;
    input_pullup[num_input_lines] = TRUE;
    input_debounce[num_input_lines++] = 0;
  }

  //
  // A failure below this point should not close the GPIO chip
  // but we simply continue without the functionality and can continue
  // to use the controller
  //

  if (CWL_LINE >= 0) {
    input_lines[num_input_lines] =  CWL_LINE;
    input_pullup[num_input_lines] = TRUE;
    input_debounce[num_input_lines++] = 10;
  }

  if (CWR_LINE >= 0) {
    input_lines[num_input_lines] =  CWR_LINE;
    input_pullup[num_input_lines] = TRUE;
    input_debounce[num_input_lines++] = 10;
  }

  if (CWKEY_LINE >= 0) {
    input_lines[num_input_lines] =  CWKEY_LINE;
    input_pullup[num_input_lines] = TRUE;
    input_debounce[num_input_lines++] = 10;
  }

  if (PTTIN_LINE >= 0) {
    input_lines[num_input_lines] =  PTTIN_LINE;
    input_pullup[num_input_lines] = TRUE;
    input_debounce[num_input_lines++] = 25;
  }

  if (PTTOUT_LINE >= 0) {
    output_lines[num_output_lines] = PTTOUT_LINE;
    output_initial_state[num_output_lines++] = 1;
  }

  if (CWOUT_LINE >= 0) {
    output_lines[num_output_lines] = CWOUT_LINE;
    output_initial_state[num_output_lines++] = 1;
  }

  if (num_output_lines > 0) {
    setup_output_lines();
  }

  if (num_input_lines > 0) {
    setup_input_lines();
    monitor_thread_id = g_thread_new( "gpiod monitor", monitor_thread, NULL);

    if (controller != NO_CONTROLLER) {
    rotary_encoder_thread_id = g_thread_new( "encoders", rotary_encoder_thread, NULL);
    }
  }

#ifdef GPIOV2
  //
  // The chip can now be closed for libgpiod V2.
  gpiod_chip_close(chip);
  chip = NULL;
#endif

  return;
}

void gpio_close() {
#ifdef GPIOV2
  if (input_request) { gpiod_line_request_release(input_request); }

  if (output_request) { gpiod_line_request_release(output_request); }
#endif

  if (chip) { gpiod_chip_close(chip); }
}

#endif
