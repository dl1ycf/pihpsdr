/* Copyright (C)
*  2017 - Markus Großer, DL8GM
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>

#include "discovered.h"
#include "discovery.h"
#include "main.h"
#include "message.h"
#include "radio.h"

// As we only run in the GTK+ main event loop, which is single-threaded and
// non-preemptive, we shouldn't need any additional synchronisation mechanisms.

//
// Starting an app on the Alpine Linux version of RedPitaya simply works
// by accessing the corresponding directory. There seems to be no stop command.
//
int alpine_start_app(const char * const app_id) {
  char app_start_url[256];
  char result[256];
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/%s/",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  run_curl(app_start_url, result, sizeof(result), 5);
  radio->protocol = ORIGINAL_PROTOCOL;
  return 0;
}

//
// Starting the app on the STEMlab web server goes via the "bazaar"
//
int stemlab_start_app(const char * const app_id) {
  // Dummy string, using the longest possible app id
  char app_start_url[256];
  char result[256];
  //
  // If there is already an SDR application running on the RedPitaya,
  // starting the SDR app might lead to an unpredictable state, unless
  // the "killall" command from stop.sh is included in start.sh but this
  // is not done at the factory.
  // Therefore, we first stop the program (this essentially includes the
  // command "killall sdr_transceiver_hpsdr") and then start it.
  // We return with value 0 if everything went OK, else we return -1.
  //
  //
  // stop command
  //
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/bazaar?stop=%s",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  run_curl(app_start_url, result, sizeof(result), 5);
  //
  // start command
  //
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/bazaar?start=%s",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  run_curl(app_start_url, result, sizeof(result), 5);
  // Since the SDR application is now running, we can hand it over to the
  // regular HPSDR protocol handling code
  radio->protocol = ORIGINAL_PROTOCOL;
  return 0;
}

//
// This version of stemlab_discovery() needs libcurl
// but does not need avahi.
//
// Therefore we try to find the SDR apps on the RedPitaya
// assuming is has the (fixed) ip address which we can now set
// in the discovery menu and which is saved to a local file.
//
// After starting the app in the main discover menu, we
// have to re-discover to get full info and start the radio.
//

void stemlab_discovery(void) {
  char txt[256];
  char result[2048];
  int app_list;
  struct sockaddr_in ip_address;
  struct sockaddr_in netmask;
  ip_address.sin_family = AF_INET;
  if (strlen(ipaddr_radio) < 3) {
    return;
  }
  if (inet_aton(ipaddr_radio, &ip_address.sin_addr) == 0) {
    t_print("%s: TCP %s is invalid!\n", __func__, ipaddr_radio);
    return;
  }
  t_print("%s: using inet addr %s\n", __func__, ipaddr_radio);
  netmask.sin_family = AF_INET;
  inet_aton("0.0.0.0", &netmask.sin_addr);
  //
  // Do a HEAD request (poor curl's ping) to see whether the device is on-line
  // allow a 5 sec time-out
  //
  app_list = 0;
  snprintf(txt, sizeof(txt), "http://%s", ipaddr_radio);
  if (run_curl(txt, result, sizeof(result), 5) != 0) { return; }
  if (g_strstr_len(result, sizeof(result), "\"sdr_receiver_hpsdr\"") != NULL) {
    app_list |= STEMLAB_PAVEL_RX | BARE_REDPITAYA;
  }
  if (g_strstr_len(result, sizeof(result), "\"sdr_transceiver_hpsdr\"") != NULL) {
    app_list |= STEMLAB_PAVEL_TRX | BARE_REDPITAYA;
  }
  //
  // Determine which SDR apps are present on the RedPitaya. The list may be empty.
  //
  if (app_list == 0) {
    snprintf(txt, sizeof(txt), "http://%s/bazaar?apps=", ipaddr_radio);
    run_curl(txt, result, sizeof(result), 20);
    if (g_strstr_len(result, sizeof(result), "\"sdr_receiver_hpsdr\":") != NULL) {
      app_list |= STEMLAB_PAVEL_RX;
    }
    if (g_strstr_len(result, sizeof(result), "\"sdr_transceiver_hpsdr\":") != NULL) {
      app_list |= STEMLAB_PAVEL_TRX;
    }
    if (g_strstr_len(result, sizeof(result), "\"stemlab_sdr_transceiver_hpsdr\":") != NULL) {
      app_list |= STEMLAB_RP_TRX;
    }
    if (g_strstr_len(result, sizeof(result), "\"hamlab_sdr_transceiver_hpsdr\":") != NULL) {
      app_list |= HAMLAB_RP_TRX;
    }
  }
  if (app_list == 0) {
    t_print( "%s: Could contact web server but no SDR apps found.\n", __func__);
    return;
  }
  //
  // Constructe "device" descripter. Hi-Jack the software version field to
  // encode which RedPitaya apps are present.
  // What is needed in the interface data is only network.address.sin_addr,
  // but the address and netmask of the interface must be compatible with this
  // address to avoid an error condition upstream. That means
  // (addr & mask) == (interface_addr & mask) must be fulfilled. This is easily
  // achieved by setting interface_addr = addr and mask = 0.
  //
  DISCOVERED *dev = &discovered[devices++];
  dev->protocol = STEMLAB_PROTOCOL;
  dev->device = DEVICE_METIS;                                     // not used
  snprintf(dev->name, sizeof(dev->name), "STEMlab");
  dev->software_version = app_list;                               // encodes list of SDR apps present
  dev->status = STATE_AVAILABLE;
  memset(dev->network.mac_address, 0, 6);                    // not used
  dev->network.address_length = sizeof(struct sockaddr_in);
  dev->network.address.sin_family = AF_INET;
  dev->network.address.sin_addr = ip_address.sin_addr;
  dev->network.address.sin_port = htons(1024);
  dev->network.interface_length = sizeof(struct sockaddr_in);
  dev->network.interface_address = ip_address;                // same as RedPitaya address
  dev->network.interface_netmask = netmask;                   // does not matter
  snprintf(dev->network.interface_name, sizeof(dev->network.interface_name), "%s", "");
}
