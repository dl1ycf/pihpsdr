/* Copyright (C)
* 2017 - Markus Großer, DL8GM
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>

#include "discovered.h"
#include "discovery.h"
#include "message.h"
#include "radio.h"

// As we only run in the GTK+ main event loop, which is single-threaded and
// non-preemptive, we shouldn't need any additional synchronisation mechanisms.

static bool curl_initialised = FALSE;
extern void status_text(const char *);

static size_t app_list_cb(void *buffer, size_t size, size_t nmemb, void *data);

// cppcheck-suppress constParameterCallback
static size_t get_list_cb(void *buffer, size_t size, size_t nmemb, void *data) {
  //
  // Scan output of original HEAD request, which is the HTML code of the
  // starting page. "barbone" RedPitayas running Alpine Linux will show
  // the existing apps, so look for them. STEMlab web servers are MUCH more
  // fancy and will not show the name of the apps in the initial HEAD
  // request.
  //
  int *software_version = (int*) data;
  const gchar *pavel_rx = "\"sdr_receiver_hpsdr\"";

  if (g_strstr_len(buffer, size * nmemb, pavel_rx) != NULL) {
    *software_version |= STEMLAB_PAVEL_RX | BARE_REDPITAYA;
  }

  const gchar *pavel_trx = "\"sdr_transceiver_hpsdr\"";

  if (g_strstr_len(buffer, size * nmemb, pavel_trx) != NULL) {
    *software_version |= STEMLAB_PAVEL_TRX | BARE_REDPITAYA;
  }

  // Returning the total amount of bytes "processed" to signal cURL that we
  // are done without any errors
  return size * nmemb;
}

// cppcheck-suppress constParameterCallback
static size_t app_list_cb(void *buffer, size_t size, size_t nmemb, void *data) {
  //
  // Analyze the JSON output of the "bazaar?app=" request and figure out
  // which applications are present. This is done the "pedestrian" way such
  // that we can build without a json library. Hopefully, the target strings
  // are not split across two buffers.
  // This is for STEMlab web servers.
  //
  int *software_version = (int*) data;
  const gchar *pavel_rx_json = "\"sdr_receiver_hpsdr\":";

  if (g_strstr_len(buffer, size * nmemb, pavel_rx_json) != NULL) {
    *software_version |= STEMLAB_PAVEL_RX;
  }

  const gchar *pavel_trx_json = "\"sdr_transceiver_hpsdr\":";

  if (g_strstr_len(buffer, size * nmemb, pavel_trx_json) != NULL) {
    *software_version |= STEMLAB_PAVEL_TRX;
  }

  const gchar *rp_trx_json = "\"stemlab_sdr_transceiver_hpsdr\":";

  if (g_strstr_len(buffer, size * nmemb, rp_trx_json) != NULL) {
    *software_version |= STEMLAB_RP_TRX;
  }

  const gchar *hamlab_trx_json = "\"hamlab_sdr_transceiver_hpsdr\":";

  if (g_strstr_len(buffer, size * nmemb, hamlab_trx_json) != NULL) {
    *software_version |= HAMLAB_RP_TRX;
  }

  // Returning the total amount of bytes "processed" to signal cURL that we
  // are done without any errors
  return size * nmemb;
}

//
// This is a no-op curl callback and swallows what is sent by
// the RedPitaya web server when starting the SDR application.
//
// cppcheck-suppress constParameterCallback
static size_t alpine_start_callback(void *buffer, size_t size, size_t nmemb, void *data) {
  return size * nmemb;
}

//
// Digest what the web server sends after starting the SDR app.
// It should show a status:OK message in the JSON output.
//
// cppcheck-suppress constParameterCallback
static size_t app_start_callback(void *buffer, size_t size, size_t nmemb, void *data) {
  if (strncmp(buffer, "{\"status\":\"OK\"}", size * nmemb) != 0) {
    t_print( "%s: Receiver error from STEMlab\n", __FUNCTION__);
    return 0;
  }

  return size * nmemb;
}

//
// Starting an app on the Alpine Linux version of RedPitaya simply works
// by accessing the corresponding directory. We could use a no-op instead of
// the WRITEFUNCTION, but this way we can activate debug output in
// alpine_start_cb.
//
int alpine_start_app(const char * const app_id) {
  // Dummy string, using the longest possible app id
  char app_start_url[] = "http://123.123.123.123/stemlab_sdr_transceiver_hpsdr_with_some_headroom";
  // The scripts on the "alpine" machine all contain code that
  // stops all running programs, so we need not stop any possible running app here
  CURL *curl_handle = curl_easy_init();
  CURLcode curl_error = CURLE_OK;

  if (curl_handle == NULL) {
    t_print( "%s: Failed to create cURL handle\n", __FUNCTION__);
    return -1;
  }

#define check_curl(description) do { \
  if (curl_error != CURLE_OK) { \
    t_print( "%s: " description ": %s\n", __FUNCTION__, \
        curl_easy_strerror(curl_error)); \
     return -1; \
  } \
}  while (0);
  //
  // Copy IP addr and name of app to app_start_url
  //
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/%s/",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_URL, app_start_url);
  check_curl("Failed setting cURL URL");
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, alpine_start_callback);
  check_curl("Failed install cURL callback");
  curl_error = curl_easy_perform(curl_handle);
  check_curl("Failed to start app");
#undef check_curl
  curl_easy_cleanup(curl_handle);
  // Since the SDR application is now running, we can hand it over to the
  // regular HPSDR protocol handling code
  radio->protocol = ORIGINAL_PROTOCOL;
  return 0;
}

//
// Starting the app on the STEMlab web server goes via the "bazaar"
//
int stemlab_start_app(const char * const app_id) {
  // Dummy string, using the longest possible app id
  char app_start_url[] = "http://123.123.123.123/bazaar?start=stemlab_sdr_transceiver_hpsdr_headroom_max";
  //
  // If there is already an SDR application running on the RedPitaya,
  // starting the SDR app might lead to an unpredictable state, unless
  // the "killall" command from stop.sh is included in start.sh but this
  // is not done at the factory.
  // Therefore, we first stop the program (this essentially includes the
  // command "killall sdr_transceiver_hpsdr") and then start it.
  // We return with value 0 if everything went OK, else we return -1.
  //
  CURL *curl_handle = curl_easy_init();
  CURLcode curl_error = CURLE_OK;

  if (curl_handle == NULL) {
    t_print( "%s: Failed to create cURL handle\n", __FUNCTION__);
    return -1;
  }

#define check_curl(description) do { \
  if (curl_error != CURLE_OK) { \
    t_print( "%s: " description ": %s\n", __FUNCTION__, \
        curl_easy_strerror(curl_error)); \
     return -1; \
  } \
}  while (0);
  //
  // stop command
  //
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/bazaar?stop=%s",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_URL, app_start_url);
  check_curl("Failed setting cURL URL");
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, app_start_callback);
  check_curl("Failed install cURL callback");
  curl_error = curl_easy_perform(curl_handle);
  check_curl("Failed to stop app");
  //
  // start command
  //
  snprintf(app_start_url, sizeof(app_start_url), "http://%s/bazaar?start=%s",
           inet_ntoa(radio->network.address.sin_addr),
           app_id);
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_URL, app_start_url);
  check_curl("Failed setting cURL URL");
  curl_error = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, app_start_callback);
  check_curl("Failed install cURL callback");
  curl_error = curl_easy_perform(curl_handle);
  check_curl("Failed to start app");
#undef check_curl
  curl_easy_cleanup(curl_handle);
  // Since the SDR application is now running, we can hand it over to the
  // regular HPSDR protocol handling code
  radio->protocol = ORIGINAL_PROTOCOL;
  return 0;
}

void stemlab_cleanup() {
  if (curl_initialised) {
    curl_global_cleanup();
  }
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

void stemlab_discovery() {
  char txt[256];
  CURL *curl_handle;
  CURLcode curl_error;
  int app_list;
  struct sockaddr_in ip_address;
  struct sockaddr_in netmask;
  t_print("%s: using inet addr %s\n", __FUNCTION__, ipaddr_radio);
  ip_address.sin_family = AF_INET;

  if (inet_aton(ipaddr_radio, &ip_address.sin_addr) == 0) {
    t_print("%s: TCP %s is invalid!\n", __FUNCTION__, ipaddr_radio);
    return;
  }

  netmask.sin_family = AF_INET;
  inet_aton("0.0.0.0", &netmask.sin_addr);
  //
  // Do a HEAD request (poor curl's ping) to see whether the device is on-line
  // allow a 5 sec time-out
  //
  curl_handle = curl_easy_init();

  if (curl_handle == NULL) {
    t_print( "%s: Failed to create cURL handle\n", __FUNCTION__);
    return;
  }

  app_list = 0;
  snprintf(txt, sizeof(txt), "http://%s", ipaddr_radio);
  curl_easy_setopt(curl_handle, CURLOPT_URL, txt);
  curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, (long) 5);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, get_list_cb);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &app_list);
  curl_error = curl_easy_perform(curl_handle);
  curl_easy_cleanup(curl_handle);

  if (curl_error ==  CURLE_OPERATION_TIMEDOUT) {
    snprintf(txt, sizeof(txt), "No response from web server at %s", ipaddr_radio);
    status_text(txt);
    t_print("%s: %s\n", __FUNCTION__, txt);
  }

  if (curl_error != CURLE_OK) {
    t_print( "%s: ping error: %s\n", __FUNCTION__, curl_easy_strerror(curl_error));
    return;
  }

  //
  // Determine which SDR apps are present on the RedPitaya. The list may be empty.
  //
  if (app_list == 0) {
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
      t_print( "%s: Failed to create cURL handle\n", __FUNCTION__);
      return;
    }

    snprintf(txt, sizeof(txt), "http://%s/bazaar?apps=", ipaddr_radio);
    curl_easy_setopt(curl_handle, CURLOPT_URL, txt);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, (long) 20);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, app_list_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &app_list);
    curl_error = curl_easy_perform(curl_handle);
    curl_easy_cleanup(curl_handle);

    if (curl_error == CURLE_OPERATION_TIMEDOUT) {
      status_text("No Response from RedPitaya in 20 secs");
      t_print("%s: TimeOut met when trying to get list of HPSDR apps from RedPitaya\n", __FUNCTION__);
    }

    if (curl_error != CURLE_OK) {
      t_print( "%s: app-list error: %s\n", __FUNCTION__, curl_easy_strerror(curl_error));
      return;
    }
  }

  if (app_list == 0) {
    t_print( "%s: Could contact web server but no SDR apps found.\n", __FUNCTION__);
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
