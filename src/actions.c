/* Copyright (C)
* 2021 - John Melton, G0ORX/N6LYT
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

#include <gtk/gtk.h>
#include <math.h>

#include "actions.h"
#include "agc.h"
#include "band.h"
#include "bandstack.h"
#include "client_server.h"
#include "discovery.h"
#include "ext.h"
#include "filter.h"
#include "gpio.h"
#include "iambic.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "ps_menu.h"
#include "radio.h"
#include "receiver.h"
#include "sliders.h"
#include "store.h"
#include "toolbar.h"
#include "vfo.h"

//
// The "short button text" (button_str) needs to be present in ALL cases, and must be different
// for each case. button_str is used to identify the action in the props files and therefore
// it should not contain white space. Apart from the props files, the button_str determines
// what is written on the buttons in the toolbar (but that's it).
// For finding an action in the "action_dialog", it is most convenient if these actions are
// (roughly) sorted by the first string, but keep "NONE" at the beginning
//
ACTION_TABLE ActionTable[] = {
  {NO_ACTION,           "None",                 "NONE",         AT_NONE},
  {A_SWAP_B,            "A<>B",                 "A<>B",         AT_BTN},
  {B_TO_A,              "A<B",                  "A<B",          AT_BTN},
  {A_TO_B,              "A>B",                  "A>B",          AT_BTN},
  {AF_GAIN,             "AF Gain",              "AFGAIN",       AT_KNB | AT_ENC | AT_SLD},
  {AF_GAIN_RX1,         "AF Gain\nRX1",         "AFGAIN1",      AT_KNB | AT_ENC},
  {AF_GAIN_RX2,         "AF Gain\nRX2",         "AFGAIN2",      AT_KNB | AT_ENC},
  {AGC,                 "AGC",                  "AGCT",         AT_BTN},
  {AGC_GAIN,            "AGC Gain",             "AGCGain",      AT_KNB | AT_ENC | AT_SLD},
  {AGC_GAIN_RX1,        "AGC Gain\nRX1",        "AGCGain1",     AT_KNB | AT_ENC},
  {AGC_GAIN_RX2,        "AGC Gain\nRX2",        "AGCGain2",     AT_KNB | AT_ENC},
  {MENU_AGC,            "AGC\nMenu",            "AGC",          AT_BTN},
  {ANF,                 "ANF",                  "ANF",          AT_BTN},
  {ATTENUATION,         "Atten",                "ATTEN",        AT_KNB | AT_ENC | AT_SLD},
  {BAND_10,             "Band 10",              "10",           AT_BTN},
  {BAND_12,             "Band 12",              "12",           AT_BTN},
  {BAND_1240,           "Band 1240",            "1240",         AT_BTN},
  {BAND_136,            "Band 136",             "136",          AT_BTN},
  {BAND_144,            "Band 144",             "144",          AT_BTN},
  {BAND_15,             "Band 15",              "15",           AT_BTN},
  {BAND_160,            "Band 160",             "160",          AT_BTN},
  {BAND_17,             "Band 17",              "17",           AT_BTN},
  {BAND_20,             "Band 20",              "20",           AT_BTN},
  {BAND_220,            "Band 220",             "220",          AT_BTN},
  {BAND_2300,           "Band 2300",            "2300",         AT_BTN},
  {BAND_30,             "Band 30",              "30",           AT_BTN},
  {BAND_3400,           "Band 3400",            "3400",         AT_BTN},
  {BAND_40,             "Band 40",              "40",           AT_BTN},
  {BAND_430,            "Band 430",             "430",          AT_BTN},
  {BAND_6,              "Band 6",               "6",            AT_BTN},
  {BAND_60,             "Band 60",              "60",           AT_BTN},
  {BAND_70,             "Band 70",              "70",           AT_BTN},
  {BAND_80,             "Band 80",              "80",           AT_BTN},
  {BAND_902,            "Band 902",             "902",          AT_BTN},
  {BAND_AIR,            "Band AIR",             "AIR",          AT_BTN},
  {BAND_GEN,            "Band GEN",             "GEN",          AT_BTN},
  {BAND_MINUS,          "Band -",               "BND-",         AT_BTN},
  {BAND_PLUS,           "Band +",               "BND+",         AT_BTN},
  {BAND_WWV,            "Band WWV",             "WWV",          AT_BTN},
  {BANDSTACK_MINUS,     "BndStack -",           "BSTK-",        AT_BTN},
  {BANDSTACK_PLUS,      "BndStack +",           "BSTK+",        AT_BTN},
  {MENU_BAND,           "Band\nMenu",           "BAND",         AT_BTN},
  {MENU_BANDSTACK,      "BndStack\nMenu",       "BSTK",         AT_BTN},
  {CAPTURE,             "Capture",              "CAPTUR",       AT_BTN},
  {COMP_ENABLE,         "Cmpr On/Off",          "COMP",         AT_BTN},
  {COMPRESSION,         "Cmpr Level",           "COMPVAL",      AT_KNB | AT_ENC | AT_SLD},
  {CTUN,                "CTUN",                 "CTUN",         AT_BTN},
  {CW_AUDIOPEAKFILTER,  "CW Audio\nPeak Fltr",  "CW-APF",       AT_BTN},
  {CW_FREQUENCY,        "CW Frequency",         "CWFREQ",       AT_KNB | AT_ENC},
  {CW_LEFT,             "CW Left",              "CWL",          AT_BTN},
  {CW_RIGHT,            "CW Right",             "CWR",          AT_BTN},
  {CW_SPEED,            "CW Speed",             "CWSPD",        AT_KNB | AT_ENC | AT_SLD},
  {CW_KEYER_KEYDOWN,    "CW Key\n(Keyer)",      "CWKy",         AT_BTN},
  {CW_KEYER_PTT,        "PTT\n(CW Keyer)",      "CWKyPTT",      AT_BTN},
  {CW_KEYER_SPEED,      "Speed\n(Keyer)",       "CWKySpd",      AT_KNB},
  {DIV,                 "DIV On/Off",           "DIVT",         AT_BTN},
  {DIV_GAIN,            "DIV Gain",             "DIVG",         AT_ENC},
  {DIV_GAIN_COARSE,     "DIV Gain\nCoarse",     "DIVGC",        AT_ENC},
  {DIV_GAIN_FINE,       "DIV Gain\nFine",       "DIVGF",        AT_ENC},
  {DIV_PHASE,           "DIV Phase",            "DIVP",         AT_ENC},
  {DIV_PHASE_COARSE,    "DIV Phase\nCoarse",    "DIVPC",        AT_ENC},
  {DIV_PHASE_FINE,      "DIV Phase\nFine",      "DIVPF",        AT_ENC},
  {MENU_DIVERSITY,      "DIV\nMenu",            "DIV",          AT_BTN},
  {DUPLEX,              "Duplex",               "DUP",          AT_BTN},
  {FILTER_MINUS,        "Filter -",             "FL-",          AT_BTN},
  {FILTER_PLUS,         "Filter +",             "FL+",          AT_BTN},
  {FILTER_CUT_LOW,      "Filter Cut\nLow",      "FCUTL",        AT_ENC},
  {FILTER_CUT_HIGH,     "Filter Cut\nHigh",     "FCUTH",        AT_ENC},
  {FILTER_CUT_DEFAULT,  "Filter Cut\nDefault",  "FCUTDEF",      AT_BTN},
  {MENU_FILTER,         "Filter\nMenu",         "FILT",         AT_BTN},
  {FUNCTION,            "Function",             "FUNC",         AT_BTN},
  {FUNCTIONREV,         "FuncRev",              "FUNC-",        AT_BTN},
  {IF_SHIFT,            "IF Shift",             "IFSHFT",       AT_ENC},
  {IF_SHIFT_RX1,        "IF Shift\nRX1",        "IFSHFT1",      AT_ENC},
  {IF_SHIFT_RX2,        "IF Shift\nRX2",        "IFSHFT2",      AT_ENC},
  {IF_WIDTH,            "IF Width",             "IFWIDTH",      AT_ENC},
  {IF_WIDTH_RX1,        "IF Width\nRX1",        "IFWIDTH1",     AT_ENC},
  {IF_WIDTH_RX2,        "IF Width\nRX2",        "IFWIDTH2",     AT_ENC},
  {LINEIN_GAIN,         "Linein\nGain",         "LIGAIN",       AT_KNB | AT_ENC | AT_SLD},
  {LOCK,                "Lock",                 "LOCKM",        AT_BTN},
  {MENU_MAIN,           "Main\nMenu",           "MAIN",         AT_BTN},
  {MENU_MEMORY,         "Memory\nMenu",         "MEM",          AT_BTN},
  {MIC_GAIN,            "Mic Gain",             "MICGAIN",      AT_KNB | AT_ENC | AT_SLD},
  {MODE_MINUS,          "Mode -",               "MD-",          AT_BTN},
  {MODE_PLUS,           "Mode +",               "MD+",          AT_BTN},
  {MENU_MODE,           "Mode\nMenu",           "MODE",         AT_BTN},
  {MOX,                 "MOX",                  "MOX",          AT_BTN},
  {MULTI_ENC,           "Multi",                "MULTI",        AT_ENC},
  {MULTI_SELECT,        "Multi Action\nSelect", "MULTISEL",     AT_ENC},
  {MULTI_BUTTON,        "Multi Toggle",         "MULTIBTN",     AT_BTN},
  {MUTE,                "Mute",                 "MUTE",         AT_BTN},
  {MUTE_RX1,            "Mute RX1",             "MUTE1",        AT_BTN},
  {MUTE_RX2,            "Mute RX2",             "MUTE2",        AT_BTN},
  {NB,                  "NB",                   "NB",           AT_BTN},
  {NR,                  "NR",                   "NR",           AT_BTN},
  {MENU_NOISE,          "Noise\nMenu",          "NOISE",        AT_BTN},
  {NUMPAD_0,            "NumPad 0",             "0",            AT_BTN},
  {NUMPAD_1,            "NumPad 1",             "1",            AT_BTN},
  {NUMPAD_2,            "NumPad 2",             "2",            AT_BTN},
  {NUMPAD_3,            "NumPad 3",             "3",            AT_BTN},
  {NUMPAD_4,            "NumPad 4",             "4",            AT_BTN},
  {NUMPAD_5,            "NumPad 5",             "5",            AT_BTN},
  {NUMPAD_6,            "NumPad 6",             "6",            AT_BTN},
  {NUMPAD_7,            "NumPad 7",             "7",            AT_BTN},
  {NUMPAD_8,            "NumPad 8",             "8",            AT_BTN},
  {NUMPAD_9,            "NumPad 9",             "9",            AT_BTN},
  {NUMPAD_BS,           "NumPad\nBS",           "BS",           AT_BTN},
  {NUMPAD_CL,           "NumPad\nCL",           "CL",           AT_BTN},
  {NUMPAD_DEC,          "NumPad\nDec",          "DEC",          AT_BTN},
  {NUMPAD_KHZ,          "NumPad\nkHz",          "KHZ",          AT_BTN},
  {NUMPAD_MHZ,          "NumPad\nMHz",          "MHZ",          AT_BTN},
  {NUMPAD_ENTER,        "NumPad\nEnter",        "EN",           AT_BTN},
  {PAN,                 "PanZoom",              "PAN",          AT_KNB | AT_ENC | AT_SLD},
  {PAN_MINUS,           "Pan-",                 "PAN-",         AT_BTN},
  {PAN_PLUS,            "Pan+",                 "PAN+",         AT_BTN},
  {PANADAPTER_HIGH,     "Panadapter\nHigh",     "PANH",         AT_KNB | AT_ENC},
  {PANADAPTER_LOW,      "Panadapter\nLow",      "PANL",         AT_KNB | AT_ENC | AT_SLD},
  {PANADAPTER_STEP,     "Panadapter\nStep",     "PANS",         AT_KNB | AT_ENC},
  {PREAMP,              "Preamp\nOn/Off",       "PRE",          AT_BTN},
  {PS,                  "PS On/Off",            "PST",          AT_BTN},
  {MENU_PS,             "PS Menu",              "PS",           AT_BTN},
  {PTT,                 "PTT",                  "PTT",          AT_BTN},
  {RCL0,                "Rcl 0",                "RCL0",         AT_BTN},
  {RCL1,                "Rcl 1",                "RCL1",         AT_BTN},
  {RCL2,                "Rcl 2",                "RCL2",         AT_BTN},
  {RCL3,                "Rcl 3",                "RCL3",         AT_BTN},
  {RCL4,                "Rcl 4",                "RCL4",         AT_BTN},
  {RCL5,                "Rcl 5",                "RCL5",         AT_BTN},
  {RCL6,                "Rcl 6",                "RCL6",         AT_BTN},
  {RCL7,                "Rcl 7",                "RCL7",         AT_BTN},
  {RCL8,                "Rcl 8",                "RCL8",         AT_BTN},
  {RCL9,                "Rcl 9",                "RCL9",         AT_BTN},
  {RF_GAIN,             "RF Gain",              "RFGAIN",       AT_KNB | AT_ENC | AT_SLD},
  {RF_GAIN_RX1,         "RF Gain\nRX1",         "RFGAIN1",      AT_KNB | AT_ENC},
  {RF_GAIN_RX2,         "RF Gain\nRX2",         "RFGAIN2",      AT_KNB | AT_ENC},
  {RIT,                 "RIT",                  "RIT",          AT_ENC},
  {RIT_CLEAR,           "RIT\nClear",           "RITCL",        AT_BTN},
  {RIT_ENABLE,          "RIT\nOn/Off",          "RITT",         AT_BTN},
  {RIT_MINUS,           "RIT -",                "RIT-",         AT_BTN},
  {RIT_PLUS,            "RIT +",                "RIT+",         AT_BTN},
  {RIT_RX1,             "RIT\nRX1",             "RIT1",         AT_ENC},
  {RIT_RX2,             "RIT\nRX2",             "RIT2",         AT_ENC},
  {RIT_STEP,            "RIT\nStep",            "RITST",        AT_BTN},
  {RITXIT,              "RIT/XIT",              "RITXIT",       AT_ENC},
  {RITSELECT,           "RIT/XIT\nCycle",       "RITXTCYC",     AT_BTN},
  {RITXIT_CLEAR,        "RIT/XIT\nClear",       "RITXTCLR",     AT_BTN},
  {RSAT,                "RSAT",                 "RSAT",         AT_BTN},
  {MENU_RX,             "RX\nMenu",             "RX",           AT_BTN},
  {RX1,                 "RX1",                  "RX1",          AT_BTN},
  {RX2,                 "RX2",                  "RX2",          AT_BTN},
  {SAT,                 "SAT",                  "SAT",          AT_BTN},
  {SHUTDOWN,            "Shutdown\nOS",         "SDWN",         AT_BTN},
  {SNB,                 "SNB",                  "SNB",          AT_BTN},
  {SPLIT,               "Split",                "SPLIT",        AT_BTN},
  {SQUELCH,             "Squelch",              "SQUELCH",      AT_KNB | AT_ENC | AT_SLD},
  {SQUELCH_RX1,         "Squelch\nRX1",         "SQUELCH1",     AT_KNB | AT_ENC},
  {SQUELCH_RX2,         "Squelch\nRX2",         "SQUELCH2",     AT_KNB | AT_ENC},
  {SWAP_RX,             "Swap RX",              "SWAPRX",       AT_BTN},
  {TOOLBAR1,            "ToolBar1",             "TBAR1",        AT_BTN},
  {TOOLBAR2,            "ToolBar2",             "TBAR2",        AT_BTN},
  {TOOLBAR3,            "ToolBar3",             "TBAR3",        AT_BTN},
  {TOOLBAR4,            "ToolBar4",             "TBAR4",        AT_BTN},
  {TOOLBAR5,            "ToolBar5",             "TBAR5",        AT_BTN},
  {TOOLBAR6,            "ToolBar6",             "TBAR6",        AT_BTN},
  {TOOLBAR7,            "ToolBar7",             "TBAR7",        AT_BTN},
  {TUNE,                "Tune",                 "TUNE",         AT_BTN},
  {TUNE_DRIVE,          "Tune\nDrv",            "TUNDRV",       AT_KNB | AT_ENC},
  {TUNE_FULL,           "Tune\nFull",           "TUNF",         AT_BTN},
  {TUNE_MEMORY,         "Tune\nMem",            "TUNM",         AT_BTN},
  {DRIVE,               "TX Drive",             "TXDRV",        AT_KNB | AT_ENC | AT_SLD},
  {TWO_TONE,            "Two-Tone",             "2TONE",        AT_BTN},
  {MENU_TX,             "TX\nMenu",             "TX",           AT_BTN},
  {VFO,                 "VFO",                  "VFO",          AT_ENC},
  {MENU_FREQUENCY,      "VFO\nMenu",            "FREQ",         AT_BTN},
  {VFO_STEP_MINUS,      "VFO Step -",           "STEP-",        AT_BTN},
  {VFO_STEP_PLUS,       "VFO Step +",           "STEP+",        AT_BTN},
  {VFOA,                "VFO A",                "VFOA",         AT_ENC},
  {VFOB,                "VFO B",                "VFOB",         AT_ENC},
  {VOX,                 "VOX\nOn/Off",          "VOX",          AT_BTN},
  {VOXLEVEL,            "VOX\nLevel",           "VOXLEV",       AT_KNB | AT_ENC | AT_SLD},
  {WATERFALL_HIGH,      "Wfall\nHigh",          "WFALLH",       AT_ENC},
  {WATERFALL_LOW,       "Wfall\nLow",           "WFALLL",       AT_ENC},
  {XIT,                 "XIT",                  "XIT",          AT_ENC},
  {XIT_CLEAR,           "XIT\nClear",           "XITCL",        AT_BTN},
  {XIT_ENABLE,          "XIT\nOn/Off",          "XITT",         AT_BTN},
  {XIT_MINUS,           "XIT -",                "XIT-",         AT_BTN},
  {XIT_PLUS,            "XIT +",                "XIT+",         AT_BTN},
  {ZOOM,                "Zoom",                 "ZOOM",         AT_KNB | AT_ENC | AT_SLD},
  {ZOOM_MINUS,          "Zoom -",               "ZOOM-",        AT_BTN},
  {ZOOM_PLUS,           "Zoom +",               "ZOOM+",        AT_BTN},
  {ACTIONS,             "None",                 "NONE",         AT_NONE}
};

//
// Supporting repeated actions if a key is pressed for a long time:
//
// In this case, a repeat timer is  initiated. Since there casen only by
// one repeat timer active at one moment, we can use static storage to
// 'remember' the action.
// The benefit of this is that there is no need to defer the g_free o a
// recently allocated PROCESS_ACTION structure.
//
static guint repeat_timer = 0;
static gboolean repeat_timer_released;
static PROCESS_ACTION repeat_action;
static gboolean multi_select_active;
static gboolean multi_first = TRUE;
static unsigned int multi_action = 0;
#define VMAXMULTIACTION 28

//
// The strings in the following table are chosen
// as to occupy minimum space in the VFO bar
//
MULTI_TABLE multi_action_table[] = {
  {AF_GAIN,          "AFgain"},
  {AGC_GAIN,         "AGC"},
  {ATTENUATION,      "Att"},
  {COMPRESSION,      "Cmpr"},
  {CW_FREQUENCY,     "CWfrq"},
  {CW_SPEED,         "CWspd"},
  {DIV_GAIN,         "DivG"},
  {DIV_PHASE,        "DivP"},
  {FILTER_CUT_LOW,   "FCutL"},
  {FILTER_CUT_HIGH,  "FCutH"},
  {IF_SHIFT,         "IFshft"},
  {IF_WIDTH,         "IFwid"},
  {LINEIN_GAIN,      "LineIn"},
  {MIC_GAIN,         "Mic"},
  {PAN,              "Pan"},
  {PANADAPTER_HIGH,  "PanH"},
  {PANADAPTER_LOW,   "PanL"},
  {PANADAPTER_STEP,  "PanStp"},
  {RF_GAIN,          "RFgain"},
  {RIT,              "RIT"},
  {SQUELCH,          "Sqlch"},
  {TUNE_DRIVE,       "TunDrv"},
  {DRIVE,            "Drive"},
  {VOXLEVEL,         "VOX"},
  {WATERFALL_HIGH,   "WfallH"},
  {WATERFALL_LOW,    "WFallL"},
  {XIT,              "XIT"},
  {ZOOM,             "Zoom"}
};

static int repeat_cb(gpointer data) {
  //
  // This is periodically called to execute the same action
  // again and agin (e.g. while the RIT button is kept being
  // pressed. The action is stored in repeat_action.
  //
  if (repeat_timer_released) {
    repeat_timer = 0;
    return G_SOURCE_REMOVE;
  }

  // process the repeat_action
  PROCESS_ACTION *a = g_new(PROCESS_ACTION, 1);
  *a = repeat_action;
  process_action(a);
  return TRUE;
}

static inline double KnobOrWheel(const PROCESS_ACTION *a, double oldval, double minval, double maxval, double inc) {
  //
  // Knob ("Potentiometer"):  set value
  // Wheel("Rotary Encoder"): increment/decrement the value (by "inc" per tick)
  //
  // In both cases, the returned value is
  //  - in the range minval...maxval
  //  - rounded to a multiple of inc
  //
  switch (a->mode) {
  case RELATIVE:
    oldval += a->val * inc;
    break;

  case ABSOLUTE:
    // The magic floating point  constant is 1/127
    oldval = minval + a->val * (maxval - minval) * 0.00787401574803150;
    break;

  default:
    // do nothing
    break;
  }

  //
  // Round and check range
  //
  oldval = inc * round(oldval / inc);

  if (oldval > maxval) { oldval = maxval; }

  if (oldval < minval) { oldval = minval; }

  return oldval;
}

//
// This interface puts an "action" into the GTK idle queue,
// but "CW key" actions are processed immediately
//
void schedule_action(enum ACTION action, enum ACTION_MODE mode, int val) {
  PROCESS_ACTION *a;

  switch (action) {
  case CW_LEFT:
  case CW_RIGHT:
    cw_key_hit = 1;
    keyer_event(action == CW_LEFT, mode == PRESSED);
    break;

  case CW_KEYER_KEYDOWN: {
    static double last = 0.0;
    double now;
    int wait;
    struct timespec ts;
    //
    // hard "key-up/down" action WITHOUT break-in
    // intended for external keyers (MIDI or GPIO connected)
    // which take care of PTT themselves.
    //
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + 1.0E-9 * ts.tv_nsec;
    wait = (int) (48000.0 * (now - last) + 0.5);
    last = now;

    if (mode == PRESSED && (!cw_keyer_internal || MIDI_cw_is_active)) {
      gpio_set_cw(1);

      if (wait > 48000) {
        //
        // first key-down after a pause: queue without delay if local,
        // but if remote, queue a no-delay pause that is at least 100 msec
        // to get some "buffering" on the other side
        //
        if (radio_is_remote) {
          tx_queue_cw_event(0, 0);
          wait = 4800;
        } else {
          wait = 0;
        }
      }

      tx_queue_cw_event(1, wait);
      cw_key_hit = 1;
    } else {
      gpio_set_cw(0);
      tx_queue_cw_event(0, wait);
    }
  }
  break;

  default:
    //
    // schedule action through GTK idle queue
    //
    a = g_new(PROCESS_ACTION, 1);
    a->action = action;
    a->mode = mode;
    a->val = val;
    g_idle_add(process_action, a);
    break;
  }
}

int process_action(void *data) {
  PROCESS_ACTION *a = (PROCESS_ACTION *)data;
  double value;
  int i;
  enum ACTION action = a->action;

  //t_print("%s: a=%p action=%d mode=%d value=%d\n",__FUNCTION__,a,action,a->mode,a->val);
  switch (action) {
  case A_SWAP_B:
    if (a->mode == PRESSED) {
      vfo_a_swap_b();
    }

    break;

  case A_TO_B:
    if (a->mode == PRESSED) {
      vfo_a_to_b();
    }

    break;

  case AF_GAIN:
    value = KnobOrWheel(a, active_receiver->volume, -40.0, 0.0, 1.0);
    radio_set_af_gain(active_receiver->id, value);
    break;

  case AF_GAIN_RX1:
    value = KnobOrWheel(a, receiver[0]->volume, -40.0, 0.0, 1.0);
    radio_set_af_gain(0, value);
    break;

  case AF_GAIN_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->volume, -40.0, 0.0, 1.0);
      radio_set_af_gain(1, value);
    }

    break;

  case AGC:
    if (a->mode == PRESSED) {
      active_receiver->agc++;

      if (active_receiver->agc >= AGC_LAST) {
        active_receiver->agc = 0;
      }

      rx_set_agc(active_receiver);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case AGC_GAIN:
    value = KnobOrWheel(a, active_receiver->agc_gain, -20.0, 120.0, 1.0);
    radio_set_agc_gain(active_receiver->id, value);
    break;

  case AGC_GAIN_RX1:
    value = KnobOrWheel(a, receiver[0]->agc_gain, -20.0, 120.0, 1.0);
    radio_set_agc_gain(0, value);
    break;

  case AGC_GAIN_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->agc_gain, -20.0, 120.0, 1.0);
      radio_set_agc_gain(1, value);
    }

    break;

  case ANF:
    if (a->mode == PRESSED) {
      TOGGLE(active_receiver->anf);
      rx_set_noise(active_receiver);
    }

    break;

  case ATTENUATION:
    if (have_rx_att) {
      value = KnobOrWheel(a, adc[active_receiver->adc].attenuation,   0.0, 31.0, 1.0);
      radio_set_attenuation(active_receiver->id, value);
    }

    break;

  case B_TO_A:
    if (a->mode == PRESSED) {
      vfo_b_to_a();
    }

    break;

  case BAND_10:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band10);
    }

    break;

  case BAND_12:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band12);
    }

    break;

  case BAND_1240:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band1240);
    }

    break;

  case BAND_144:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band144);
    }

    break;

  case BAND_15:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band15);
    }

    break;

  case BAND_160:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band160);
    }

    break;

  case BAND_17:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band17);
    }

    break;

  case BAND_20:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band20);
    }

    break;

  case BAND_220:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band220);
    }

    break;

  case BAND_2300:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band2300);
    }

    break;

  case BAND_30:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band30);
    }

    break;

  case BAND_3400:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band3400);
    }

    break;

  case BAND_40:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band40);
    }

    break;

  case BAND_430:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band430);
    }

    break;

  case BAND_6:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band6);
    }

    break;

  case BAND_60:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band60);
    }

    break;

  case BAND_70:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band70);
    }

    break;

  case BAND_80:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band80);
    }

    break;

  case BAND_902:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band902);
    }

    break;

  case BAND_AIR:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, bandAIR);
    }

    break;

  case BAND_GEN:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, bandGen);
    }

    break;

  case BAND_136:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, band136);
    }

    break;

  case BAND_MINUS:
    if (a->mode == PRESSED) {
      band_minus(active_receiver->id);
    }

    break;

  case BAND_PLUS:
    if (a->mode == PRESSED) {
      band_plus(active_receiver->id);
    }

    break;

  case BAND_WWV:
    if (a->mode == PRESSED) {
      vfo_id_band_changed(active_receiver->id, bandWWV);
    }

    break;

  case BANDSTACK_MINUS:
    if (a->mode == PRESSED) {
      const BAND *band = band_get_band(vfo[active_receiver->id].band);
      const BANDSTACK *bandstack = band->bandstack;
      int b = vfo[active_receiver->id].bandstack - 1;

      if (b < 0) { b = bandstack->entries - 1; };

      vfo_bandstack_changed(b);
    }

    break;

  case BANDSTACK_PLUS:
    if (a->mode == PRESSED) {
      const BAND *band = band_get_band(vfo[active_receiver->id].band);
      const BANDSTACK *bandstack = band->bandstack;
      int b = vfo[active_receiver->id].bandstack + 1;

      if (b >= bandstack->entries) { b = 0; }

      vfo_bandstack_changed(b);
    }

    break;

  case CAPTURE:

    //
    // Audio capture and playback is handled on the server side only
    //
    if (radio_is_remote && a->mode == PRESSED) {
      send_capture(client_socket);
    } else if (can_transmit && a->mode == PRESSED) {
      switch (capture_state) {
      case CAP_INIT:
        //
        // Hitting "capture" during TX when nothing has ever been
        // recorded moves us to CAP_AVAIL with an empty buffer.
        //
        capture_data = g_new(double, capture_max);
        capture_record_pointer = 0;
        capture_replay_pointer = 0;
        capture_state = CAP_AVAIL;
        break;

      case CAP_AVAIL:

        //
        // In this state, a recording is already in memory, so we can
        // either play-back (TX) or start a new recording (RX)
        //
        if (radio_is_transmitting()) {
          radio_start_playback();
          capture_replay_pointer = 0;
          capture_state = CAP_REPLAY;
        } else {
          radio_start_capture();
          capture_record_pointer = 0;
          capture_state = CAP_RECORDING;
        }

        break;

      case CAP_RECORDING:
      case CAP_RECORD_DONE:
        //
        // The two states only differ in whether recording stops due
        // to user request (CAP_RECORDING) or because the audio
        // buffer was full (CAP_RECORD_DONE)
        //
        radio_end_capture();
        capture_state = CAP_AVAIL;
        break;

      case CAP_REPLAY:
      case CAP_REPLAY_DONE:
        //
        // The two states only differ in whether playback stops due
        // to user request (CAP_REPLAY) or because the entire recording
        // has been re-played.
        //
        radio_end_playback();
        capture_state = CAP_AVAIL;
        break;

      case CAP_GOTOSLEEP:
        //
        // Called after a timeout
        //
        capture_state = CAP_SLEEPING;
        break;

      case CAP_SLEEPING:
        //
        // Data is available but this is not displayed. Go back to
        // CAP_AVAIL
        //
        capture_state = CAP_AVAIL;
        break;
      }
    }

    break;

  case COMP_ENABLE:
    if (can_transmit && a->mode == PRESSED) {
      TOGGLE(transmitter->compressor);
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case COMPRESSION:
    if (can_transmit) {
      value = KnobOrWheel(a, transmitter->compressor_level, 0.0, 20.0, 1.0);
      transmitter->compressor = SET(value > 0.5);
      transmitter->compressor_level = value;
      tx_set_compressor(transmitter);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case CTUN:
    if (a->mode == PRESSED) {
      vfo_id_ctun_update(active_receiver->id, NOT(vfo[active_receiver->id].ctun));
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case CW_AUDIOPEAKFILTER:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      vfo_id_cwpeak_changed(id, NOT(vfo[id].cwAudioPeakFilter));
    }

    break;

  case CW_FREQUENCY:
    value = KnobOrWheel(a, (double)cw_keyer_sidetone_frequency, 300.0, 1000.0, 10.0);
    radio_set_sidetone_freq((int) value);
    break;

  case CW_SPEED:
    value = KnobOrWheel(a, (double)cw_keyer_speed, 1.0, 60.0, 1.0);
    radio_set_cw_speed((int) value);
    break;

  case DIV:
    if (a->mode == PRESSED && n_adc > 1) {
      radio_set_diversity(NOT(diversity_enabled));
    }

    break;

  case DIV_GAIN:
    radio_set_diversity_gain(div_gain + (double)a->val * 0.05);
    break;

  case DIV_GAIN_COARSE:
    radio_set_diversity_gain(div_gain + (double)a->val * 0.25);
    break;

  case DIV_GAIN_FINE:
    radio_set_diversity_gain(div_gain + (double)a->val * 0.01);
    break;

  case DIV_PHASE:
    radio_set_diversity_phase(div_phase + (double)a->val * 0.5);
    break;

  case DIV_PHASE_COARSE:
    radio_set_diversity_phase(div_phase + (double)a->val * 2.5);
    break;

  case DIV_PHASE_FINE:
    radio_set_diversity_phase(div_phase + (double)a->val * 0.1);
    break;

  case DRIVE:
    value = KnobOrWheel(a, radio_get_drive(), 0.0, drive_max, 1.0);
    radio_set_drive(value);
    break;

  case DUPLEX:
    if (can_transmit && !radio_is_transmitting() && a->mode == PRESSED) {
      radio_set_duplex(NOT(duplex));
    }

    break;

  case FILTER_MINUS:

    //
    // since the widest filters start at f=0, FILTER_MINUS actually
    // cycles upwards
    //
    if (a->mode == PRESSED) {
      int f = vfo[active_receiver->id].filter + 1;

      if (f >= FILTERS) { f = 0; }

      vfo_filter_changed(f);
    }

    break;

  case FILTER_PLUS:

    //
    // since the widest filters start at f=0, FILTER_PLUS actually
    // cycles downwards
    //
    if (a->mode == PRESSED) {
      int f = vfo[active_receiver->id].filter - 1;

      if (f < 0) { f = FILTERS - 1; }

      vfo_filter_changed(f);
    }

    break;

  case FILTER_CUT_HIGH:
    filter_high_changed(active_receiver->id, a->val);
    break;

  case FILTER_CUT_LOW:
    filter_low_changed(active_receiver->id, a->val);
    break;

  case FILTER_CUT_DEFAULT:
    if (a->mode == PRESSED) {
      filter_cut_default(active_receiver->id);
    }

    break;

  case FUNCTION:
    if (a->mode == PRESSED) {
      tb_function[0]++;

      if (tb_function[0] >= MAX_TB_FUNCTIONS) {
        tb_function[0] = 0;
      }

      update_toolbar_labels();
    }

    break;

  case FUNCTIONREV:
    if (a->mode == PRESSED) {
      tb_function[0]--;

      if (tb_function[0] < 0) {
        tb_function[0] = MAX_TB_FUNCTIONS - 1;
      }

      update_toolbar_labels();
    }

    break;

  case IF_SHIFT:
    filter_shift_changed(active_receiver->id, a->val);
    break;

  case IF_SHIFT_RX1:
    filter_shift_changed(0, a->val);
    break;

  case IF_SHIFT_RX2:
    filter_shift_changed(1, a->val);
    break;

  case IF_WIDTH:
    filter_width_changed(active_receiver->id, a->val);
    break;

  case IF_WIDTH_RX1:
    filter_width_changed(0, a->val);
    break;

  case IF_WIDTH_RX2:
    filter_width_changed(1, a->val);
    break;

  case LINEIN_GAIN:
    value = KnobOrWheel(a, linein_gain, -34.0, 12.5, 1.5);
    radio_set_linein_gain(value);
    break;

  case LOCK:
    if (a->mode == PRESSED) {
      if (radio_is_remote) {
        send_lock(client_socket, NOT(locked));
      } else {
        TOGGLE(locked);
        g_idle_add(ext_vfo_update, NULL);
      }
    }

    break;

  case MENU_AGC:
    if (a->mode == PRESSED) {
      start_agc();
    }

    break;

  case MENU_BAND:
    if (a->mode == PRESSED) {
      start_band();
    }

    break;

  case MENU_BANDSTACK:
    if (a->mode == PRESSED) {
      start_bandstack();
    }

    break;

  case MENU_DIVERSITY:
    if (a->mode == PRESSED && RECEIVERS == 2 && n_adc > 1) {
      start_diversity();
    }

    break;

  case MENU_FILTER:
    if (a->mode == PRESSED) {
      start_filter();
    }

    break;

  case MENU_FREQUENCY:
    if (a->mode == PRESSED) {
      start_vfo(active_receiver->id);
    }

    break;

  case MENU_MAIN:
    if (a->mode == PRESSED) {
      new_menu();
    }

    break;

  case MENU_MEMORY:
    if (a->mode == PRESSED) {
      start_store();
    }

    break;

  case MENU_MODE:
    if (a->mode == PRESSED) {
      start_mode();
    }

    break;

  case MENU_NOISE:
    if (a->mode == PRESSED) {
      start_noise();
    }

    break;

  case MENU_PS:
    if (a->mode == PRESSED) {
      start_ps();
    }

    break;

  case MENU_RX:
    if (a->mode == PRESSED) {
      start_rx();
    }

    break;

  case MENU_TX:
    if (a->mode == PRESSED) {
      start_tx();
    }

    break;

  case MIC_GAIN:
    if (can_transmit) {
      value = KnobOrWheel(a, transmitter->mic_gain, -12.0, 50.0, 1.0);
      radio_set_mic_gain(value);
    }

    break;

  case MODE_MINUS:
    if (a->mode == PRESSED) {
      int mode = vfo[active_receiver->id].mode;
      mode--;

      if (mode < 0) { mode = MODES - 1; }

      vfo_mode_changed(mode);
    }

    break;

  case MODE_PLUS:
    if (a->mode == PRESSED) {
      int mode = vfo[active_receiver->id].mode;
      mode++;

      if (mode >= MODES) { mode = 0; }

      vfo_mode_changed(mode);
    }

    break;

  case MOX:
    if (a->mode == PRESSED) {
      radio_toggle_mox();
    }

    break;

  // swap multifunction from implementing an action, and choosing which action is assigned
  case MULTI_BUTTON:
    if (a->mode == PRESSED) {
      multi_first = FALSE;
      multi_select_active = !multi_select_active;
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  // multifunction encoder. If multi_select_active, it edits the assigned action; else implements assigned action.
  case MULTI_ENC:
    multi_first = FALSE;

    if (multi_select_active) {
      multi_action = KnobOrWheel(a, multi_action, 0, VMAXMULTIACTION - 1, 1);
      g_idle_add(ext_vfo_update, NULL);
    } else {
      PROCESS_ACTION *multifunction_action;
      multifunction_action = g_new(PROCESS_ACTION, 1);
      multifunction_action->mode = a->mode;
      multifunction_action->val = a->val;
      multifunction_action->action = multi_action_table[multi_action].action;
      process_action((void*)multifunction_action);
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

  // choose an action for a multifunction encoder
  case MULTI_SELECT:
    multi_first = FALSE;
    multi_action = KnobOrWheel(a, multi_action, 0, VMAXMULTIACTION - 1, 1);
    g_idle_add(ext_vfo_update, NULL);
    break;

  case MUTE:
    if (a->mode == PRESSED) {
      active_receiver->mute_radio = !active_receiver->mute_radio;
    }

    break;

  case MUTE_RX1:
    if (a->mode == PRESSED) {
      receiver[0]->mute_radio = !receiver[0]->mute_radio;
    }

    break;

  case MUTE_RX2:
    if (a->mode == PRESSED && receivers > 1) {
      receiver[1]->mute_radio = !receiver[1]->mute_radio;
    }

    break;

  case NB:
    if (a->mode == PRESSED) {
      active_receiver->nb++;

      if (active_receiver->nb > 2) { active_receiver->nb = 0; }

      rx_set_noise(active_receiver);
    }

    break;

  case NR:
    if (a->mode == PRESSED) {
      active_receiver->nr++;
#ifdef EXTNR

      if (active_receiver->nr > 4) { active_receiver->nr = 0; }

#else

      if (active_receiver->nr > 2) { active_receiver->nr = 0; }

#endif
      rx_set_noise(active_receiver);
    }

    break;

  case NUMPAD_0:
    if (a->mode == PRESSED) {
      vfo_num_pad(0, active_receiver->id);
    }

    break;

  case NUMPAD_1:
    if (a->mode == PRESSED) {
      vfo_num_pad(1, active_receiver->id);
    }

    break;

  case NUMPAD_2:
    if (a->mode == PRESSED) {
      vfo_num_pad(2, active_receiver->id);
    }

    break;

  case NUMPAD_3:
    if (a->mode == PRESSED) {
      vfo_num_pad(3, active_receiver->id);
    }

    break;

  case NUMPAD_4:
    if (a->mode == PRESSED) {
      vfo_num_pad(4, active_receiver->id);
    }

    break;

  case NUMPAD_5:
    if (a->mode == PRESSED) {
      vfo_num_pad(5, active_receiver->id);
    }

    break;

  case NUMPAD_6:
    if (a->mode == PRESSED) {
      vfo_num_pad(6, active_receiver->id);
    }

    break;

  case NUMPAD_7:
    if (a->mode == PRESSED) {
      vfo_num_pad(7, active_receiver->id);
    }

    break;

  case NUMPAD_8:
    if (a->mode == PRESSED) {
      vfo_num_pad(8, active_receiver->id);
    }

    break;

  case NUMPAD_9:
    if (a->mode == PRESSED) {
      vfo_num_pad(9, active_receiver->id);
    }

    break;

  case NUMPAD_BS:
    if (a->mode == PRESSED) {
      vfo_num_pad(-6, active_receiver->id);
    }

    break;

  case NUMPAD_CL:
    if (a->mode == PRESSED) {
      vfo_num_pad(-1, active_receiver->id);
    }

    break;

  case NUMPAD_ENTER:
    if (a->mode == PRESSED) {
      vfo_num_pad(-2, active_receiver->id);
    }

    break;

  case NUMPAD_KHZ:
    if (a->mode == PRESSED) {
      vfo_num_pad(-3, active_receiver->id);
    }

    break;

  case NUMPAD_MHZ:
    if (a->mode == PRESSED) {
      vfo_num_pad(-4, active_receiver->id);
    }

    break;

  case NUMPAD_DEC:
    if (a->mode == PRESSED) {
      vfo_num_pad(-5, active_receiver->id);
    }

    break;

  case PAN:
    value = KnobOrWheel(a, active_receiver->pan, -100.0, 100.0, 1.0);
    radio_set_pan(active_receiver->id,  (int) value);
    break;

  case PAN_MINUS:
    if (a->mode == PRESSED) {
      radio_set_pan(active_receiver->id,  active_receiver->pan - 5);
    }

    break;

  case PAN_PLUS:
    if (a->mode == PRESSED) {
      radio_set_pan(active_receiver->id,  active_receiver->pan + 5);
    }

    break;

  case PANADAPTER_HIGH:
    value = KnobOrWheel(a, active_receiver->panadapter_high, -60.0, 20.0, 1.0);
    radio_set_panhigh(active_receiver->id, (int) value);
    break;

  case PANADAPTER_LOW:
    value = KnobOrWheel(a, active_receiver->panadapter_low, -160.0, -60.0, 1.0);
    radio_set_panlow(active_receiver->id, (int) value);
    break;

  case PANADAPTER_STEP:
    value = KnobOrWheel(a, active_receiver->panadapter_step, 5.0, 30.0, 5.0);
    radio_set_panstep(active_receiver->id, (int) value);
    break;

  case PREAMP:
    break;

  case PS:
    if (a->mode == PRESSED) {
      if (can_transmit) {
        if (transmitter->puresignal == 0) {
          tx_ps_onoff(transmitter, 1);
        } else {
          tx_ps_onoff(transmitter, 0);
        }
      }
    }

    break;

  case PTT:
    if (a->mode == PRESSED || a->mode == RELEASED) {
      radio_set_mox(a->mode == PRESSED);
    }

    break;

  case RCL0:
  case RCL1:
  case RCL2:
  case RCL3:
  case RCL4:
  case RCL5:
  case RCL6:
  case RCL7:
  case RCL8:
  case RCL9:
    if (a->mode == PRESSED) {
      recall_memory_slot(action - RCL0);
    }

    break;

  case RF_GAIN:
    if (have_rx_gain) {
      value = KnobOrWheel(a, adc[active_receiver->adc].gain, adc[active_receiver->adc].min_gain,
                          adc[active_receiver->adc].max_gain, 1.0);
      radio_set_rf_gain(active_receiver->id, value);
    }

    break;

  case RF_GAIN_RX1:
    if (have_rx_gain) {
      value = KnobOrWheel(a, adc[receiver[0]->adc].gain, adc[receiver[0]->adc].min_gain, adc[receiver[0]->adc].max_gain, 1.0);
      radio_set_rf_gain(0, value);
    }

    break;

  case RF_GAIN_RX2:
    if (have_rx_gain && receivers == 2) {
      value = KnobOrWheel(a, adc[receiver[1]->adc].gain, adc[receiver[1]->adc].min_gain, adc[receiver[1]->adc].max_gain, 1.0);
      radio_set_rf_gain(1, value);
    }

    break;

  case RIT:
    if (a->mode == RELATIVE) {
      int id = active_receiver->id;
      vfo_id_rit_incr(id, vfo[id].rit_step * a->val);
    }

    break;

  case RIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_id_rit_value(active_receiver->id, 0);
    }

    break;

  case RIT_ENABLE:
    if (a->mode == PRESSED) {
      vfo_id_rit_toggle(active_receiver->id);
    }

    break;

  case RIT_MINUS:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      vfo_id_rit_incr(id, -vfo[id].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;
    }

    break;

  case RIT_PLUS:
    if (a->mode == PRESSED) {
      int id = active_receiver->id;
      vfo_id_rit_incr(id, vfo[id].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;
    }

    break;

  case RIT_RX1:
    vfo_id_rit_incr(0, vfo[0].rit_step * a->val);
    break;

  case RIT_RX2:
    vfo_id_rit_incr(1, vfo[1].rit_step * a->val);
    break;

  case RIT_STEP:
    if (a->mode == PRESSED) {
      int incr = 10 * vfo[active_receiver->id].rit_step;

      if (incr > 100) { incr = 100; }

      vfo_set_rit_step(incr);
    }

    break;

  case RITXIT:

    //
    // a RITXIT encoder automatically switches between RIT or XIT. It does XIT
    // if (and only if) RIT is disabled and XIT is enabled, otherwise it does RIT
    //
    if (a->mode == RELATIVE) {
      int id = active_receiver->id;

      if ((vfo[id].rit_enabled == 0) && (vfo[vfo_get_tx_vfo()].xit_enabled == 1)) {
        vfo_xit_incr(vfo[id].rit_step * a->val);
      } else {
        vfo_id_rit_incr(id, vfo[id].rit_step * a->val);
      }
    }

    break;

  case RITSELECT:

    //
    // An action which cycles between RIT on, XIT on, and both off.
    // This is intended to be used together with the RITXIT encoder
    //
    if (a->mode == PRESSED) {
      if ((vfo[active_receiver->id].rit_enabled == 0) && (vfo[vfo_get_tx_vfo()].xit_enabled == 0)) {
        vfo_id_rit_onoff(active_receiver->id, 1);
        vfo_xit_onoff(0);
      } else if ((vfo[active_receiver->id].rit_enabled == 1) && (vfo[vfo_get_tx_vfo()].xit_enabled == 0)) {
        vfo_id_rit_onoff(active_receiver->id, 0);
        vfo_xit_onoff(1);
      } else {
        vfo_id_rit_onoff(active_receiver->id, 0);
        vfo_xit_onoff(0);
      }
    }

    break;

  case RITXIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_id_rit_value(active_receiver->id, 0);
      vfo_xit_value(0);
    }

    break;

  case RX1:
    if (a->mode == PRESSED && receivers == 2) {
      rx_set_active(receiver[0]);
    }

    break;

  case RX2:
    if (a->mode == PRESSED && receivers == 2) {
      rx_set_active(receiver[1]);
    }

    break;

  case RSAT:
    if (a->mode == PRESSED) {
      radio_set_satmode (sat_mode == RSAT_MODE ? SAT_NONE : RSAT_MODE);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case SAT:
    if (a->mode == PRESSED) {
      radio_set_satmode (sat_mode == SAT_MODE ? SAT_NONE : SAT_MODE);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case SHUTDOWN:
    if (a->mode == PRESSED) {
      radio_shutdown();
    }

    break;

  case SNB:
    if (a->mode == PRESSED) {
      TOGGLE(active_receiver->snb);
      rx_set_noise(active_receiver);
    }

    break;

  case SPLIT:
    if (a->mode == PRESSED) {
      radio_split_toggle();
    }

    break;

  case SQUELCH:
    value = KnobOrWheel(a, active_receiver->squelch, 0.0, 100.0, 1.0);
    radio_set_squelch(active_receiver->id, value);
    break;

  case SQUELCH_RX1:
    value = KnobOrWheel(a, receiver[0]->squelch, 0.0, 100.0, 1.0);
    radio_set_squelch(0, value);
    break;

  case SQUELCH_RX2:
    if (receivers == 2) {
      value = KnobOrWheel(a, receiver[1]->squelch, 0.0, 100.0, 1.0);
      radio_set_squelch(1, value);
    }

    break;

  case SWAP_RX:
    if (a->mode == PRESSED) {
      if (receivers == 2) {
        rx_set_active(receiver[active_receiver->id == 1 ? 0 : 1]);
      }
    }

    break;

  case TOOLBAR1:
  case TOOLBAR2:
  case TOOLBAR3:
  case TOOLBAR4:
  case TOOLBAR5:
  case TOOLBAR6:
  case TOOLBAR7: {
    //
    // The TOOLBARn actions simply schedule the action currently associated
    // with the n-th toolbar button
    // NOTE: this gives a circular dependency if any of the toolbar buttons
    // is TOOLBARn, so filter this out!
    //
    int tbaction = tb_actions[tb_function[0]][action - TOOLBAR1];

    if (tbaction < TOOLBAR1 || tbaction > TOOLBAR7) {
      schedule_action(tbaction, a->mode, a->val);
    }
  }
  break;

  case TUNE:
    if (a->mode == PRESSED) {
      full_tune = 0;
      memory_tune = 0;
      radio_toggle_tune();
    }

    break;

  case TUNE_DRIVE:
    if (can_transmit) {
      value = KnobOrWheel(a, (double) transmitter->tune_drive, 0.0, 100.0, 1.0);
      transmitter->tune_drive = (int) value;
      transmitter->tune_use_drive = 0;

      if (radio_is_remote) {
        send_txmenu(client_socket);
      }

      queue_popup_slider(TUNE_DRIVE, -1, 0.0, 100.0, 1.0, value, "TUNE DRIVE");
    }

    break;

  case TUNE_FULL:
    if (a->mode == PRESSED) {
      full_tune = 1;
      memory_tune = 0;
      radio_toggle_tune();
    }

    break;

  case TUNE_MEMORY:
    if (a->mode == PRESSED) {
      full_tune = 0;
      memory_tune = 1;
      radio_toggle_tune();
    }

    break;

  case TWO_TONE:
    if (a->mode == PRESSED) {
      if (can_transmit) {
        radio_set_twotone(transmitter, NOT(transmitter->twotone));
      }
    }

    break;

  case VFO:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_step(new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VFO_STEP_MINUS:
    if (a->mode == PRESSED) {
      i = vfo_id_get_stepindex(active_receiver->id);
      vfo_id_set_step_from_index(active_receiver->id, --i);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case VFO_STEP_PLUS:
    if (a->mode == PRESSED) {
      i = vfo_id_get_stepindex(active_receiver->id);
      vfo_id_set_step_from_index(active_receiver->id, ++i);
      g_idle_add(ext_vfo_update, NULL);
    }

    break;

  case VFOA:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_id_step(0, new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VFOB:
    if (a->mode == RELATIVE && !locked) {
      static int acc = 0;
      acc += (int) a->val;
      int new = acc / vfo_encoder_divisor;

      if (new != 0) {
        vfo_id_step(1, new);
        acc -= new*vfo_encoder_divisor;
      }
    }

    break;

  case VOX:
    if (a->mode == PRESSED) {
      radio_set_voxenable(!vox_enabled);
    }

    break;

  case VOXLEVEL:
    value = KnobOrWheel(a, vox_threshold, 0.0, 1.0, 0.01);
    radio_set_voxlevel(value);
    break;

  case WATERFALL_HIGH:
    value = KnobOrWheel(a, active_receiver->waterfall_high, -100.0, 0.0, 1.0);
    active_receiver->waterfall_high = (int)value;
    queue_popup_slider(WATERFALL_HIGH, active_receiver->id + 1, -100.0, 0.0, 1.0, value, "WFALL HIGH RX");
    break;

  case WATERFALL_LOW:
    value = KnobOrWheel(a, active_receiver->waterfall_low, -150.0, -50.0, 1.0);
    active_receiver->waterfall_low = (int)value;
    queue_popup_slider(WATERFALL_HIGH, active_receiver->id + 1, -150.0, -50.0, 1.0, value, "WFALL LOW RX");
    break;

  case XIT:
    vfo_xit_incr(vfo[vfo_get_tx_vfo()].rit_step * a->val);
    break;

  case XIT_CLEAR:
    if (a->mode == PRESSED) {
      vfo_xit_value(0);
    }

    break;

  case XIT_ENABLE:
    if (a->mode == PRESSED && can_transmit) {
      vfo_xit_toggle();
    }

    break;

  case XIT_MINUS:
    if (a->mode == PRESSED) {
      vfo_xit_incr(-10 * vfo[vfo_get_tx_vfo()].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;
    }

    break;

  case XIT_PLUS:
    if (a->mode == PRESSED) {
      vfo_xit_incr(10 * vfo[vfo_get_tx_vfo()].rit_step);

      if (repeat_timer == 0) {
        repeat_action = *a;
        repeat_timer = g_timeout_add(250, repeat_cb, NULL);
        repeat_timer_released = FALSE;
      }
    } else {
      repeat_timer_released = TRUE;
    }

    break;

  case ZOOM:
    value = KnobOrWheel(a, active_receiver->zoom, 1.0, MAX_ZOOM, 1.0);
    radio_set_zoom(active_receiver->id, (int)  value);
    break;

  case ZOOM_MINUS:
    if (a->mode == PRESSED) {
      radio_set_zoom(active_receiver->id, active_receiver->zoom - 1);
    }

    break;

  case ZOOM_PLUS:
    if (a->mode == PRESSED) {
      radio_set_zoom(active_receiver->id, active_receiver->zoom + 1);
    }

    break;

  case CW_KEYER_PTT:

    //
    // This is a PTT signal from an external keyer (either MIDI or GPIO connected).
    // In addition to activating PTT, we have to set MIDI_cw_is_active to temporarily
    // enable CW from piHPSDR even if CW is handled  in the radio.
    //
    // This is to support a configuration where a key is attached to (and handled in)
    // the radio, while a contest logger controls a keyer whose key up/down events
    // arrive via MIDI/GPIO.
    //
    // When KEYER_PTT is removed, clear MIDI_CW_is_active to (re-)allow CW being handled in the
    // radio. piHPSDR then goes RX unless "Radio PTT" is seen, which indicates that either
    // a footswitch has been pushed, or that the radio went TX due to operating a Morse key
    // attached to the radio.
    // In both cases, piHPSDR stays TX and the radio will induce the TX/RX transition by removing radio_ptt.
    //
    switch (a->mode) {
    case PRESSED:
      MIDI_cw_is_active = 1;         // disable "CW handled in radio"
      cw_key_hit = 1;                // this tells rigctl to abort CAT CW

      if (radio_is_remote) {
        send_mox(client_socket, 1);
      } else {
        schedule_transmit_specific();
        radio_set_mox(1);
      }

      break;

    case RELEASED:
      MIDI_cw_is_active = 0;         // enable "CW handled in radio", if it was selected

      if (radio_is_remote) {
        usleep(100000);              // since we delayed the start of the first CW, increase hang time
        send_mox(client_socket, 0);
      } else {
        schedule_transmit_specific();

        if (!radio_ptt) { radio_set_mox(0); }
      }

      break;

    default:
      // should not happen
      break;
    }

    break;

  case CW_KEYER_SPEED:
    //
    // This is a MIDI message from a CW keyer. The MIDI controller
    // value maps 1:1 to the speed, but we keep it within limits.
    //
    i = a->val;

    if (i >= 1 && i <= 60) { cw_keyer_speed = i; }

    keyer_update();
    g_idle_add(ext_vfo_update, NULL);
    break;

  case NO_ACTION:
    // do nothing
    break;

  default:
    if (a->action >= 0 && a->action < ACTIONS) {
      t_print("%s: UNKNOWN PRESSED SWITCH ACTION %d (%s)\n", __FUNCTION__, a->action, ActionTable[a->action].str);
    } else {
      t_print("%s: INVALID PRESSED SWITCH ACTION %d\n", __FUNCTION__, a->action);
    }

    break;
  }

  g_free(data);
  return 0;
}

//
// Function to convert an internal action number to a unique string
// This is used to specify actions in the props files.
//
void Action2String(int id, char *str, size_t len) {
  if (id < 0 || id >= ACTIONS) {
    snprintf(str, len, "NONE");
  } else {
    snprintf(str, len, "%s", ActionTable[id].button_str);
  }
}

//
// Function to convert a string to an action number
// This is used to specify actions in the props files.
//
int String2Action(const char *str) {
  int i;

  for (i = 0; i < ACTIONS; i++) {
    if (!strcmp(str, ActionTable[i].button_str)) { return i; }
  }

  return NO_ACTION;
}

//
// function to get status for multifunction encoder
// status = 0: no multifunction encoder in use (no status)
// status = 1: "active" (normal) state (status in yellow)
// status = 2: "select" state (status in red)
//
int  GetMultifunctionStatus() {
  if (multi_first) {
    return 0;
  }

  return multi_select_active ? 2 : 1;
}

//
// function to get string for multifunction encoder
//
void GetMultifunctionString(char* str, size_t len) {
  snprintf(str, len, "M=%s", multi_action_table[multi_action].descr);
}
