/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include <gdk/gdk.h>
#include <math.h>
#include <semaphore.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <wdsp.h>

#include "appearance.h"
#include "discovered.h"
#include "main.h"
#include "agc.h"
#include "mode.h"
#include "filter.h"
#include "bandstack.h"
#include "band.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "new_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "vfo.h"
#include "channel.h"
#include "toolbar.h"
#include "new_menu.h"
#include "rigctl.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "ext.h"
#include "message.h"
#include "filter.h"

static int my_width;
static int my_height;

static GtkWidget *vfo_panel;
static cairo_surface_t *vfo_surface = NULL;

int steps[] = {1, 10, 25, 50, 100, 250, 500, 1000, 5000, 6250, 9000, 10000, 12500, 100000, 250000, 500000, 1000000};
char *step_labels[] = {"1Hz", "10Hz", "25Hz", "50Hz", "100Hz", "250Hz", "500Hz", "1kHz", 
                       "5kHz", "6.25k", "9kHz", "10kHz", "12.5k", "100kHz", "250kHz", "500kHz", "1MHz"};

inline long long ROUND(long long freq, int nsteps, int step) {
  //
  // Move frequency f by n steps, adjust to multiple of step size
  // This should replace *all* divisions by the step size
  // If nsteps is zero, this is simply rounding.
  //
  long  long f;

  f = (freq + step/2)/step;
  f = f + nsteps;
  f = f * step;

  return f;
}

struct _vfo vfo[MAX_VFOS];
struct _mode_settings mode_settings[MODES];

static void vfoSaveBandstack() {
  BANDSTACK *bandstack = bandstack_get_bandstack(vfo[0].band);
  BANDSTACK_ENTRY *entry = &bandstack->entry[vfo[0].bandstack];
  entry->frequency = vfo[0].frequency;
  entry->mode = vfo[0].mode;
  entry->filter = vfo[0].filter;
  entry->ctun = vfo[0].ctun;
  entry->ctun_frequency = vfo[0].ctun_frequency;
  entry->deviation = vfo[0].deviation;

  if (can_transmit) {
    entry->ctcss_enabled = transmitter->ctcss_enabled;
    entry->ctcss = transmitter->ctcss;
  }
}

static void modesettingsSaveState() {
  char name[128];
  char value[128];

  for (int i = 0; i < MODES; i++) {
    SetPropI1("modeset.%d.filter", i,                mode_settings[i].filter);
    SetPropI1("modeset.%d.nr", i,                    mode_settings[i].nr);
    SetPropI1("modeset.%d.nb", i,                    mode_settings[i].nb);
    SetPropI1("modeset.%d.anf", i,                   mode_settings[i].anf);
    SetPropI1("modeset.%d.snb", i,                   mode_settings[i].snb);
    SetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
    SetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
    SetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
    SetPropI1("modeset.%d.step", i,                  mode_settings[i].step);
    SetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
    SetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);

    for (int j = 0; j < 4; j++) {
      SetPropI2("modeset.%d.txeq.%d", i, j,          mode_settings[i].txeq[j]);
      SetPropI2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rxeq[j]);
    }
  }
}

static void modesettingsRestoreState() {
  char name[128];
  char *value;

  for (int i = 0; i < MODES; i++) {
    //
    // set defaults: everything off, and the default
    //               filter and VFO step size depends on the mode
    //
    switch (i) {
    case modeLSB:
    case modeUSB:
    case modeDSB:
      mode_settings[i].filter = filterF5; //  2700 Hz
      mode_settings[i].step   = 100;
      break;

    case modeDIGL:
    case modeDIGU:
      mode_settings[i].filter = filterF6; //  1000 Hz
      mode_settings[i].step   = 50;
      break;

    case modeCWL:
    case modeCWU:
      mode_settings[i].filter = filterF4; //   500 Hz
      mode_settings[i].step   = 25;
      break;

    case modeAM:
    case modeSAM:
    case modeSPEC:
    case modeDRM:
    case modeFMN:  // nowhere used for FM
      mode_settings[i].filter = filterF3; //  8000 Hz
      mode_settings[i].step   = 100;
      break;
    }

    mode_settings[i].nr = 0;
    mode_settings[i].nb = 0;
    mode_settings[i].anf = 0;
    mode_settings[i].snb = 0;
    mode_settings[i].en_txeq = 0;
    mode_settings[i].txeq[0] = 0;
    mode_settings[i].txeq[1] = 0;
    mode_settings[i].txeq[2] = 0;
    mode_settings[i].txeq[3] = 0;
    mode_settings[i].en_rxeq = 0;
    mode_settings[i].rxeq[0] = 0;
    mode_settings[i].rxeq[1] = 0;
    mode_settings[i].rxeq[2] = 0;
    mode_settings[i].rxeq[3] = 0;
    mode_settings[i].compressor = 0;
    mode_settings[i].compressor_level = 0.0;
    GetPropI1("modeset.%d.filter", i,                mode_settings[i].filter);
    GetPropI1("modeset.%d.nr", i,                    mode_settings[i].nr);
    GetPropI1("modeset.%d.nb", i,                    mode_settings[i].nb);
    GetPropI1("modeset.%d.anf", i,                   mode_settings[i].anf);
    GetPropI1("modeset.%d.snb", i,                   mode_settings[i].snb);
    GetPropI1("modeset.%d.en_txeq", i,               mode_settings[i].en_txeq);
    GetPropI1("modeset.%d.en_rxeq", i,               mode_settings[i].en_rxeq);
    GetPropI1("modeset.%d.step", i,                  mode_settings[i].step);
    GetPropF1("modeset.%d.compressor_level", i,      mode_settings[i].compressor_level);
    GetPropI1("modeset.%d.compressor", i,            mode_settings[i].compressor);

    for (int j = 0; j < 4; j++) {
      GetPropI2("modeset.%d.txeq.%d", i, j,          mode_settings[i].txeq[j]);
      GetPropI2("modeset.%d.rxeq.%d", i, j,          mode_settings[i].rxeq[j]);
    }
  }
}

void vfoSaveState() {
  char name[128];
  char value[128];
  vfoSaveBandstack();

  for (int i = 0; i < MAX_VFOS; i++) {
    SetPropI1("vfo.%d.band", i,             vfo[i].band);
    SetPropI1("vfo.%d.frequency", i,        vfo[i].frequency);
    SetPropI1("vfo.%d.ctun", i,             vfo[i].ctun);
    SetPropI1("vfo.%d.ctun_frequency", i,   vfo[i].ctun_frequency);
    SetPropI1("vfo.%d.rit", i,              vfo[i].rit);
    SetPropI1("vfo.%d.rit_enabled", i,      vfo[i].rit_enabled);
    SetPropI1("vfo.%d.xit", i,              vfo[i].xit);
    SetPropI1("vfo.%d.xit_enabled", i,      vfo[i].xit_enabled);
    SetPropI1("vfo.%d.lo", i,               vfo[i].lo);
    SetPropI1("vfo.%d.offset", i,           vfo[i].offset);
    SetPropI1("vfo.%d.mode", i,             vfo[i].mode);
    SetPropI1("vfo.%d.filter", i,           vfo[i].filter);
    SetPropI1("vfo.%d.cw_apf", i,           vfo[i].cwAudioPeakFilter);
    SetPropI1("vfo.%d.deviation", i,        vfo[i].deviation);
    SetPropI1("vfo.%d.step", i,             vfo[i].step);
  }

  modesettingsSaveState();
}

void vfoRestoreState() {
  char name[128];
  char *value;

  for (int i = 0; i < MAX_VFOS; i++) {
    //
    // Set defaults, using a simple heuristics to get a
    // band that actually works on the current hardware.
    //
    if (radio->frequency_min <  430E6 && radio->frequency_max > 440E6) {
      vfo[i].band            = band430;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 434010000;
    } else if (radio->frequency_min < 144E6 && radio->frequency_max > 146E6 ) {
      vfo[i].band            = band144;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 145000000;
    } else {
      vfo[i].band            = band20;
      vfo[i].bandstack       = 0;
      vfo[i].frequency       = 14010000;
    }

    vfo[i].mode              = modeCWU;
    vfo[i].filter            = filterF4;
    vfo[i].cwAudioPeakFilter = 0;
    vfo[i].lo                = 0;
    vfo[i].offset            = 0;
    vfo[i].rit_enabled       = 0;
    vfo[i].rit               = 0;
    vfo[i].xit_enabled       = 0;
    vfo[i].xit               = 0;
    vfo[i].ctun              = 0;
    vfo[i].deviation         = 2500;
    vfo[i].step              = 100;
    GetPropI1("vfo.%d.band", i,             vfo[i].band);
    GetPropI1("vfo.%d.frequency", i,        vfo[i].frequency);
    GetPropI1("vfo.%d.ctun", i,             vfo[i].ctun);
    GetPropI1("vfo.%d.ctun_frequency", i,   vfo[i].ctun_frequency);
    GetPropI1("vfo.%d.rit", i,              vfo[i].rit);
    GetPropI1("vfo.%d.rit_enabled", i,      vfo[i].rit_enabled);
    GetPropI1("vfo.%d.xit", i,              vfo[i].xit);
    GetPropI1("vfo.%d.xit_enabled", i,      vfo[i].xit_enabled);
    GetPropI1("vfo.%d.lo", i,               vfo[i].lo);
    GetPropI1("vfo.%d.offset", i,           vfo[i].offset);
    GetPropI1("vfo.%d.mode", i,             vfo[i].mode);
    GetPropI1("vfo.%d.filter", i,           vfo[i].filter);
    GetPropI1("vfo.%d.cw_apf", i,           vfo[i].cwAudioPeakFilter);
    GetPropI1("vfo.%d.deviation", i,        vfo[i].deviation);
    GetPropI1("vfo.%d.step", i,             vfo[i].step);

    // Sanity check: if !ctun, offset must be zero
    if (!vfo[i].ctun) {
      vfo[i].offset = 0;
    }
  }

  modesettingsRestoreState();
}

void vfo_xvtr_changed() {
  //
  // It may happen that the XVTR band is messed up in the sense
  // that the resulting radio frequency exceeds the limits.
  // In this case, change the VFO frequencies to get into
  // the allowed range, which is from LO + radio_min_frequency
  // to LO + radio_max_frequency
  //
  if (vfo[0].band >= BANDS) {
    const BAND *band = band_get_band(vfo[0].band);
    vfo[0].lo = band->frequencyLO + band->errorLO;

    if ((vfo[0].frequency > vfo[0].lo + radio->frequency_max)  ||
        (vfo[0].frequency < vfo[0].lo + radio->frequency_min)) {
      vfo[0].frequency = vfo[0].lo + (radio->frequency_min + radio->frequency_max) / 2;
      receiver_set_frequency(receiver[0], vfo[0].frequency);
    }
  }

  if (vfo[1].band >= BANDS) {
    const BAND *band = band_get_band(vfo[1].band);
    vfo[1].lo = band->frequencyLO + band->errorLO;

    if ((vfo[1].frequency > vfo[1].lo + radio->frequency_max)  ||
        (vfo[1].frequency < vfo[1].lo + radio->frequency_min)) {
      vfo[1].frequency = vfo[1].lo + (radio->frequency_min + radio->frequency_max) / 2;

      if (receivers == 2) {
        receiver_set_frequency(receiver[1], vfo[1].frequency);
      }
    }
  }

  schedule_general();        // for disablePA
  schedule_high_priority();  // for Frequencies

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_apply_mode_settings(RECEIVER *rx) {
  int id, m;
  id = rx->id;
  m = vfo[id].mode;
  vfo[id].filter       = mode_settings[m].filter;
  rx->nr               = mode_settings[m].nr;
  rx->nb               = mode_settings[m].nb;
  rx->anf              = mode_settings[m].anf;
  rx->snb              = mode_settings[m].snb;
  enable_rx_equalizer  = mode_settings[m].en_rxeq;
  rx_equalizer[0]      = mode_settings[m].rxeq[0];
  rx_equalizer[1]      = mode_settings[m].rxeq[1];
  rx_equalizer[2]      = mode_settings[m].rxeq[2];
  rx_equalizer[3]      = mode_settings[m].rxeq[3];
  vfo[id].step         = mode_settings[m].step;

  //
  // Transmitter-specific settings are only changed if this VFO
  // controls the TX
  //
  if ((id == get_tx_vfo()) && can_transmit) {
    enable_tx_equalizer  = mode_settings[m].en_txeq;
    tx_equalizer[0]      = mode_settings[m].txeq[0];
    tx_equalizer[1]      = mode_settings[m].txeq[1];
    tx_equalizer[2]      = mode_settings[m].txeq[2];
    tx_equalizer[3]      = mode_settings[m].txeq[3];
    transmitter_set_compressor_level(transmitter, mode_settings[m].compressor_level);
    transmitter_set_compressor      (transmitter, mode_settings[m].compressor      );
  }

  //
  // make changes effective and put them on the VFO display
  //
  g_idle_add(ext_update_noise, NULL);
  g_idle_add(ext_update_eq, NULL);
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_band_changed(int id, int b) {
  BANDSTACK *bandstack;
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_band(client_socket, id, b);
    return;
  }

#endif

  //
  // If the band is not equal to the current band, look at the frequency of the
  // new bandstack entry.
  // Return quickly if the frequency is not compatible with the radio.
  // Note the LO frequency of the *new* band must be subtracted here
  //
  if (b != vfo[id].band) {
    const BAND *band = band_get_band(b);
    bandstack = bandstack_get_bandstack(b);
    long long f = bandstack->entry[bandstack->current_entry].frequency;
    f -= (band->frequencyLO + band->errorLO);

    if (f < radio->frequency_min || f > radio->frequency_max) {
      return;
    }
  }

  if (id == 0) {
    vfoSaveBandstack();
  }

  if (b == vfo[id].band) {
    // same band selected - step to the next band stack
    bandstack = bandstack_get_bandstack(b);
    vfo[id].bandstack++;

    if (vfo[id].bandstack >= bandstack->entries) {
      vfo[id].bandstack = 0;
    }
  } else {
    // new band - get band stack entry
    bandstack = bandstack_get_bandstack(b);
    vfo[id].bandstack = bandstack->current_entry;
  }

  const BAND *band = band_set_current(b);
  const BANDSTACK_ENTRY *entry = &bandstack->entry[vfo[id].bandstack];
  vfo[id].band = b;
  vfo[id].frequency = entry->frequency;
  vfo[id].ctun = entry->ctun;
  vfo[id].ctun_frequency = entry->ctun_frequency;
  vfo[id].mode = entry->mode;
  vfo[id].lo = band->frequencyLO + band->errorLO;
  vfo[id].filter = entry->filter;
  vfo[id].deviation = entry->deviation;

  if (can_transmit) {
    transmitter_set_ctcss(transmitter, entry->ctcss_enabled, entry->ctcss);
  }

  //
  // In the case of CTUN, the offset is re-calculated
  // during receiver_vfo_changed ==> receiver_frequency_changed
  //

  if (id == 0) {
    bandstack->current_entry = vfo[id].bandstack;
  }

  if (id < receivers) {
    //
    // This VFO controls a receiver
    //
    vfo_apply_mode_settings(receiver[id]);
    receiver_vfo_changed(receiver[id]);
  }

  tx_vfo_changed();
  set_alex_antennas();  // This includes scheduling hiprio and general packets
#ifdef SOAPYSDR

  //
  // This is strange, since it already done via receiver_vfo_changed()
  // correctly and the present code seems to be wrong if
  // (receivers == 1 && id == 1) or (receivers == 2 && id == 0)
  //
  if (protocol == SOAPYSDR_PROTOCOL) {
    soapy_protocol_set_rx_frequency(active_receiver, id);
  }

#endif
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_bandstack_changed(int b) {
  CLIENT_MISSING;
  int id = active_receiver->id;

  if (id == 0) {
    vfoSaveBandstack();
  }

  vfo[id].bandstack = b;
  BANDSTACK *bandstack = bandstack_get_bandstack(vfo[id].band);
  const BANDSTACK_ENTRY *entry = &bandstack->entry[vfo[id].bandstack];
  vfo[id].frequency = entry->frequency;
  vfo[id].ctun_frequency = entry->ctun_frequency;
  vfo[id].ctun = entry->ctun;
  vfo[id].mode = entry->mode;
  vfo[id].filter = entry->filter;
  vfo[id].deviation = entry->deviation;

  if (can_transmit) {
    transmitter_set_ctcss(transmitter, entry->ctcss_enabled, entry->ctcss);
  }

  if (id == 0) {
    bandstack->current_entry = vfo[id].bandstack;
  }

  if (id < receivers) {
    vfo_apply_mode_settings(receiver[id]);
    receiver_vfo_changed(receiver[id]);
  }

  tx_vfo_changed();
  set_alex_antennas();  // This includes scheduling hiprio and general packets
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_mode_changed(int m) {
  int id = active_receiver->id;
  vfo_id_mode_changed(id, m);
}

void vfo_id_mode_changed(int id, int m) {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_mode(client_socket, id, m);
    return;
  }

#endif
  vfo[id].mode = m;

  if (id < receivers) {
    vfo_apply_mode_settings(receiver[id]);
    receiver_mode_changed(receiver[id]);
    receiver_filter_changed(receiver[id]);
  }

  if (can_transmit) {
    tx_set_mode(transmitter, get_tx_mode());
  }

  //
  // changing modes may change BFO frequency
  // and SDR need to be informed about "CW or not CW"
  //
  schedule_high_priority();       // update frequencies
  schedule_transmit_specific();   // update "CW" flag

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_deviation_changed(int dev) {
  int id = active_receiver->id;
  vfo_id_deviation_changed(id, dev);
}

void vfo_id_deviation_changed(int id, int dev) {
  vfo[id].deviation = dev;

  if (id < receivers) {
    receiver_filter_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_filter_changed(int f) {
  int id = active_receiver->id;
  vfo_id_filter_changed(id, f);
}

void vfo_id_filter_changed(int id, int f) {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_filter(client_socket, id, f);
    return;
  }

#endif
  // store changed filter in the mode settings
  mode_settings[vfo[id].mode].filter = f;
  vfo[id].filter = f;

  //
  // If f is either Var1 or Var2, then the changed filter edges
  // should also apply to the other receiver, if it is running.
  // Otherwise the filter and rx settings do not coincide.
  //
  if (f == filterVar1 || f == filterVar2 ) {
    for (int i = 0; i < receivers; i++) {
      receiver_filter_changed(receiver[i]);
    }
  } else {
    if (id < receivers) {
      receiver_filter_changed(receiver[id]);
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_a_to_b() {
  CLIENT_MISSING;
  vfo[VFO_B] = vfo[VFO_A];

  if (receivers == 2) {
    receiver_vfo_changed(receiver[1]);
  }

  tx_vfo_changed();
  set_alex_antennas();  // This includes scheduling hiprio and general packets
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_b_to_a() {
  CLIENT_MISSING;
  vfo[VFO_A] = vfo[VFO_B];
  receiver_vfo_changed(receiver[0]);
  tx_vfo_changed();
  set_alex_antennas();  // This includes scheduling hiprio and general packets
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_a_swap_b() {
  CLIENT_MISSING;
  struct  _vfo temp = vfo[VFO_A];
  vfo[VFO_A]        = vfo[VFO_B];
  vfo[VFO_B]        = temp;
  receiver_vfo_changed(receiver[0]);

  if (receivers == 2) {
    receiver_vfo_changed(receiver[1]);
  }

  tx_vfo_changed();
  set_alex_antennas();  // This includes scheduling hiprio and general packets
  g_idle_add(ext_vfo_update, NULL);
}

//
// here we collect various functions to
// get/set the VFO step size
//

int vfo_get_step_from_index(int index) {
  //
  // This function is used for some
  // extended CAT commands
  //
  if (index < 0) { index = 0; }

  if (index >= STEPS) { index = STEPS - 1; }

  return steps[index];
}

int vfo_get_stepindex(int id) {
  //
  // return index of current step size in steps[] array
  //
  int i;
  int step = vfo[id].step;

  for (i = 0; i < STEPS; i++) {
    if (steps[i] == step) { break; }
  }

  //
  // If step size is not found (this should not happen)
  // report some "convenient" index at the small end
  // (here: index 4 corresponding to 100 Hz)
  //
  if (i >= STEPS) { i = 4; }

  return i;
}

void vfo_set_step_from_index(int id, int index) {
  CLIENT_MISSING;
  //
  // Set VFO step size to steps[index], with range checking
  //
  if (index < 0) { index = 0; }

  if (index >= STEPS) { index = STEPS - 1; }

  int step = steps[index];
  int m = vfo[id].mode;
  vfo[id].step = step;
  mode_settings[m].step = step;
}

void vfo_step(int steps) {
  int id = active_receiver->id;
  vfo_id_step(id, steps);
}

void vfo_id_step(int id, int steps) {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    update_vfo_step(id, steps);
    return;
  }

#endif

  if (!locked) {
    long long delta;

    if (vfo[id].ctun) {
      // CTUN offset is limited by half the sample rate
      // if "id" does not refer to a running RX, take the active receiver
      RECEIVER *myrx;

      if (id < receivers) {
        myrx = receiver[id];
      } else {
        myrx = active_receiver;
      }

      long long frequency = vfo[id].frequency;
      long long rx_low = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step) + myrx->filter_low;
      long long rx_high = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step) + myrx->filter_high;
      long long half = (long long)myrx->sample_rate / 2LL;
      long long min_freq = frequency - half;
      long long max_freq = frequency + half;

      if (rx_low <= min_freq) {
        return;
      } else if (rx_high >= max_freq) {
        return;
      }

      delta = vfo[id].ctun_frequency;
      vfo[id].ctun_frequency = ROUND(vfo[id].ctun_frequency, steps, vfo[id].step);
      delta = vfo[id].ctun_frequency - delta;
    } else {
      delta = vfo[id].frequency;
      vfo[id].frequency = ROUND(vfo[id].frequency, steps, vfo[id].step);
      delta = vfo[id].frequency - delta;
    }

    int sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
      } else {
        vfo[sid].frequency      += delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrments and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
      } else {
        vfo[sid].frequency      -= delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;
    }

    receiver_frequency_changed(receiver[id]);
    g_idle_add(ext_vfo_update, NULL);
  }
}

//
// vfo_move (and vfo_id_move) are exclusively used
// to update the radio while dragging with the
// pointer device in the panadapter area. Therefore,
// the behaviour is different whether we use CTUN or not.
//
// In "normal" (non-CTUN) mode, we "drag the spectrum". This
// means, when dragging to the right the spectrum moves towards
// higher frequencies  this means the RX frequence is *decreased*.
//
// In "CTUN" mode, the spectrum is nailed to the display and we
// move the CTUN frequency. So dragging to the right
// *increases* the RX frequency.
//
void vfo_id_move(int id, long long hz, int round) {
  long long delta;
  int sid;
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    //send_vfo_move(client_socket,id,hz,round);
    update_vfo_move(id, hz, round);
    return;
  }

#endif

  if (!locked) {
    if (vfo[id].ctun) {
      // don't let ctun go beyond end of passband
      RECEIVER *myrx;

      if (id < receivers) {
        myrx = receiver[id];
      } else {
        myrx = active_receiver;
      }

      long long frequency = vfo[id].frequency;
      long long rx_low = vfo[id].ctun_frequency + hz + myrx->filter_low;
      long long rx_high = vfo[id].ctun_frequency + hz + myrx->filter_high;
      long long half = (long long)myrx->sample_rate / 2LL;
      long long min_freq = frequency - half;
      long long max_freq = frequency + half;

      if (rx_low <= min_freq) {
        return;
      } else if (rx_high >= max_freq) {
        return;
      }

      delta = vfo[id].ctun_frequency;
      // *Add* the shift (hz) to the ctun frequency
      vfo[id].ctun_frequency = vfo[id].ctun_frequency + hz;

      if (round && (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU)) {
        vfo[id].ctun_frequency = ROUND(vfo[id].ctun_frequency, 0, vfo[id].step);
      }

      delta = vfo[id].ctun_frequency - delta;
    } else {
      delta = vfo[id].frequency;
      // *Subtract* the shift (hz) from the VFO frequency
      vfo[id].frequency = vfo[id].frequency - hz;

      if (round && (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU)) {
        vfo[id].frequency = ROUND(vfo[id].frequency, 0, vfo[id].step);
      }

      delta = vfo[id].frequency - delta;
    }

    sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
      } else {
        vfo[sid].frequency      += delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrments and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
      } else {
        vfo[sid].frequency      -= delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;
    }

    receiver_frequency_changed(receiver[id]);
    g_idle_add(ext_vfo_update, NULL);
  }
}

void vfo_move(long long hz, int round) {
  vfo_id_move(active_receiver->id, hz, round);
}

void vfo_move_to(long long hz) {
  int id = active_receiver->id;
  vfo_id_move_to(id, hz);
}

void vfo_id_move_to(int id, long long hz) {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_vfo_move_to(client_socket, id, hz);
    return;
  }

#endif
  // hz is the offset from the min displayed frequency
  RECEIVER *myrx;

  if (id < receivers) {
    myrx = receiver[id];
  } else {
    myrx = active_receiver;
  }

  long long offset = hz;
  long long half = (long long)(myrx->sample_rate / 2);
  long long f;

  if (vfo[id].mode != modeCWL && vfo[id].mode != modeCWU) {
    offset = ROUND(hz, 0, vfo[id].step);
  }

  f = (vfo[id].frequency - half) + offset + ((double)myrx->pan * myrx->hz_per_pixel);

  if (!locked) {
    long long delta;
    int sid;

    if (vfo[id].ctun) {
      delta = vfo[id].ctun_frequency;
      vfo[id].ctun_frequency = f;

      if (vfo[id].mode == modeCWL) {
        vfo[id].ctun_frequency += cw_keyer_sidetone_frequency;
      } else if (vfo[id].mode == modeCWU) {
        vfo[id].ctun_frequency -= cw_keyer_sidetone_frequency;
      }

      delta = vfo[id].ctun_frequency - delta;
    } else {
      delta = vfo[id].frequency;
      vfo[id].frequency = f;

      if (vfo[id].mode == modeCWL) {
        vfo[id].frequency += cw_keyer_sidetone_frequency;
      } else if (vfo[id].mode == modeCWU) {
        vfo[id].frequency -= cw_keyer_sidetone_frequency;
      }

      delta = vfo[id].frequency - delta;
    }

    sid = 1 - id;

    switch (sat_mode) {
    case SAT_NONE:
      break;

    case SAT_MODE:

      // A and B increment and decrement together
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency += delta;
      } else {
        vfo[sid].frequency      += delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;

    case RSAT_MODE:

      // A increments and B decrements or A decrements and B increments
      if (vfo[sid].ctun) {
        vfo[sid].ctun_frequency -= delta;
      } else {
        vfo[sid].frequency      -= delta;
      }

      if (sid < receivers) {
        receiver_frequency_changed(receiver[sid]);
      }

      break;
    }

    receiver_vfo_changed(receiver[id]);
    g_idle_add(ext_vfo_update, NULL);
  }
}

static gboolean
vfo_scroll_event_cb (GtkWidget      *widget,
                     GdkEventScroll *event,
                     gpointer        data) {
  if (event->direction == GDK_SCROLL_UP) {
    vfo_step(1);
  } else {
    vfo_step(-1);
  }

  return FALSE;
}

static gboolean vfo_configure_event_cb (GtkWidget         *widget,
                                        GdkEventConfigure *event,
                                        gpointer           data) {
  if (vfo_surface) {
    cairo_surface_destroy (vfo_surface);
  }

  vfo_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                CAIRO_CONTENT_COLOR,
                gtk_widget_get_allocated_width (widget),
                gtk_widget_get_allocated_height (widget));
  /* Initialize the surface to black */
  cairo_t *cr;
  cr = cairo_create (vfo_surface);
  cairo_set_source_rgba(cr, cl[VBG][0],cl[VBG][1],cl[VBG][2],cl[VBG][3]);
  cairo_paint (cr);
  cairo_destroy(cr);
  g_idle_add(ext_vfo_update, NULL);
  return TRUE;
}

static gboolean vfo_draw_cb (GtkWidget *widget,
                             cairo_t   *cr,
                             gpointer   data) {
  cairo_set_source_surface (cr, vfo_surface, 0.0, 0.0);
  cairo_paint (cr);
  return FALSE;
}

//
// This function re-draws the VFO bar.
// Lot of elements are programmed, whose size and position
// is determined by the current vfo_layout
// Elements whose x-coordinate is zero are not drawn
//
void vfo_update() {
  if (!vfo_surface) { return; }

  int id = active_receiver->id;
  int m = vfo[id].mode;
  int f = vfo[id].filter;
  int txvfo = get_tx_vfo();
  const VFO_BAR_LAYOUT *vfl = &vfo_layout_list[vfo_layout];
  //
  // Filter used in active receiver
  //
  FILTER* band_filters = filters[m];
  const FILTER* band_filter = &band_filters[f];
  char temp_text[32];
  cairo_t *cr;
  cr = cairo_create (vfo_surface);
  cairo_set_source_rgba(cr, cl[VBG][0],cl[VBG][1],cl[VBG][2],cl[VBG][3]);
  cairo_paint (cr);
  cairo_select_font_face(cr, FNT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  // -----------------------------------------------------------
  //
  // Only if using a variable filter:
  // Draw a picture showing the actual and default fileter edges
  //
  // -----------------------------------------------------------
  if ((f == filterVar1 || f == filterVar2) && m != modeFMN && vfl->filter_x != 0) {
    double range;
    double s, x1, x2;
    int def_low, def_high;
    int low = band_filter->low;
    int high = band_filter->high;

    if (vfo[id].filter == filterVar1) {
      def_low = var1_default_low[m];
      def_high = var1_default_high[m];
    } else {
      def_low = var2_default_low[m];
      def_high = var2_default_high[m];
    }

    // switch high/low for lower-sideband-modes such
    // that the graphic display refers to audio frequencies.
    if (m == modeLSB || m == modeDIGL || m == modeCWL) {
      int swap;
      swap     = def_low;
      def_low  = def_high;
      def_high = swap;
      swap     = low;
      low      = high;
      high     = swap;
    }

    // default range is 50 pix wide in a 100 pix window
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgba(cr, cl[COK][0],cl[COK][1],cl[COK][2],cl[COK][3]);
    cairo_move_to(cr, vfl->filter_x + 20, vfl->filter_y);
    cairo_line_to(cr, vfl->filter_x + 25, vfl->filter_y - 5);
    cairo_line_to(cr, vfl->filter_x + 75, vfl->filter_y - 5);
    cairo_line_to(cr, vfl->filter_x + 80, vfl->filter_y);
    cairo_stroke(cr);
    range = (double) (def_high - def_low);
    s = 50.0 / range;
    // convert actual filter size to the "default" scale
    x1 = vfl->filter_x + 25 + s * (double)(low - def_low);
    x2 = vfl->filter_x + 25 + s * (double)(high - def_low);
    cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    cairo_move_to(cr, x1 - 5, vfl->filter_y - 15);
    cairo_line_to(cr, x1, vfl->filter_y - 10);
    cairo_line_to(cr, x2, vfl->filter_y - 10);
    cairo_line_to(cr, x2 + 5, vfl->filter_y - 15);
    cairo_stroke(cr);
  }

  // -----------------------------------------------------------
  //
  // Draw a string specifying the mode, the filter width
  // For CW; add CW speed and side tone frequency
  //
  // -----------------------------------------------------------
  if (vfl->mode_x != 0) {
    switch (vfo[id].mode) {
    case modeFMN: {
      int dev;
      const char *wid;
      //
      // filter edges are +/- 5500 if deviation==2500,
      //              and +/- 8000 if deviation==5000
      dev = vfo[id].deviation;

      switch (dev) {
      case 2500:
        wid = "11k";
        break;

      case 5000:
        wid = "16k";
        break;

      default:
        wid = "???";
        break;
      }

      if (can_transmit ? transmitter->ctcss_enabled : 0) {
        snprintf(temp_text, 32, "%s %s C=%0.1f", mode_string[vfo[id].mode], wid,
                 ctcss_frequencies[transmitter->ctcss]);
      } else {
        snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], wid);
      }
    }
    break;

    case modeCWL:
    case modeCWU:
      if (vfo[id].cwAudioPeakFilter) {
        snprintf(temp_text, 32, "%s %sP %dwpm %dHz", mode_string[vfo[id].mode],
                 band_filter->title,
                 cw_keyer_speed,
                 cw_keyer_sidetone_frequency);
      } else {
        snprintf(temp_text, 32, "%s %s %d wpm %d Hz", mode_string[vfo[id].mode],
                 band_filter->title,
                 cw_keyer_speed,
                 cw_keyer_sidetone_frequency);
      }

      break;

    case modeLSB:
    case modeUSB:
    case modeDSB:
    case modeAM:
      snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], band_filter->title);
      break;

    default:
      snprintf(temp_text, 32, "%s %s", mode_string[vfo[id].mode], band_filter->title);
      break;
    }

    cairo_set_font_size(cr, vfl->size1);
    cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    cairo_move_to(cr, vfl->mode_x, vfl->mode_y);
    cairo_show_text(cr, temp_text);
  }

  // In what follows, we want to display the VFO frequency
  // on which we currently transmit a signal with red colour.
  // If it is out-of-band, we display "Out of band" in red.
  // Frequencies we are not transmitting on are displayed in green
  // (dimmed if the freq. does not belong to the active receiver).
  // Frequencies of VFO A and B
  long long af = vfo[0].ctun ? vfo[0].ctun_frequency : vfo[0].frequency;
  long long bf = vfo[1].ctun ? vfo[1].ctun_frequency : vfo[1].frequency;
#if 0

  //
  // DL1YCF:
  // There is no consensus whether the "VFO display frequency" should move if
  // RIT/XIT values are changed. My Kenwood TS590 does so, but some popular
  // other SDR software does not (which in my personal view is a bug, not a feature).
  //
  // The strongest argument to prefer the "TS590" behaviour is that during TX,
  // the frequency actually used for transmit should be displayed.
  // Then, to preserve symmetry, during RX the effective RX frequency
  // is also displayed.
  //
  // Adjust VFO_A frequency for RIT/XIT
  //
  if (isTransmitting() && txvfo == 0) {
    if (vfo[0].xit_enabled) { af += vfo[0].xit; }
  } else {
    if (vfo[0].rit_enabled) { af += vfo[0].rit; }
  }

  //
  // Adjust VFO_B frequency for RIT/XIT
  //
  if (isTransmitting() && txvfo == 1) {
    if (vfo[1].xit_enabled) { af += vfo[1].xit; }
  } else {
    if (vfo[1].rit_enabled) { bf += vfo[1].rit; }
  }

#endif
  int oob = 0;
  int f_m; // MHz part
  int f_k; // kHz part
  int f_h; // Hz  part

  if (can_transmit) { oob = transmitter->out_of_band; }

  // -----------------------------------------------------------
  //
  // Draw VFO A Dial.
  //
  // -----------------------------------------------------------
  if (vfl->vfo_a_x != 0) {
    cairo_move_to(cr, abs(vfl->vfo_a_x), vfl->vfo_a_y);

    if (txvfo == 0 && (isTransmitting() || oob)) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else if (vfo[0].entered_frequency[0]) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else if (id != 0) {
      cairo_set_source_rgba(cr, cl[OKW][0],cl[OKW][1],cl[OKW][2],cl[OKW][3]);
    } else {
      cairo_set_source_rgba(cr, cl[COK][0],cl[COK][1],cl[COK][2],cl[COK][3]);
    }

    f_m = af / 1000000LL;
    f_k = (af - 1000000LL * f_m) / 1000;
    f_h = (af - 1000000LL * f_m - 1000 * f_k);
    cairo_set_font_size(cr, vfl->size2);
    cairo_show_text(cr, "A:");
    cairo_set_font_size(cr, vfl->size3);

    if (txvfo == 0 && oob) {
      cairo_show_text(cr, "Out of band");
    } else if (vfo[0].entered_frequency[0]) {
      snprintf(temp_text, sizeof(temp_text), "%s", vfo[0].entered_frequency);
      cairo_show_text(cr, temp_text);
    } else {
      //
      // poor man's right alignment:
      // If the frequency is small, print some zeroes
      // with the background colour
      //
      cairo_save(cr);
      cairo_set_source_rgba(cr, cl[VBG][0],cl[VBG][1],cl[VBG][2],cl[VBG][3]);

      if (f_m < 10) {
        cairo_show_text(cr, "0000");
      } else if (f_m < 100) {
        cairo_show_text(cr, "000");
      } else if (f_m < 1000) {
        cairo_show_text(cr, "00");
      } else if (f_m < 10000) {
        cairo_show_text(cr, "0");
      }

      cairo_restore(cr);
      snprintf(temp_text, 32, "%0d.%03d", f_m, f_k);
      cairo_show_text(cr, temp_text);
      cairo_set_font_size(cr, vfl->size2);
      snprintf(temp_text, 32, "%03d", f_h);
      cairo_show_text(cr, temp_text);
    }
  }

  // -----------------------------------------------------------
  //
  // Draw VFO B Dial.
  //
  // -----------------------------------------------------------

  if (vfl->vfo_b_x != 0) {
    cairo_move_to(cr, abs(vfl->vfo_b_x), abs(vfl->vfo_b_y));

    if (txvfo == 1 && (isTransmitting() || oob)) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else if (vfo[1].entered_frequency[0]) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else if (id != 1) {
      cairo_set_source_rgba(cr, cl[OKW][0],cl[OKW][1],cl[OKW][2],cl[OKW][3]);
    } else {
      cairo_set_source_rgba(cr, cl[COK][0],cl[COK][1],cl[COK][2],cl[COK][3]);
    }

    f_m = bf / 1000000LL;
    f_k = (bf - 1000000LL * f_m) / 1000;
    f_h = (bf - 1000000LL * f_m - 1000 * f_k);
    cairo_set_font_size(cr, vfl->size2);
    cairo_show_text(cr, "B:");
    cairo_set_font_size(cr, vfl->size3);

    if (txvfo == 0 && oob) {
      cairo_show_text(cr, "Out of band");
    } else if (vfo[1].entered_frequency[0]) {
      snprintf(temp_text, sizeof(temp_text), "%s", vfo[1].entered_frequency);
      cairo_show_text(cr, temp_text);
    } else {
      //
      // poor man's right alignment:
      // If the frequency is small, print some zeroes
      // with the background colour
      //
      cairo_save(cr);
      cairo_set_source_rgba(cr, cl[VBG][0],cl[VBG][1],cl[VBG][2],cl[VBG][3]);

      if (f_m < 10) {
        cairo_show_text(cr, "0000");
      } else if (f_m < 100) {
        cairo_show_text(cr, "000");
      } else if (f_m < 1000) {
        cairo_show_text(cr, "00");
      } else if (f_m < 10000) {
        cairo_show_text(cr, "0");
      }

      cairo_restore(cr);
      snprintf(temp_text, 32, "%0d.%03d", f_m, f_k);
      cairo_show_text(cr, temp_text);
      cairo_set_font_size(cr, vfl->size2);
      snprintf(temp_text, 32, "%03d", f_h);
      cairo_show_text(cr, temp_text);
    }
  }

  //
  // Everything that follows uses font size 1
  //
  cairo_set_font_size(cr, vfl->size1);

  // -----------------------------------------------------------
  //
  // Draw string indicating Zoom status
  //
  // -----------------------------------------------------------
  if (vfl->zoom_x != 0) {
    cairo_move_to(cr, vfl->zoom_x, vfl->zoom_y);

    if (active_receiver->zoom > 1) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    snprintf(temp_text, 32, "Zoom %d", active_receiver->zoom);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating PS status
  //
  // -----------------------------------------------------------
  if ((protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) && can_transmit && vfl->ps_x != 0) {
    cairo_move_to(cr, vfl->ps_x, vfl->ps_y);

    if (transmitter->puresignal) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "PS");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating RIT offset
  //
  // -----------------------------------------------------------
  if (vfl->rit_x != 0) {
    if (vfo[id].rit_enabled == 0) {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    } else {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    }

    snprintf(temp_text, 32, "RIT %lldHz", vfo[id].rit);
    cairo_move_to(cr, vfl->rit_x, vfl->rit_y);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating XIT offset
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->xit_x != 0) {
    if (vfo[txvfo].xit_enabled == 0) {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    } else {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    }

    snprintf(temp_text, 32, "XIT %lldHz", vfo[txvfo].xit);
    cairo_move_to(cr, vfl->xit_x, vfl->xit_y);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating NB status
  //
  // -----------------------------------------------------------
  if (vfl->nb_x != 0) {
    cairo_move_to(cr, vfl->nb_x, vfl->nb_y);

    switch (active_receiver->nb) {
    case 1:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NB");
      break;

    case 2:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NB2");
      break;

    default:
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
      cairo_show_text(cr, "NB");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating NR status
  //
  // -----------------------------------------------------------
  if (vfl->nr_x != 0) {
    cairo_move_to(cr, vfl->nr_x, vfl->nr_y);

    switch (active_receiver->nr) {
    case 1:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NR");
      break;

    case 2:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NR2");
      break;
#ifdef EXTNR

    case 3:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NR3");
      break;

    case 4:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "NR4");
      break;
#endif

    default:
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
      cairo_show_text(cr, "NR");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating ANF status
  //
  // -----------------------------------------------------------
  if (vfl->anf_x != 0) {
    cairo_move_to(cr, vfl->anf_x, vfl->anf_y);

    if (active_receiver->anf) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "ANF");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SNB status
  //
  // -----------------------------------------------------------
  if (vfl->snb_x != 0) {
    cairo_move_to(cr, vfl->snb_x, vfl->snb_y);

    if (active_receiver->snb) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "SNB");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating AGC status
  //
  // -----------------------------------------------------------
  if (vfl->agc_x != 0) {
    cairo_move_to(cr, vfl->agc_x, vfl->agc_y);

    switch (active_receiver->agc) {
    case AGC_OFF:
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
      cairo_show_text(cr, "AGC off");
      break;

    case AGC_LONG:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "AGC long");
      break;

    case AGC_SLOW:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "AGC slow");
      break;

    case AGC_MEDIUM:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "AGC med");
      break;

    case AGC_FAST:
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "AGC fast");
      break;
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating compressor status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->cmpr_x != 0) {
    cairo_move_to(cr, vfl->cmpr_x, vfl->cmpr_y);

    if (transmitter->compressor) {
      snprintf(temp_text, 32, "CMPR %d", (int) transmitter->compressor_level);
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, temp_text);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
      cairo_show_text(cr, "CMPR");
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating equalizer status
  //
  // -----------------------------------------------------------
  if (vfl->eq_x != 0) {
    cairo_move_to(cr, vfl->eq_x, vfl->eq_y);

    if (isTransmitting() && enable_tx_equalizer) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "TxEQ");
    } else if (!isTransmitting() && enable_rx_equalizer) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
      cairo_show_text(cr, "RxEQ");
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
      cairo_show_text(cr, "EQ");
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating DIVERSITY status
  //
  // -----------------------------------------------------------
  if (vfl->div_x != 0) {
    cairo_move_to(cr, vfl->div_x, vfl->div_y);

    if (diversity_enabled) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "DIV");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating VFO step size
  //
  // -----------------------------------------------------------
  if (vfl->step_x != 0) {
    int s;

    for (s = 0; s < STEPS; s++) {
      if (steps[s] == vfo[id].step) { break; }
    }

    if (s >= STEPS) { s = 0; }

    snprintf(temp_text, 32, "Step %s", step_labels[s]);
    cairo_move_to(cr, vfl->step_x, vfl->step_y);
    cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    cairo_show_text(cr, temp_text);
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating CTUN status
  //
  // -----------------------------------------------------------
  if (vfl->ctun_x != 0) {
    cairo_move_to(cr, vfl->ctun_x, vfl->ctun_y);

    if (vfo[id].ctun) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "CTUN");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating CAT status
  //
  // -----------------------------------------------------------
  if (vfl->cat_x != 0) {
    cairo_move_to(cr, vfl->cat_x, vfl->cat_y);

    if (cat_control > 0) {
      cairo_set_source_rgba(cr, cl[ATT][0],cl[ATT][1],cl[ATT][2],cl[ATT][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "CAT");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating VOX status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->vox_x != 0) {
    cairo_move_to(cr, vfl->vox_x, vfl->vox_y);

    if (vox_enabled) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "VOX");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating LOCK status
  //
  // -----------------------------------------------------------
  if (vfl->lock_x != 0) {
    cairo_move_to(cr, vfl->lock_x, vfl->lock_y);

    if (locked) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "Locked");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SPLIT status
  //
  // -----------------------------------------------------------
  if (vfl->split_x != 0) {
    cairo_move_to(cr, vfl->split_x, vfl->split_y);

    if (split) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    cairo_show_text(cr, "Split");
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SAT status
  //
  // -----------------------------------------------------------
  if (vfl->sat_x != 0) {
    cairo_move_to(cr, vfl->sat_x, vfl->sat_y);

    if (sat_mode != SAT_NONE) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    if (sat_mode == SAT_NONE || sat_mode == SAT_MODE) {
      cairo_show_text(cr, "SAT");
    } else {
      cairo_show_text(cr, "RSAT");
    }
  }

  // -----------------------------------------------------------
  //
  // Draw string indicating SAT status
  //
  // -----------------------------------------------------------
  if (can_transmit && vfl->dup_x != 0) {
    if (duplex) {
      cairo_set_source_rgba(cr, cl[ALM][0],cl[ALM][1],cl[ALM][2],cl[ALM][3]);
    } else {
      cairo_set_source_rgba(cr, cl[SHD][0],cl[SHD][1],cl[SHD][2],cl[SHD][3]);
    }

    snprintf(temp_text, 32, "DUP");
    cairo_move_to(cr, vfl->dup_x, vfl->dup_y);
    cairo_show_text(cr, temp_text);
  }

  cairo_destroy (cr);
  gtk_widget_queue_draw (vfo_panel);
}

static gboolean
vfo_press_event_cb (GtkWidget *widget,
                    GdkEventButton *event,
                    gpointer        data) {
  int v;

  switch (event->button) {
  case GDK_BUTTON_PRIMARY:
    v = VFO_A;

    if (event->x >= abs(vfo_layout_list[vfo_layout].vfo_b_x)) { v = VFO_B; }

    g_idle_add(ext_start_vfo, GINT_TO_POINTER(v));
    break;

  case GDK_BUTTON_SECONDARY:
    // do not discriminate between A and B
    g_idle_add(ext_start_band, NULL);
    break;
  }

  return TRUE;
}

GtkWidget* vfo_init(int width, int height) {
  my_width = width;
  my_height = height;
  vfo_panel = gtk_drawing_area_new ();
  gtk_widget_set_size_request (vfo_panel, width, height);
  g_signal_connect (vfo_panel, "configure-event",
                    G_CALLBACK (vfo_configure_event_cb), NULL);
  g_signal_connect (vfo_panel, "draw",
                    G_CALLBACK (vfo_draw_cb), NULL);
  /* Event signals */
  g_signal_connect (vfo_panel, "button-press-event",
                    G_CALLBACK (vfo_press_event_cb), NULL);
  g_signal_connect(vfo_panel, "scroll_event",
                   G_CALLBACK(vfo_scroll_event_cb), NULL);
  gtk_widget_set_events (vfo_panel, gtk_widget_get_events (vfo_panel)
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_SCROLL_MASK);
  return vfo_panel;
}

//
// Some utility functions to get characteristics of the current
// transmitter. These functions can be used even if there is no
// transmitter
//

int get_tx_vfo() {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  return txvfo;
}

int get_tx_mode() {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  return vfo[txvfo].mode;
}

long long get_tx_freq() {
  int txvfo = active_receiver->id;

  if (split) { txvfo = 1 - txvfo; }

  if (vfo[txvfo].ctun) {
    return  vfo[txvfo].ctun_frequency;
  } else {
    return vfo[txvfo].frequency;
  }
}

void vfo_xit_value(long long value ) {
  CLIENT_MISSING;
  int id = get_tx_vfo();
  vfo[id].xit = value;
  vfo[id].xit_enabled = value ? 1 : 0;

  schedule_high_priority();

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_toggle() {
  CLIENT_MISSING;
  int id = get_tx_vfo();
  TOGGLE(vfo[id].xit_enabled);

  schedule_high_priority();

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_toggle(int id) {
  CLIENT_MISSING;
  TOGGLE(vfo[id].rit_enabled);

  if (id < receivers) {
    receiver_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_value(int id, long long value) {
  CLIENT_MISSING;
  vfo[id].rit = value;
  vfo[id].rit_enabled = value ? 1 : 0;

  if (id < receivers) {
    receiver_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_onoff(int id, int enable) {
  CLIENT_MISSING;
  vfo[id].rit_enabled = SET(enable);

  if (id < receivers) {
    receiver_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_onoff(int enable) {
  CLIENT_MISSING;
  int id = get_tx_vfo();
  vfo[id].xit_enabled = SET(enable);

  schedule_high_priority();

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_xit_incr(int incr) {
  CLIENT_MISSING;
  int id = get_tx_vfo();
  long long value = vfo[id].xit + incr;

  if (value < -9999) {
    value = -9999;
  } else if (value > 9999) {
    value = 9999;
  }

  vfo[id].xit = value;
  vfo[id].xit_enabled = (value != 0);

  schedule_high_priority();

  g_idle_add(ext_vfo_update, NULL);
}

void vfo_rit_incr(int id, int incr) {
  CLIENT_MISSING;
  long long value = vfo[id].rit + incr;

  if (value < -9999) {
    value = -9999;
  } else if (value > 9999) {
    value = 9999;
  }

  vfo[id].rit = value;
  vfo[id].rit_enabled = (value != 0);

  if (id < receivers) {
    receiver_frequency_changed(receiver[id]);
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// Interface to set the frequency, including
// "long jumps", for which we may have to
// change the band. This is solely used for
//
// - FREQ MENU
// - MIDI or GPIO NumPad
// - CAT "set frequency" command
//
void vfo_set_frequency(int v, long long f) {
  CLIENT_MISSING;
  int b = get_band_from_frequency(f);

  if (b != vfo[v].band) {
    vfo_band_changed(v, b);
  }

  if (v == VFO_A) { receiver_set_frequency(receiver[0], f); }

  if (v == VFO_B) {
    //
    // If there is only one receiver, there is no RX running that
    // is controlled by VFO_B, so just update the frequency of the
    // VFO without telling WDSP about it.
    // If VFO_B controls a (running) receiver, do the "full job".
    //
    if (receivers == 2) {
      receiver_set_frequency(receiver[1], f);
    } else {
      vfo[v].frequency = f;

      if (vfo[v].ctun) {
        vfo[v].ctun = FALSE;
        vfo[v].offset = 0;
        vfo[v].ctun_frequency = vfo[v].frequency;
      }
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// Set CTUN state of a VFO
//
void vfo_ctun_update(int id, int state) {
  CLIENT_MISSING;

  //
  // Note: if this VFO does not control a (running) receiver,
  //       receiver_set_frequency is *not* called therefore
  //       we should update ctun_frequency and offset
  //
  if (vfo[id].ctun == state) { return; }  // no-op if no change

  vfo[id].ctun = state;

  if (vfo[id].ctun) {
    // CTUN turned OFF->ON
    vfo[id].ctun_frequency = vfo[id].frequency;
    vfo[id].offset = 0;

    if (id < receivers) {
      receiver_set_frequency(receiver[id], vfo[id].ctun_frequency);
    }
  } else {
    // CTUN turned ON->OFF: keep frequency
    vfo[id].frequency = vfo[id].ctun_frequency;
    vfo[id].offset = 0;

    if (id < receivers) {
      receiver_set_frequency(receiver[id], vfo[id].ctun_frequency);
    }
  }
}
