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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <string.h>
#include <errno.h>

#include "discovered.h"
#include "discovery.h"
#include "message.h"

static char interface_name[64];
static struct sockaddr_in interface_addr = {0};
static struct sockaddr_in interface_netmask = {0};

#define DISCOVERY_PORT 1024
static int discovery_socket;

static void new_discover(struct ifaddrs* iface, int discflag);

static GThread *discover_thread_id;
static gpointer new_discover_receive_thread(gpointer data);

void new_discovery() {
  struct ifaddrs *addrs,*ifa;
  //
  // Start with discovering from a fixed ip address
  //
  int previous_devices = devices;
  new_discover(NULL, 2);

  //
  // If we have been successful with the fixed IP address,
  // assume that we want that radio, and do not discover any
  // more.
  //
  if (devices <= previous_devices) {
    getifaddrs(&addrs);
    ifa = addrs;

    while (ifa) {
      g_main_context_iteration(NULL, 0);

      //
      // Sometimes there are many (virtual) interfaces, and some
      // of them are very unlikely to offer a radio connection.
      // These are skipped.
      //
      if (ifa->ifa_addr) {
        if (
          ifa->ifa_addr->sa_family == AF_INET
          && (ifa->ifa_flags & IFF_UP) == IFF_UP
          && (ifa->ifa_flags & IFF_RUNNING) == IFF_RUNNING
          && (ifa->ifa_flags & IFF_LOOPBACK) != IFF_LOOPBACK
          && strncmp("veth", ifa->ifa_name, 4)
          && strncmp("dock", ifa->ifa_name, 4)
          && strncmp("hass", ifa->ifa_name, 4)
        ) {
          new_discover(ifa, 1);   // send UDP broadcast packet to interface
        }
      }

      ifa = ifa->ifa_next;
    }

    freeifaddrs(addrs);
  }

  t_print( "new_discovery found %d devices\n", devices);
}

//
// discflag = 1: send UDP broadcast packet
// discflag = 2: send UDP packet to specified IP address
//
static void new_discover(struct ifaddrs* iface, int discflag) {
  int rc;
  struct sockaddr_in *sa;
  struct sockaddr_in *mask;
  unsigned char buffer[60];
  struct sockaddr_in to_addr = {0};
  int i;

  switch (discflag) {
  case 1:
    //
    // prepeare socket for sending an UDP broadcast packet to interface ifa
    //
    snprintf(interface_name, sizeof(interface_name), "%s", iface->ifa_name);
    t_print("new_discover: looking for HPSDR devices on %s\n", interface_name);
    // send a broadcast to locate metis boards on the network
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("new_discover: create socket failed for discovery_socket\n");
      return;
    }

    sa = (struct sockaddr_in *) iface->ifa_addr;
    mask = (struct sockaddr_in *) iface->ifa_netmask;
    interface_netmask.sin_addr.s_addr = mask->sin_addr.s_addr;
    // bind to this interface and the discovery port
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_addr.s_addr = sa->sin_addr.s_addr;
    interface_addr.sin_port = htons(0); // system assigned port

    if (bind(discovery_socket, (struct sockaddr * )&interface_addr, sizeof(interface_addr)) < 0) {
      t_perror("new_discover: bind socket failed for discovery_socket\n");
      close (discovery_socket);
      return;
    }

    t_print("new_discover: bound to %s\n", interface_name);
    // allow broadcast on the socket
    int on = 1;
    rc = setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    if (rc != 0) {
      t_print("new_discover: cannot set SO_BROADCAST: rc=%d\n", rc);
      close (discovery_socket);
      return;
    }

    // setup to address
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);
    to_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    break;

  case 2:
    //
    // prepeare socket for sending an UPD packet to ipaddr_radio
    //
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);

    if (inet_aton(ipaddr_radio, &to_addr.sin_addr) == 0) {
      return;
    }

    t_print("discover: looking for HPSDR device with IP %s\n", ipaddr_radio);
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for discovery_socket:");
      return;
    }

    break;

  default:
    return;
    break;
  }

  int optval = 1;
  setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
  rc = devices;
  // start a receive thread to collect discovery response packets
  discover_thread_id = g_thread_new( "new discover receive", new_discover_receive_thread, GINT_TO_POINTER(discflag));
  // send discovery packet
  buffer[0] = 0x00;
  buffer[1] = 0x00;
  buffer[2] = 0x00;
  buffer[3] = 0x00;
  buffer[4] = 0x02;

  for (i = 5; i < 60; i++) {
    buffer[i] = 0x00;
  }

  if (sendto(discovery_socket, buffer, 60, 0, (struct sockaddr * )&to_addr, sizeof(to_addr)) < 0) {
    t_perror("new_discover: sendto socket failed for discovery_socket\n");
    close (discovery_socket);
    return;
  }

  // wait for receive thread to complete
  g_thread_join(discover_thread_id);
  close(discovery_socket);

  switch (discflag) {
  case 1:
    t_print("new_discover: exiting discover for %s\n", iface->ifa_name);
    break;

  case 2:
    t_print("discover: exiting HPSDR discover for IP %s\n", ipaddr_radio);

    if (devices == rc + 1) {
      //
      // METIS detection UDP packet sent to fixed IP address got a valid response.
      //
      memcpy((void *)&discovered[rc].network.address, (void *)&to_addr, sizeof(to_addr));
      discovered[rc].network.address_length = sizeof(to_addr);
      snprintf(discovered[rc].network.interface_name, sizeof(discovered[rc].network.interface_name), "UDP");
      discovered[rc].use_routing = 1;
    }

    break;
  }
}

static gpointer new_discover_receive_thread(gpointer data) {
  struct sockaddr_in addr;
  socklen_t len;
  unsigned char buffer[2048];
  struct timeval tv;
  int i;
  int flag = GPOINTER_TO_INT(data);
  int oldnumdev = devices;
  double frequency_min, frequency_max;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
  len = sizeof(addr);

  while (1) {
    if (flag != 1 && devices > oldnumdev) {
      //
      // For a 'directed' (UDP) discovery packet, return as soon as there
      // has been a valid answer.
      //
      break;
    }

    int bytes_read = recvfrom(discovery_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &len);

    if (bytes_read < 0) {
      t_print("new_discover: bytes read %d\n", bytes_read);
      t_perror("new_discover: recvfrom socket failed for discover_receive_thread");
      break;
    }

    t_print("new_discover: received %d bytes\n", bytes_read);

    if (bytes_read == 1444) {
      if (devices > 0) {
        break;
      }
    } else {
      if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 0) {
        int status = buffer[4] & 0xFF;

        if (status == 2 || status == 3) {
          if (devices < MAX_DEVICES) {
            discovered[devices].protocol = NEW_PROTOCOL;
            discovered[devices].device = buffer[11] & 0xFF;
            discovered[devices].software_version = buffer[13] & 0xFF;
            discovered[devices].status = status;
            //
            // The NEW_DEVICE_XXXX numbers are just 1000+board_id
            //
            discovered[devices].device += 1000;

            switch (discovered[devices].device) {
            case NEW_DEVICE_ATLAS:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Atlas");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_HERMES:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Atlas");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_HERMES2:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Hermes2");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_ANGELIA:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Angelia");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_ORION:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Orion");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_ORION2:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Orion2");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_SATURN:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Saturn/G2");
              frequency_min = 0.0;
              frequency_max = 61440000.0;
              break;

            case NEW_DEVICE_HERMES_LITE:
              if (discovered[devices].software_version < 40) {
                snprintf(discovered[devices].name, sizeof(discovered[devices].name), "HermesLite V1");
              } else {
                snprintf(discovered[devices].name, sizeof(discovered[devices].name), "HermesLite V2");
                discovered[devices].device = NEW_DEVICE_HERMES_LITE2;
              }

              frequency_min = 0.0;
              frequency_max = 30720000.0;
              break;

            default:
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Unknown");
              frequency_min = 0.0;
              frequency_max = 30720000.0;
              break;
            }

            for (i = 0; i < 6; i++) {
              discovered[devices].network.mac_address[i] = buffer[i + 5];
            }

            memcpy((void*)&discovered[devices].network.address, (void*)&addr, sizeof(addr));
            discovered[devices].network.address_length = sizeof(addr);
            memcpy((void*)&discovered[devices].network.interface_address, (void*)&interface_addr, sizeof(interface_addr));
            memcpy((void*)&discovered[devices].network.interface_netmask, (void*)&interface_netmask,
                   sizeof(interface_netmask));
            discovered[devices].network.interface_length = sizeof(interface_addr);
            snprintf(discovered[devices].network.interface_name, sizeof(discovered[devices].network.interface_name), "%s",
                     interface_name);
            discovered[devices].supported_receivers = 2;
            //
            // Info not yet made use of:
            //
            // buffer[12]: P2 version supported (e.g. 39 for 3.9)
            // buffer[20]: number of DDCs
            // buffer[23]: beta version number (if nonzero)
            //             E.g. if buffer[13] is 21 and buffer[23] is 18 this
            //             means firmware Version 2.1.18
            //
            // We put the additional info to stderr at least since it might be
            // useful for debugging/development but do not store it in the
            // "discovered" data structure.
            //
            t_print("new_discover: P2(%d)  device=%d (%dRX) software_version=%d(.%d) status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
                    buffer[12] & 0xFF,
                    discovered[devices].device - 1000,
                    buffer[20] & 0xFF,
                    discovered[devices].software_version,
                    buffer[23] & 0xFF,
                    discovered[devices].status,
                    inet_ntoa(discovered[devices].network.address.sin_addr),
                    discovered[devices].network.mac_address[0],
                    discovered[devices].network.mac_address[1],
                    discovered[devices].network.mac_address[2],
                    discovered[devices].network.mac_address[3],
                    discovered[devices].network.mac_address[4],
                    discovered[devices].network.mac_address[5],
                    discovered[devices].network.interface_name);
            discovered[devices].frequency_min = frequency_min;
            discovered[devices].frequency_max = frequency_max;
            devices++;
          }
        }
      }
    }
  }

  t_print("new_discover: exiting new_discover_receive_thread\n");
  g_thread_exit(NULL);
  return NULL;
}
