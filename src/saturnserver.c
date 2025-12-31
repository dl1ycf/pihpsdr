/* Copyright (C)
* 2021 - Laurence Barker G8NJJ
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

/////////////////////////////////////////////////////////////
//
// Saturn project: Artix7 FPGA + Raspberry Pi4 Compute Module
// PCI Express interface from linux on Raspberry pi
// this application uses C code to emulate HPSDR protocol 2
//
// saturnserver.c:
//
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// Protocol2 is defined by "openHPSDR Ethernet Protocol V3.8"
// unlike protocol 1, it uses multiple ports for the data endpoints
//
// Saturn network interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <semaphore.h>

#include "message.h"
#include "saturndrivers.h"
#include "saturnmain.h"
#include "saturnregisters.h"              // register I/O for Saturn
#include "saturnserver.h"

bool saturn_server_en = false;
struct sockaddr_in server_reply_addr;              // destination address for outgoing data
bool ReplyAddressSet = false;               // true when reply address has been set
bool StartBitReceived = false;              // true when "run" bit has been set
bool ServerActive = false;

static bool NewMessageReceived = false;            // set whenever a message is received
static bool ExitRequested = false;                 // true if "exit checking" thread requests shutdown
static bool ThreadError = false;                   // true if a thread reports an error

#define VDISCOVERYSIZE 60                   // discovery packet
#define VDISCOVERYREPLYSIZE 60              // reply packet
#define VWIDEBANDSIZE 1028                  // wideband scalar samples
#define VCONSTTXAMPLSCALEFACTOR 0x0001FFFF  // 18 bit scale value - set to 1/2 of full scale

struct ThreadSocketData SocketData[VPORTTABLESIZE] = {
  {0, 0, 1024, "Cmd", false, {}, 0, 0},                     // command (incoming) thread
  {0, 0, 1025, "DDC Specific", false, {}, 0, 0},            // DDC specifc (incoming) thread
  {0, 0, 1026, "DUC Specific", false, {}, 0, 0},            // DUC specific (incoming) thread
  {0, 0, 1027, "High Priority In", false, {}, 0, 0},        // High Priority (incoming) thread
  {0, 0, 1028, "Spkr Audio", false, {}, 0, 0},              // Speaker Audio (incoming) thread
  {0, 0, 1029, "DUC I/Q", false, {}, 0, 0},                 // DUC IQ (incoming) thread
  {0, 0, 1025, "High Priority Out", false, {}, 0, 0},       // High Priority (outgoing) thread
  {0, 0, 1026, "Mic Audio", false, {}, 0, 0},               // Mic Audio (outgoing) thread
  {0, 0, 1035, "DDC I/Q 0", false, {}, 0, 0},               // DDC IQ 0 (outgoing) thread
  {0, 0, 1036, "DDC I/Q 1", false, {}, 0, 0},               // DDC IQ 1 (outgoing) thread
  {0, 0, 1037, "DDC I/Q 2", false, {}, 0, 0},               // DDC IQ 2 (outgoing) thread
  {0, 0, 1038, "DDC I/Q 3", false, {}, 0, 0},               // DDC IQ 3 (outgoing) thread
  {0, 0, 1039, "DDC I/Q 4", false, {}, 0, 0},               // DDC IQ 4 (outgoing) thread
  {0, 0, 1040, "DDC I/Q 5", false, {}, 0, 0},               // DDC IQ 5 (outgoing) thread
  {0, 0, 1041, "DDC I/Q 6", false, {}, 0, 0},               // DDC IQ 6 (outgoing) thread
  {0, 0, 1042, "DDC I/Q 7", false, {}, 0, 0},               // DDC IQ 7 (outgoing) thread
  {0, 0, 1043, "DDC I/Q 8", false, {}, 0, 0},               // DDC IQ 8 (outgoing) thread
  {0, 0, 1044, "DDC I/Q 9", false, {}, 0, 0},               // DDC IQ 9 (outgoing) thread
  {0, 0, 1027, "Wideband 0", false, {}, 0, 0},              // Wideband 0 (outgoing) thread
  {0, 0, 1028, "Wideband 1", false, {}, 0, 0}               // Wideband 1 (outgoing) thread
};

static pthread_t saturn_server_thread;
static pthread_t DDCSpecificThread;
static pthread_t HighPriorityToSDRThread;
static pthread_t WatchDogThread;           // thread looks for inactvity

static void* saturn_server(void *arg);
static void *IncomingDDCSpecific(void *arg);           // listener thread
static void *IncomingHighPriority(void *arg);          // listener thread

//
// function to make an incoming or outgoing socket, bound to the specified port in the structure
// 1st parameter is a link into the socket data table
//
static int MakeSocket(struct ThreadSocketData* Ptr, int DDCid) {
  struct timeval ReadTimeout;                                       // read timeout
  int yes = 1;

  //  struct sockaddr_in addr_cmddata;
  //
  // create socket for incoming data
  //
  if ((Ptr->Socketid = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    t_perror("socket fail");
    return EXIT_FAILURE;
  }

  //
  // set 1ms timeout, and re-use any recently open ports
  //
  setsockopt(Ptr->Socketid, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  ReadTimeout.tv_sec = 0;
  ReadTimeout.tv_usec = 1000;
  setsockopt(Ptr->Socketid, SOL_SOCKET, SO_RCVTIMEO, (void *)&ReadTimeout, sizeof(ReadTimeout));
  //
  // bind application to the specified port
  //
  memset(&Ptr->addr_cmddata, 0, sizeof(struct sockaddr_in));
  Ptr->addr_cmddata.sin_family = AF_INET;
  Ptr->addr_cmddata.sin_addr.s_addr = htonl(INADDR_ANY);
  Ptr->addr_cmddata.sin_port = htons(Ptr->Portid);

  if (bind(Ptr->Socketid, (struct sockaddr *)&Ptr->addr_cmddata, sizeof(struct sockaddr_in)) < 0) {
    t_perror("bind");
    return EXIT_FAILURE;
  }

  struct sockaddr_in checkin;

  socklen_t len = sizeof(checkin);

  if (getsockname(Ptr->Socketid, (struct sockaddr *)&checkin, &len) == -1) {
    t_perror("getsockname");
  }

  Ptr->DDCid = DDCid;                       // set DDC number, for outgoing ports
  return 0;
}

//
// Watchdog thread.
// if no messages in a second, goes back to "inactive" state.
//
// cppcheck-suppress constParameterCallback
static void* SaturnServerWatchdog(void *arg) {
  while (1) {
    sleep(5);                                   // wait for 5 seconds
    bool PreviouslyActiveState = ServerActive;     // see if active on entry

    if (!NewMessageReceived) {  // if no messages received,
      ServerActive = false;                        // set back to inactive
      ReplyAddressSet = false;
      StartBitReceived = false;

      if (PreviouslyActiveState) {
        for (int i = 4; i < VNUMDDC; i++) {        // disable upper bank of DDCs
          SetP2SampleRate(i, false, 48, false);
        }

        WriteP2DDCRateRegister();
        t_print("Reverted to Inactive State after no activity\n");
      }
    }

    NewMessageReceived = false;
  }

  t_print("%s ended\n", __FUNCTION__);
  return NULL;
}

//
// perform ordely shutdown of the program
//
void shutdown_saturn_server() {
  ServerActive = false;
  close(SocketData[0].Socketid);                          // close incoming data socket
  ExitRequested = true;
  t_print("Shutdown COMPLETE\n");
}

void start_saturn_server() {
  ExitRequested = false;

  if (pthread_create(&saturn_server_thread, NULL, saturn_server, NULL) < 0) {
    t_perror("pthread_create saturn_server thread");
    return;
  }

  pthread_detach(saturn_server_thread);
}

//
// Initialise, then handle incoming command/general data
// has a loop that reads & processes incoming command packets
// see protocol documentation
//
static void* saturn_server(void *arg) {
  int i, size;
  uint8_t UDPInBuffer[VDISCOVERYSIZE];
  //
  // part written discovery reply packet
  //
  uint8_t DiscoveryReply[VDISCOVERYREPLYSIZE] = {
    0, 0, 0, 0,                                   // sequence bytes
    2,                                            // 2 if not active; 3 if active
    0, 0, 0, 0, 0, 0,                             // SDR (raspberry i) MAC address
    10,                                           // board type. Saturn
    39,                                           // protocol version 3.9
    20,                                           // this SDR firmware version. >17 to enable QSK
    0, 0, 0, 0, 0, 0,                             // Mercury, Metis, Penny version numbers
    6,                                            // 6 DDC's for network client
    1,                                            // phase word
    0,                                            // endian mode
    0, 0,                                         // beta version, reserved byte (total 25 useful bytes)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // 10 bytes padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // 10 bytes padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0      // 15 bytes padding
  };
  uint8_t CmdByte;                                                  // command word from PC app
  struct sockaddr_in addr_from;                                     // holds MAC address of source of incoming messages
  struct iovec iovecinst;                                           // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                                           // multiple incoming message header

  //
  // start up thread to check for no longer getting messages, to set back to inactive
  //
  if (pthread_create(&WatchDogThread, NULL, SaturnServerWatchdog, NULL) < 0) {
    t_perror("pthread_create check for exit");
    return NULL;
  }

  pthread_detach(WatchDogThread);
  //
  // create socket for incoming data on the command port
  //
  MakeSocket(SocketData, 0);
#if defined(__linux__)
  //
  // get MAC address of ethernet adapter "eth0"
  //
  struct ifreq hwaddr;                                              // holds this device MAC address
  memset(&hwaddr, 0, sizeof(hwaddr));
  strncpy(hwaddr.ifr_name, "eth0", IFNAMSIZ - 1);
  ioctl(SocketData[VPORTCOMMAND].Socketid, SIOCGIFHWADDR, &hwaddr);

  for (i = 0; i < 6; ++i) { DiscoveryReply[i + 5] = hwaddr.ifr_addr.sa_data[i]; }        // copy MAC to reply message

#else

  // BSD, or MacOS have a different mechanism of getting the hardware addr.
  // Since this is intended to work on RaspPi only, just use fake addr.
  for (i = 0; i < 6; ++i) { DiscoveryReply[i + 5] = 0xAA; }

#endif
  MakeSocket(SocketData + VPORTDDCSPECIFIC, 0);          // create and bind a socket

  if (pthread_create(&DDCSpecificThread, NULL, IncomingDDCSpecific, (void * )&SocketData[VPORTDDCSPECIFIC]) < 0) {
    t_perror("pthread_create DDC specific");
    return NULL;
  }

  pthread_detach(DDCSpecificThread);

  MakeSocket(SocketData + VPORTHIGHPRIORITYTOSDR, 0);          // create and bind a socket

  if (pthread_create(&HighPriorityToSDRThread, NULL, IncomingHighPriority,
                     (void * )&SocketData[VPORTHIGHPRIORITYTOSDR]) < 0) {
    t_perror("pthread_create High priority to SDR");
    return NULL;
  }

  pthread_detach(HighPriorityToSDRThread);

  //
  // create all the DDC sockets
  //
  MakeSocket(SocketData + VPORTDDCIQ0, 0);
  MakeSocket(SocketData + VPORTDDCIQ1, 1);
  MakeSocket(SocketData + VPORTDDCIQ2, 2);
  MakeSocket(SocketData + VPORTDDCIQ3, 3);
  MakeSocket(SocketData + VPORTDDCIQ4, 4);
  MakeSocket(SocketData + VPORTDDCIQ5, 5);
  MakeSocket(SocketData + VPORTDDCIQ6, 6);
  MakeSocket(SocketData + VPORTDDCIQ7, 7);
  MakeSocket(SocketData + VPORTDDCIQ8, 8);
  MakeSocket(SocketData + VPORTDDCIQ9, 9);

  //
  // now main processing loop. Process received Command packets arriving at port 1024
  // these are identified by the command byte (byte 4)
  // cmd=00: general packet
  // cmd=02: discovery
  // cmd=03: set IP address (not supported)
  // cmd=04: erase (not supported)
  // cmd=05: program (not supported)
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDISCOVERYSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    size = recvmsg(SocketData[0].Socketid, &datagram, 0);         // get one message. If it times out, gets size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, port 1024");
      return NULL;
    }

    if (ThreadError) {
      break;
    }

    //
    // only process packets of length 60 bytes on this port, to exclude protocol 1 discovery for example.
    // (that means we can't handle the programming packet but we don't use that anyway)
    //
    CmdByte = UDPInBuffer[4];

    if (size == VDISCOVERYSIZE) {
      NewMessageReceived = true;

      switch (CmdByte) {
      //
      // general packet: establish listener threads (with fixed port numbers)
      //
      case 0:
        //t_print("P2 General packet to SDR, size= %d\n", size);
        //
        // get "from" MAC address and port; this is where the data goes back to
        //
        memset(&server_reply_addr, 0, sizeof(server_reply_addr));
        server_reply_addr.sin_family = AF_INET;
        server_reply_addr.sin_addr.s_addr = addr_from.sin_addr.s_addr;
        server_reply_addr.sin_port = addr_from.sin_port;                       // (but each outgoing thread needs to set its own sin_port)
        ReplyAddressSet = true;

        if (StartBitReceived) {
          ServerActive = true;  // only set active if we have start bit too
        }

        break;

      //
      // discovery packet
      //
      case 2:
        t_print("P2 Discovery packet\n");

        if (ServerActive) {
          DiscoveryReply[4] = 3;  // response 2 if not active, 3 if running
        } else {
          DiscoveryReply[4] = 2;
        }

        memset(&UDPInBuffer, 0, VDISCOVERYREPLYSIZE);
        memcpy(&UDPInBuffer, DiscoveryReply, VDISCOVERYREPLYSIZE);
        sendto(SocketData[0].Socketid, &UDPInBuffer, VDISCOVERYREPLYSIZE, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;

      case 3:
      case 4:
      case 5:
        t_print("Unsupported packet\n");
        break;

      default:
        break;
      }// end switch (packet type)
    }

    //
    // now do any "post packet" processing
    //
  } //while(1)

  if (ThreadError) {
    t_print("Thread error reported - exiting\n");
  }

  //
  // clean exit
  //
  t_print("Exiting\n");
  shutdown_saturn_server();
  return NULL;
}

//
// listener thread for incoming high priority packets
//
static void *IncomingHighPriority(void *arg) {          // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VHIGHPRIOTIYTOSDRSIZE];           // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up high priority incoming thread with port %d\n", ThreadData->Portid);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VHIGHPRIOTIYTOSDRSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, high priority");
      t_print("error number = %d\n", errno);
      return NULL;
    }

    //
    // if correct packet, process it
    //
    if (size == VHIGHPRIOTIYTOSDRSIZE) {
      NewMessageReceived = true;
      saturn_handle_high_priority_server(UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}

//
// listener thread for incoming DDC specific packets
//
void *IncomingDDCSpecific(void *arg) {                  // listener thread
  struct ThreadSocketData *ThreadData;                  // socket etc data for this thread
  struct sockaddr_in addr_from;                         // holds MAC address of source of incoming messages
  uint8_t UDPInBuffer[VDDCSPECIFICSIZE];                // incoming buffer
  struct iovec iovecinst;                               // iovcnt buffer - 1 for each outgoing buffer
  struct msghdr datagram;                               // multiple incoming message header
  ThreadData = (struct ThreadSocketData *)arg;
  ThreadData->Active = true;
  t_print("spinning up DDC specific thread with port %d\n", ThreadData->Portid);

  //
  // main processing loop
  //
  while (!ExitRequested) {
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(datagram));
    iovecinst.iov_base = &UDPInBuffer;                  // set buffer for incoming message number i
    iovecinst.iov_len = VDDCSPECIFICSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &addr_from;
    datagram.msg_namelen = sizeof(addr_from);
    int size = recvmsg(ThreadData->Socketid, &datagram, 0);         // get one message. If it times out, ges size=-1

    if (size < 0 && errno != EAGAIN) {
      t_perror("recvfrom, DDC Specific");
      return NULL;
    }

    if (size == VDDCSPECIFICSIZE) {
      NewMessageReceived = true;
      saturn_handle_ddc_specific_server(UDPInBuffer);
    }
  }

  //
  // close down thread
  //
  close(ThreadData->Socketid);                  // close incoming data socket
  ThreadData->Socketid = 0;
  ThreadData->Active = false;                   // indicate it is closed
  return NULL;
}
