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
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// saturnmain.c: based on p2app client app software
//
// Saturn interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#include <gtk/gtk.h>
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
#include <sys/stat.h>

#include "discovered.h"
#include "message.h"
#include "new_protocol.h"
#include "saturndrivers.h"
#include "saturnmain.h"
#include "saturnregisters.h"
#include "saturnserver.h"

static bool HaveMOX;                                   // true if in TX
static bool SDRActive;                                  // true if this SDR is running at the moment
static bool Exiting = false;

//
// For "minor" versions up to 17, there is no "major" one.
// For "minor" version 18,  the "major" version is 1
// With each firmware update, the "minor" version is increased (it is not reset upon advancing the major)
// The "major" version is increased if piHPSDR compatibility is broken
//
#define FIRMWARE_MIN_MINOR            8          // Minimum FPGA "minor" software version that this software can run
#define FIRMWARE_MAX_MINOR            18         // Maximum FPGA "minor" software version that this software can run
#define FIRMWARE_MIN_MAJOR            1          // Minimum FPGA "major" software version that this software can run
#define FIRMWARE_MAX_MAJOR            1          // Minimum FPGA "major" software version that this software can run

#define VCONSTTXAMPLSCALEFACTOR       0x0001FFFF // 18 bit scale value - set to 1/2 of full scale
#define VCONSTTXAMPLSCALEFACTOR_13    0x0002000  // 18 bit scale value - set to 1/32 of full scale FWV13+
#define VCONSTTXAMPLSCALEFACTOR_PCBV3 0x0002A00  // 18 bit scale value - set to 1/32 of full scale for PCB V3
#define VDMATRANSFERSIZE              4096
#define VDMABUFFERSIZE                131072     // memory buffer to reserve (4x DDC FIFO so OK)
#define VALIGNMENT                    4096       // buffer alignment
#define VBASE                         0x1000     // offset into I/Q buffer for DMA to start
#define VIQSAMPLESPERFRAME            238
#define VIQBYTESPERFRAME              6*VIQSAMPLESPERFRAME // total bytes in one outgoing frame
#define VIQDUCSAMPLESPERFRAME         240

#define VSPKSAMPLESPERFRAME           64         // samples per UDP frame
#define VMEMWORDSPERFRAME             32         // 8 byte writes per UDP msg
#define VSPKSAMPLESPERMEMWORD         2          // 2 samples (each 4 bytres) per 8 byte word
#define VDMASPKBUFFERSIZE             32768      // memory buffer to reserve
#define VDMASPKTRANSFERSIZE           256        // write 1 message at a time

#define VMICSAMPLESPERFRAME           64
#define VDMAMICBUFFERSIZE             32768      // memory buffer to reserve
#define VDMAMICTRANSFERSIZE           128        // read 1 message at a time
#define VMICPACKETSIZE                132

// uncomment to display debug printouts for FPGA data over/under flows
//#define DISPLAY_OVER_UNDER_FLOWS 1

static gpointer saturn_rx_thread(gpointer arg);
static GThread *saturn_rx_thread_id;
static gpointer saturn_micaudio_thread(gpointer arg);
static GThread *saturn_micaudio_thread_id;
static gpointer saturn_high_priority_thread(gpointer arg);
static GThread *saturn_high_priority_thread_id;
//
// code to allocate and free dynamic allocated memory
// first the memory buffers:
//
static uint8_t* DMAReadBuffer = NULL;                   // data for DMA read from DDC
static uint32_t DMABufferSize = VDMABUFFERSIZE;
static unsigned char* DMAReadPtr;                       // pointer for 1st available location in DMA memory
static unsigned char* DMAHeadPtr;                       // ptr to 1st free location in DMA memory
static unsigned char* DMABasePtr;                       // ptr to target DMA location in DMA memory

static uint8_t* DDCSampleBuffer[VNUMDDC];               // buffer per DDC
static unsigned char* IQReadPtr[VNUMDDC];               // pointer for reading out an I or Q sample
static unsigned char* IQHeadPtr[VNUMDDC];               // ptr to 1st free location in I/Q memory
static unsigned char* IQBasePtr[VNUMDDC];               // ptr to DMA location in I/Q memory

// Memory buffers to be exchanged with PiHPSDR APIs
#define DDCMYBUF 0
#define MICMYBUF 1
#define HPMYBUF  2
#define MAXMYBUF 3
//
// number of buffers allocated (for statistics)
//
static int num_buf[MAXMYBUF];

//
// head of buffer list
//
static mybuffer *buflist[MAXMYBUF];

//
// Obtain a free buffer. If no one is available allocate
// new ones. Note these buffer "live" as long as the
// program lives. They are never released.
//
static mybuffer *get_my_buffer(int numlist) {
  int i, j, first;
  const char *desc;
  mybuffer *bp = buflist[numlist];

  while (bp) {
    if (bp->free) {
      // found free buffer. Mark as used and return that one.
      bp->free = 0;
      return bp;
    }

    bp = bp->next;
  }

  //
  // No buffer free, or the first time we request a buffer:
  // allocate (a) new one(s). Note we need very few
  // HighPrio buffers, a limited amount of MicSample buffers,
  // and a possibly large amount of DDC IQ buffers.
  //
  first = (bp == NULL);

  switch (numlist) {
  case HPMYBUF:
    j = 1;  // allocate 1 new buffer
    desc = "HP";
    break;

  case MICMYBUF:
    j = 5;  // allocate 5 new buffers
    desc = "MIC";
    break;

  case DDCMYBUF:
    j = 25; // allocate 25 new buffers
    desc = "DDC";
    break;

  default:
    // NOTREACHED
    j = 5;
    desc = "UNKNOWN";
    break;
  }

  for (i = 0; i < j; i++) {
    bp = malloc(sizeof(mybuffer));

    if (bp) {
      bp->free = 1;
      bp->next = buflist[numlist];
      buflist[numlist] = bp;
      num_buf[numlist]++;
    }
  }

  t_print("%s: number of buffer[%s] %s to %d\n", __FUNCTION__, desc,
          first ? "set" : "increased", num_buf[numlist]);
  // Mark the first buffer in list as used and return that one.
  buflist[numlist]->free = 0;
  return buflist[numlist];
}

void saturn_free_buffers() {
  //
  // Mark all buffers as "free" but do not release storage
  // This is called upon a protocol restart
  //
  mybuffer *mybuf;

  for (int i = 0; i < MAXMYBUF; i++) {
    mybuf = buflist[i];

    while (mybuf) {
      mybuf->free = 1;
      mybuf = mybuf->next;
    }
  }
}

//
// Return "true" if allocation failed
// from P2_app/OutDDCIQ.c commit 935592526fd0e144c3bc29b39cb18246c483bce0
//
static bool CreateDynamicMemory(void) {                     // return true if error
  //
  // first create the buffer for DMA, and initialise its pointers
  //
  posix_memalign((void**)&DMAReadBuffer, VALIGNMENT, DMABufferSize);

  if (!DMAReadBuffer) {
    t_print("DMA read buffer allocation failed\n");
    return true;
  }

  memset(DMAReadBuffer, 0, DMABufferSize);
  DMAReadPtr = DMAReadBuffer + VBASE;  // at offset "VBASE" in the buffer
  DMAHeadPtr = DMAReadBuffer + VBASE;
  DMABasePtr = DMAReadBuffer + VBASE;

  //
  // set up per-DDC data structures
  //
  for (int DDC = 0; DDC < VNUMDDC; DDC++) {
    DDCSampleBuffer[DDC] = malloc(DMABufferSize);

    if (!DDCSampleBuffer[DDC]) {
      t_print("DDC%d buffer allocation failed\n", DDC);
      return true;
    }

    IQReadPtr[DDC] = DDCSampleBuffer[DDC] + VBASE; // at offset "VBASE" in the buffer
    IQHeadPtr[DDC] = DDCSampleBuffer[DDC] + VBASE;
    IQBasePtr[DDC] = DDCSampleBuffer[DDC] + VBASE;
  }

  return false;
}

/////////////////////////////////////////////////////////////////////
//
// code from P2_app/p2app.c main(), commit 935592526fd0e144c3bc29b39cb18246c483bce0
//
// Note OpenXDMADriver() is done in saturn_discovery, and
// version numbers are retrieved there
//
/////////////////////////////////////////////////////////////////////

static void saturn_register_init() {
  //
  // setup Saturn hardware
  //
  SetSpkrMute(true);                                                // mute speaker before initialising codec
  usleep(10000);
  CodecInitialise();
  InitialiseDACAttenROMs();
  SetKeyerParams(30, 500, 9);                                       // RFdelay=30ms, HangTime=500ms, RampLength=9ms
  SetCWSideTone(true, 50, 800);                                     // enable side tone, Volume=50, 800 Hz
  SetTXProtocol2();                                                 // set to protocol 2
  EnableCW(false, false);
  SetByteSwapping(true);                                            // h/w to generate NOT network byte order
  SetSpkrMute(false);

  if (Saturn_PCB_Version <= 2) {
    if (FPGA_MinorVersion < 13) {
      SetTXAmplitudeScaling(VCONSTTXAMPLSCALEFACTOR);               // for firmware version up to 1.2 (old PCB)
    } else {
      SetTXAmplitudeScaling(VCONSTTXAMPLSCALEFACTOR_13);            // for  firmware version 1.3 .... (old PCB)
    }
  } else {
    SetTXAmplitudeScaling(VCONSTTXAMPLSCALEFACTOR_PCBV3);           // for new V3 boards
  }

  SetBalancedMicInput(false);
}

/////////////////////////////////////////////////////////////////////
//
// Saturb/piHPSDR specific stuff
//
/////////////////////////////////////////////////////////////////////

// is there already a pihpsdr running and using xdma?
static bool is_already_running() {
  FILE *fp;
  char path[1035];
  fp = popen("lsof /dev/xdma0_user | grep pihpsdr", "r");

  if (fp == NULL) {
    t_print("Failed to run command in %s\n", __FUNCTION__ );
    exit(1);
  }

  while (fgets(path, sizeof(path), fp) != NULL) {}

  pclose(fp);
  return (strstr(path, "pihpsdr") == NULL) ? false : true;
}

#define SATURNPRODUCTID 1                       // Saturn, any version
#define SATURNGOLDENCONFIGID 3                  // "golden" configuration id
#define SATURNPRIMARYCONFIGID 4                 // "primary" configuration id
#define VADDRUSERVERSIONREG 0x4004              // user defined version register
#define VADDRSWVERSIONREG 0XC000                // user defined s/w version register
#define VADDRPRODVERSIONREG 0XC004              // user defined product version register

void saturn_discovery() {
  struct stat sb;

  if (devices < MAX_DEVICES && stat("/dev/xdma0_user", &sb) == 0 && S_ISCHR(sb.st_mode)) {
    uint8_t *mac = discovered[devices].network.mac_address;
    bool goodConfig = true;
    bool incompatible = true;
    char buf[256];
    bool running = is_already_running();

    if (OpenXDMADriver() == 0) {
      return;
    }

    //
    // Check version numbers etc. If they do not match,
    // Be aware that the XDMA may not be connected to a known Saturn board,
    // in this case close the XDMA driver and return without discovering.
    //
    // This means, do not call saturn_register_init() before you know you have
    // a Saturn board.
    //
    // All version info is contained in two 32-bit string SI and PI, to be read
    // from two FPGA registers. The layout is
    //
    //     FPGA_MajorVersion  SW[31:25], only valid if MinorVersion >= 18
    //     FPGA_SWID          SW[24:20]
    //     FPGA_MinorVersion  SW[19:4]
    //     FPGA_ClockInfo     SW[3:0]
    //     FPGA_ProdID        PR[31:16]
    //     Saturn_PCB_Version PR[15:0]
    //
    uint32_t SoftwareInformation = RegisterRead(VADDRSWVERSIONREG);
    uint32_t ProductInformation  = RegisterRead(VADDRPRODVERSIONREG);
    uint32_t UserVersion         = RegisterRead(VADDRUSERVERSIONREG);
    uint32_t FPGA_ClockInfo      = (SoftwareInformation      ) & 0xF;      // 4 clock bits
    uint32_t FPGA_MajorVersion   = (SoftwareInformation >> 25) & 0x7F;     //  7 bit major sw version
    uint32_t FPGA_SWID           = (SoftwareInformation >> 20) & 0x1F;     //  5 bit software ID
    uint32_t FPGA_ProdID         = (ProductInformation  >> 16) & 0xFFFF;   // 16 bit product ID

    //
    // These two are global
    //
    FPGA_MinorVersion   = (SoftwareInformation >>  4) & 0xFFFF;            // 16 bit minor sw version
    Saturn_PCB_Version  = (ProductInformation & 0xFFFF);                   // 16 bit board ID

    //
    // Initially, MajorVersions did not exist
    //
    if (FPGA_MinorVersion < 18) {
      FPGA_MajorVersion = 0;
    }

    if (FPGA_ProdID != SATURNPRODUCTID) {
      t_print("SATURN ProdID does not match\n");
      goodConfig = false;
    }

    if (FPGA_SWID != SATURNGOLDENCONFIGID && FPGA_SWID != SATURNPRIMARYCONFIGID) {
      t_print("SATURN SWID does not match\n");
      goodConfig = false;
    }

    if (FPGA_ClockInfo != 0xF) {
      t_print("SATURN clocks missing\n");
      goodConfig = false;  // not all clocks are present
    }

    if (!goodConfig) {
      //
      // This may indicate that the XDMA driver is not connected to a known Saturn board
      //
      CloseXDMADriver();
      return;
    }

    //
    // Now we know we have a Saturn board, so we can init it, and shall report back
    // a discovery result, which contains one of
    //
    // STATE_AVAILABLE         Saturn board ready to be used
    // STATE_SENDING           Saturn board already in use by another instance of piHPSDR
    // STATE_INCOMPATIBLE      Saturn FPGA version not supported
    //

    saturn_register_init();
    discovered[devices].status = (running) ? STATE_SENDING : STATE_AVAILABLE;

    if (FPGA_MajorVersion == 0 && FPGA_MinorVersion >= FIRMWARE_MIN_MINOR && FPGA_MinorVersion <= FIRMWARE_MAX_MINOR) {
      incompatible = false;
    }

    if (FPGA_MajorVersion >= FIRMWARE_MIN_MAJOR && FPGA_MajorVersion <=  FIRMWARE_MAX_MAJOR) {
      incompatible = false;
    }

    if (incompatible) {
      t_print("Incompatible Saturn FPGA firmware version (%ud,%ud), need (%d...%d, %d...%d) \n",
              FPGA_MajorVersion,
              FPGA_MinorVersion,
              FIRMWARE_MIN_MAJOR,
              FIRMWARE_MAX_MAJOR,
              FIRMWARE_MIN_MINOR,
              FIRMWARE_MAX_MINOR
             );
      discovered[devices].status = STATE_INCOMPATIBLE;
    }

    discovered[devices].protocol = NEW_PROTOCOL;
    discovered[devices].device = NEW_DEVICE_SATURN;
    discovered[devices].software_version = FPGA_MinorVersion;
    discovered[devices].fpga_version = UserVersion;
    snprintf(discovered[devices].name, sizeof(discovered[devices].name), "saturn");
    discovered[devices].frequency_min = 0.0;
    discovered[devices].frequency_max = 61440000.0;
    //
    // Try to obtain the hardware MAC address of the local eth0.
    // This is for diagnostic purposes only, so if it fails,
    // just use 00:00:00:00::00.
    //
    // One possible reason for failure is that the ethernet adapter is
    // not present or has a name different from eth0.
    // The interface name reported upstream is "XDMA" anyway.
    //
    memset(buf, 0, 256);
    FILE *fp = fopen("/sys/class/net/eth0/address", "rt");

    if (fp) {
      if (fgets(buf, sizeof buf, fp) != NULL) {
        sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0],
               &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
      }

      fclose(fp);
    } else {
      for (int i = 0; i < 6; i++) {
        discovered[devices].network.mac_address[i] = 0;
      }
    }

    discovered[devices].network.address_length = 0;
    discovered[devices].network.interface_length = 0;
    snprintf(discovered[devices].network.interface_name, sizeof(discovered[devices].network.interface_name),
             "XDMA");
    discovered[devices].use_tcp = 0;
    discovered[devices].use_routing = 0;
    discovered[devices].supported_receivers = 2;
    t_print("discovery: found saturn device min=%0.3f MHz max=%0.3f MHz\n",
            discovered[devices].frequency_min * 1E-6,
            discovered[devices].frequency_max * 1E-6);
    devices++;
  }
}

/////////////////////////////////////////////////////////////////////
//
// from P2_app/InDUCIQ.c, start of the DUC IQ listener thread
//
/////////////////////////////////////////////////////////////////////

#define VMEMDUCWORDSPERFRAME 180                       // memory writes per UDP frame
#define VBYTESPERSAMPLE 6                                                       // 24 bit + 24 bit samples
#define VDMADUCBUFFERSIZE 32768                                            // memory buffer to reserve
#define VDMADUCTRANSFERSIZE 1440                       // write 1 message at a time

static int DMADUCWritefile_fd = -1;               // DMA read file device
static unsigned char* DUCIQBasePtr;               // ptr to DMA location in I/Q memory

//
// Code from P2_app/InDUCIQ.c, IncomingDUCIQ() initialisation
//
static void saturn_init_duc_iq() {
  //
  // variables for DMA buffer
  //
  uint8_t* IQWriteBuffer = NULL;              // data for DMA to write to DUC
  uint32_t IQBufferSize = VDMADUCBUFFERSIZE;
  t_print("%s: Initializing DUC I/Q data\n", __FUNCTION__);
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&IQWriteBuffer, VALIGNMENT, IQBufferSize);

  if (!IQWriteBuffer) {
    t_print("%s: I/Q TX write buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  DUCIQBasePtr = IQWriteBuffer + VBASE;
  memset(IQWriteBuffer, 0, IQBufferSize);
  //
  // open DMA device driver
  // there is at least one XDMA device driver around which *needs* write-only here
  //
  DMADUCWritefile_fd = open(VDUCDMADEVICE, O_WRONLY);

  if (DMADUCWritefile_fd < 0) {
    t_print("%s: XDMA write device open failed for TX I/Q data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // setup hardware
  //
  EnableDUCMux(false);                                  // disable temporarily
  SetTXIQDeinterleaved(false);                          // not interleaved (at least for now!)
  ResetDUCMux();                                        // reset 64 to 48 mux
  ResetDMAStreamFIFO(eTXDUCDMA);
  SetupFIFOMonitorChannel(eTXDUCDMA, false);
  EnableDUCMux(true);                                   // enable operation
}

//
// Code from P2_app/InDUCIQ.c, IncomingDUCIQ() receive loop
//
void saturn_handle_duc_iq(const unsigned char *UDPInBuffer) {

  const uint8_t* SrcPtr;                                  // pointer to data from Host
  uint8_t* DestPtr;                                       // pointer to DMA buffer data
  uint32_t DepthDUC = 0;
  unsigned int Current;                                   // current occupied locations in FIFO
  bool FIFODUCOverflow, FIFODUCUnderflow, FIFODUCOverThreshold;

  DepthDUC = ReadFIFOMonitorChannel(eTXDUCDMA, &FIFODUCOverflow, &FIFODUCOverThreshold, &FIFODUCUnderflow,
                                    &Current);  // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

  if (FIFODUCOverThreshold) {
    t_print("TX DUC FIFO Overthreshold, depth now = %d\n", Current);
  }

  if (FIFODUCUnderflow) {
    t_print("TX DUC FIFO Underflowed, depth now = %d\n", Current);
  }

#endif

  //
  // Do 'busy waiting' until there is space in the FIFO
  //
  while (DepthDUC < VMEMDUCWORDSPERFRAME) {
    usleep(500);                                    // 0.5ms wait
    DepthDUC = ReadFIFOMonitorChannel(eTXDUCDMA, &FIFODUCOverflow, &FIFODUCOverThreshold, &FIFODUCUnderflow,
                                      &Current); // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

    if (FIFODUCOverThreshold) {
      t_print("TX DUC FIFO Overthreshold, depth now = %d\n", Current);
    }

    if (FIFODUCUnderflow) {
      t_print("TX DUC FIFO Underflowed, depth now = %d\n", Current);
    }

#endif
  }

  //
  // Now copy DUC IQ data to DMA.
  //
  SrcPtr = (UDPInBuffer + 4);
  DestPtr = DUCIQBasePtr;

  //
  // We must swap I/Q, so for all 6-byte IQ samples
  // copy byte 3,4,5,0,1,2 in that order
  //
  for (int Cntr = 0; Cntr < VIQDUCSAMPLESPERFRAME; Cntr++) {
    *DestPtr++ = *(SrcPtr + 3);
    *DestPtr++ = *(SrcPtr + 4);
    *DestPtr++ = *(SrcPtr + 5);
    *DestPtr++ = *(SrcPtr + 0);
    *DestPtr++ = *(SrcPtr + 1);
    *DestPtr++ = *(SrcPtr + 2);
    SrcPtr += 6;
  }

  //
  // Write IQ data to DMA
  //
  DMAWriteToFPGA(DMADUCWritefile_fd, DUCIQBasePtr, VDMADUCTRANSFERSIZE, VADDRDUCSTREAMWRITE);
  return;
}

static int DMASpkWritefile_fd = -1;
static unsigned char* SpkBasePtr;

//
// Code from P2_app/InSpkrAudio.c, IncomingSpkrAudio() initialisation
//
static void saturn_init_speaker_audio() {
  //
  // variables for DMA buffer
  //
  uint8_t* SpkWriteBuffer = NULL;             // data for DMA to write to spkr
  uint32_t SpkBufferSize = VDMASPKBUFFERSIZE;
  t_print("%s\n", __FUNCTION__);
  //
  // setup DMA buffer
  //
  posix_memalign((void**)&SpkWriteBuffer, VALIGNMENT, SpkBufferSize);

  if (!SpkWriteBuffer) {
    t_print("%s: spkr write buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  SpkBasePtr = SpkWriteBuffer + VBASE;
  memset(SpkWriteBuffer, 0, SpkBufferSize);
  //
  // open DMA device driver
  // there is at least one XDMA device driver around which *needs* write-only here
  //
  DMASpkWritefile_fd = open(VSPKDMADEVICE, O_WRONLY);

  if (DMASpkWritefile_fd < 0) {
    t_print("%s: XDMA write device open failed for spk data\n", __FUNCTION__);
    exit(-1);
  }

  ResetDMAStreamFIFO(eSpkCodecDMA);
  SetupFIFOMonitorChannel(eSpkCodecDMA, false);
  return;
}

//
// Code from P2_app/InSpkrAudio.c, IncomingSpkrAudio(), receive loop
//
void saturn_handle_speaker_audio(const unsigned char *UDPInBuffer) {
  bool FIFOSpkOverflow, FIFOSpkUnderflow, FIFOSpkOverThreshold;;
  uint32_t DepthSpk = 0;
  unsigned int Current;     // current occupied locations in FIFO
  DepthSpk = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOSpkOverflow, &FIFOSpkOverThreshold, &FIFOSpkUnderflow,
                                    &Current);  // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

  if (FIFOSpkOverThreshold) {
    t_print("Codec speaker FIFO Overthreshold, depth now = %d\n", Current);
  }

  if (FIFOSpkUnderflow) {
    t_print("Codec Speaker FIFO Underflowed, depth now = %d\n", Current);
  }

#endif

  //
  // do 'busy wait' until there is enough space available
  //
  while (DepthSpk < VMEMWORDSPERFRAME) {
    usleep(1000);
    DepthSpk = ReadFIFOMonitorChannel(eSpkCodecDMA, &FIFOSpkOverflow, &FIFOSpkOverThreshold, &FIFOSpkUnderflow,
                                      &Current); // read the FIFO free locations
#ifdef DISPLAY_OVER_UNDER_FLOWS

    if (FIFOSpkOverThreshold) {
      t_print("Codec speaker FIFO Overthreshold, depth now = %d\n", Current);
    }

    if (FIFOSpkUnderflow) {
      t_print("Codec Speaker FIFO Underflowed, depth now = %d\n", Current);
    }

#endif
  }

  //
  // Copy data to DMA transfer buffer and write to FPGA
  //
  memcpy(SpkBasePtr, UDPInBuffer + 4, VDMASPKTRANSFERSIZE);
  DMAWriteToFPGA(DMASpkWritefile_fd, SpkBasePtr, VDMASPKTRANSFERSIZE, VADDRSPKRSTREAMWRITE);
  return;
}

void saturn_exit() {
  //
  // This is called when pihpsdr exits.
  //
  t_print("%s: Exiting\n", __FUNCTION__);
  Exiting = true;
  SDRActive = false;
  SetMOX(false);
  SetTXEnable(false);
  EnableCW(false, false);
  ServerActive = false;
  CloseXDMADriver();
}

//
// Periodically send HighPrio data. No need to send it to the client
// since this is RX only.
//
static gpointer saturn_high_priority_thread(gpointer arg) {
  uint8_t  Byte;                                  // data being encoded
  uint16_t Word;                                  // data being encoded
  uint8_t  ADCOverflows = 0;                      // set to non-zero if ADC overflows detected

  while (!Exiting) {
    uint32_t SequenceCounter = 0;                       // sequence count for pihpsdr

    while (!SDRActive) {
      usleep(10000);
    }

    //
    // this is the main loop. SDR is running. transfer data;
    // also check for changes to DDC enabled, and DDC interleaved
    //
    // potential race conditions: thread execution order is underfined.
    // when a DDC becomes enabled, its paired DDC may not know yet and may still be set to interleaved.
    // when a DDC is set to interleaved, the paired DDC may not have been disabled yet.
    //
    while (SDRActive) {                            // main loop
      uint16_t SleepCount;                                      // counter for sending next message
      uint8_t PTTBits;                                          // PTT bits - and change means a new message needed
      mybuffer *mybuf = get_my_buffer(HPMYBUF);
      ReadStatusRegister();
      PTTBits = GetP2PTTKeyInputs() & 0xFF;
      mybuf->buffer[4] = PTTBits;
      ADCOverflows |= (GetADCOverflow() & 0xFF);
      mybuf->buffer[5] = ADCOverflows;
      ADCOverflows = 0;                                                                // clear it once reported
      Byte = GetUserIOBits() & 0xFF;                                                   // user I/O bits
      mybuf->buffer[59] = Byte;
      Word = GetAnalogueIn(4) & 0xFFFF;                                               // exciter power
      mybuf->buffer[6] = (Word >> 8) & 0xFF;
      mybuf->buffer[7] = (Word     ) & 0xFF;
      Word = GetAnalogueIn(0) & 0xFFFF;                                               // forward power
      mybuf->buffer[14] = (Word >> 8) & 0xFF;
      mybuf->buffer[15] = (Word     ) & 0xFF;
      Word = GetAnalogueIn(1) & 0xFFFF;                                               // reverse power
      mybuf->buffer[22] = (Word >> 8) & 0xFF;
      mybuf->buffer[23] = (Word     ) & 0xFF;
      Word = GetAnalogueIn(5) & 0xFFFF;                                               // supply voltage
      mybuf->buffer[49] = (Word >> 8) & 0xFF;
      mybuf->buffer[50] = (Word     ) & 0xFF;
      Word = GetAnalogueIn(2) & 0xFFFF;                                               // User ADC0
      mybuf->buffer[57] = (Word >> 8) & 0xFF;
      mybuf->buffer[58] = (Word     ) & 0xFF;
      Word = GetAnalogueIn(3) & 0xFFFF;                                               // User ADC1
      mybuf->buffer[55] = (Word >> 8) & 0xFF;
      mybuf->buffer[56] = (Word     ) & 0xFF;

      mybuf->buffer[0] = (SequenceCounter >> 24) & 0xFF;                            // add seq. count
      mybuf->buffer[1] = (SequenceCounter >> 16) & 0xFF;
      mybuf->buffer[2] = (SequenceCounter >>  8) & 0xFF;
      mybuf->buffer[3] = (SequenceCounter      ) & 0xFF;
      SequenceCounter++;
      saturn_post_high_priority(mybuf);

      //
      // now we need to sleep for 1ms (in TX) or 200ms (not in TX)
      // - if any of the PTT or key inputs change, send a message immediately
      // - if a new ADC overload is detected, send after 50 ms at the latest
      //
      SleepCount = HaveMOX ? 2 : 400;

      while (SleepCount-- > 0) {
        ReadStatusRegister();

        if ((GetP2PTTKeyInputs() & 0xFF) != PTTBits) {
          break;
        }

        //
        // Note GetADCOverflow() *clears* the ADC overflow latch
        // in the FPGA. During TX, just report ADC status every
        // msec in the message that is sent anyway. During RX,
        // take care that a HighPrio packet is sent "soon" after
        // a new ADC overflow has been detected.
        //
        ADCOverflows |= (GetADCOverflow() & 0xFF);

        if (ADCOverflows != 0 && SleepCount > 100)  {
          // We come here during RX only
          SleepCount = 100;
        }

        if (HaveMOX && SleepCount > 1) {
          // RXTX transition while "sleeping"
          SleepCount = 1;
        }

        usleep(500);
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

//
// Periodically send MicSamples. Although the client is RX only,
// send zero samples to the client in the pace of incoming
// samples from XDMA, since this may be used as a
// heart beat or clock source.
//
static gpointer saturn_micaudio_thread(gpointer arg) {
  //
  // variables for DMA buffer
  //
  uint8_t* MicReadBuffer = NULL;              // data for DMA read from DDC
  uint32_t MicBufferSize = VDMAMICBUFFERSIZE;
  unsigned char* MicBasePtr;                // ptr to DMA location in mic memory
  uint32_t Depth;
  int DMAReadfile_fd = -1;                  // DMA read file device
  uint32_t RegisterValue;
  bool FIFOOverflow, FIFOUnderflow, FIFOOverThreshold;
  unsigned int Current;                     // current occupied locations in FIFO
  uint8_t UDPBuffer[VMICPACKETSIZE];
  int Error;
  //
  // variables for outgoing UDP frame
  //
  struct sockaddr_in DestAddr;
  struct iovec iovecinst;
  struct msghdr datagram;

  //
  // The UDP buffer is cleared. Only the sequence number will be
  // updated each time a packet is sent via ethernet
  //
  memset(UDPBuffer, 0, sizeof(UDPBuffer));

  //
  // setup DMA buffer
  //
  posix_memalign((void**)&MicReadBuffer, VALIGNMENT, MicBufferSize);

  if (!MicReadBuffer) {
    t_print("%s: mic read buffer allocation failed\n", __FUNCTION__);
    exit(-1);
  }

  MicBasePtr = MicReadBuffer + VBASE;
  memset(MicReadBuffer, 0, MicBufferSize);
  //
  // open DMA device driver
  // there is at least one XDMA device driver around which *needs* read-only here
  //
  DMAReadfile_fd = open(VMICDMADEVICE, O_RDONLY);

  if (DMAReadfile_fd < 0) {
    t_print("%s: XDMA read device open failed for mic data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // now initialise Saturn hardware.
  // clear FIFO
  // then read depth
  //
  SetupFIFOMonitorChannel(eMicCodecDMA, false);
  ResetDMAStreamFIFO(eMicCodecDMA);
  RegisterValue = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                         &Current);  // read the FIFO Depth register
  t_print("%s: mic FIFO Depth register = %08x (should be ~0)\n", __FUNCTION__, RegisterValue);

  //
  // planned strategy: just DMA mic data when available; don't copy and DMA a larger amount.
  // if sufficient FIFO data available: DMA that data and transfer it out.
  // if it turns out to be too inefficient, we'll have to try larger DMA.
  //
  while (!Exiting) {
    uint32_t SequenceCounter = 0;
    uint32_t SequenceCounter2 = 0;

    while (!SDRActive) {
      usleep(10000);
    }

    memcpy(&DestAddr, &server_reply_addr, sizeof(struct sockaddr_in)); // make local copy of dest. addr.
    memset(&iovecinst, 0, sizeof(struct iovec));
    memset(&datagram, 0, sizeof(struct msghdr));
    iovecinst.iov_len = VMICPACKETSIZE;
    datagram.msg_iov = &iovecinst;
    datagram.msg_iovlen = 1;
    datagram.msg_name = &DestAddr;                   // MAC addr & port to send to
    datagram.msg_namelen = sizeof(DestAddr);
    t_print("starting %s\n", __FUNCTION__);

    while (SDRActive) {
      //
      // now wait until there is data, then DMA it
      //
      Depth = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                     &Current);  // read the FIFO Depth register. 4 mic words per 64 bit word.
#ifdef DISPLAY_OVER_UNDER_FLOWS

      if (FIFOOverThreshold) {
        t_print("Codec Mic FIFO Overthreshold, depth now = %d\n", Current);
      }

      // note this would often generate a message because we deliberately read it down to zero.
      // this isn't a problem as we can send the data on without the code becoming blocked.
      //if(FIFOUnderflow)
      //  t_print("Codec Mic FIFO Underflowed, depth now = %d\n", Depth);
#endif

      while (Depth < (VMICSAMPLESPERFRAME / 4)) {         // 16 locations = 64 samples
        usleep(1000);                       // 1ms wait
        Depth = ReadFIFOMonitorChannel(eMicCodecDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                       &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

        if (FIFOOverThreshold) {
          t_print("Codec Mic FIFO Overthreshold, depth now = %d\n", Current);
        }

        // note this would often generate a message because we deliberately read it down to zero.
        // this isn't a problem as we can send the data on without the code becoming blocked.
        //if(FIFOUnderflow)
        //  t_print("Codec Mic FIFO Underflowed, depth now = %d\n", Depth);
#endif
      }

      DMAReadFromFPGA(DMAReadfile_fd, MicBasePtr, VDMAMICTRANSFERSIZE, VADDRMICSTREAMREAD);
      // create the packet
      mybuffer *mybuf = get_my_buffer(MICMYBUF);
      mybuf->buffer[0] = (SequenceCounter >> 24) & 0xFF;           // add seq. count
      mybuf->buffer[1] = (SequenceCounter >> 16) & 0xFF;
      mybuf->buffer[2] = (SequenceCounter >>  8) & 0xFF;
      mybuf->buffer[3] = (SequenceCounter      ) & 0xFF;
      SequenceCounter++;

      memcpy(mybuf->buffer + 4, MicBasePtr, VDMAMICTRANSFERSIZE);  // copy in mic samples
      saturn_post_micaudio(VMICPACKETSIZE, mybuf);

      if (ServerActive) {
        iovecinst.iov_base = UDPBuffer;
        memcpy(&DestAddr, &server_reply_addr, sizeof(struct sockaddr_in)); // make local copy of dest. addr.
        UDPBuffer[0] = (SequenceCounter2 >> 24) & 0xFF;           // add seq. count
        UDPBuffer[1] = (SequenceCounter2 >> 16) & 0xFF;
        UDPBuffer[2] = (SequenceCounter2 >>  8) & 0xFF;
        UDPBuffer[3] = (SequenceCounter2      ) & 0xFF;
        SequenceCounter2++;
        Error = sendmsg(SocketData[VPORTMICAUDIO].Socketid, &datagram, 0);

        if (Error == -1) {
          t_perror("sendmsg, Mic Audio");
          exit(-1);
        }
      } else {
        SequenceCounter2 = 0;
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

static gpointer saturn_rx_thread(gpointer arg) {
  t_print( "%s\n", __FUNCTION__);
  //
  // memory buffers
  //
  uint32_t DMATransferSize;
  uint32_t ResidueBytes;
  uint32_t Depth;
  int IQReadfile_fd = -1;                     // DMA read file device
  uint32_t RegisterValue;
  bool FIFOOverflow, FIFOUnderflow, FIFOOverThreshold;
  int Error;
  //
  // variables for outgoing UDP frame
  //
  struct sockaddr_in DestAddr[VNUMDDC];
  struct iovec iovecinst[VNUMDDC];                            // instance of iovec
  struct msghdr datagram[VNUMDDC];
  uint32_t SequenceCounter[VNUMDDC];                          // UDP sequence count
  //
  // variables for analysing a DDC frame
  //
  uint32_t FrameLength = 0;                                   // number of words per frame
  uint32_t DDCCounts[VNUMDDC];                                // number of samples per DDC in a frame
  uint32_t RateWord = 0;                                      // DDC rate word from buffer
  uint32_t HdrWord;                                           // check word read form DMA's data
  const uint16_t* SrcWordPtr;
  uint16_t *DestWordPtr;                                      // 16 bit read & write pointers
  uint32_t PrevRateWord;                                      // last used rate word
  bool HeaderFound;
  uint32_t DecodeByteCount;                                   // bytes to decode
  unsigned int Current;                                       // current occupied locations in FIFO
  //
  // initialise. Create memory buffers and open DMA file devices
  //
  PrevRateWord = 0xFFFFFFFF;                                  // illegal value to forc re-calculation of rates
  DMATransferSize = VDMATRANSFERSIZE;                         // initial size, but can be changed

  if (CreateDynamicMemory()) {
    t_print("%s: CreateDynamicMemory Failed\n", __FUNCTION__);
    exit(-1);
  }

  //
  // open DMA device driver
  // there is at least one XDMA device driver around which *needs* read-only here
  //
  IQReadfile_fd = open(VDDCDMADEVICE, O_RDONLY);

  if (IQReadfile_fd < 0) {
    t_print("%s: XDMA read device open failed for DDC data\n", __FUNCTION__);
    exit(-1);
  }

  //
  // now initialise Saturn hardware.
  //
  SetRXDDCEnabled(false);
  usleep(1000);                           // give FIFO time to stop recording
  SetupFIFOMonitorChannel(eRXDDCDMA, false);
  ResetDMAStreamFIFO(eRXDDCDMA);
  RegisterValue = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                         &Current); // read the FIFO Depth register
  t_print("%s: DDC FIFO Depth register = %08x (should be ~0)\n", __FUNCTION__, RegisterValue);
  SetByteSwapping(true);                                            // h/w to generate network byte order
  //
  // thread loop. runs continuously until commanded by main loop to exit
  // for now: add 1 RX data + mic data at 48KHz sample rate. Mic data is constant zero.
  // while there is enough I/Q data, make outgoing packets;
  // when not enough data, read more.
  //
  //
  // enable Saturn DDC to transfer data
  //
  t_print("%s: enable data transfer\n", __FUNCTION__);
  SetRXDDCEnabled(true);
  HeaderFound = false;

  while (!Exiting) {
    while (!SDRActive) {
      usleep(10000);
    }

    for (int DDC = 0; DDC < VNUMDDC; DDC++) {
      SequenceCounter[DDC] = 0;
      memcpy(&DestAddr[DDC], &server_reply_addr, sizeof(struct sockaddr_in)); // make local copy of dest. addr.
      memset(&iovecinst[DDC], 0, sizeof(struct iovec));
      memset(&datagram[DDC], 0, sizeof(struct msghdr));
      iovecinst[DDC].iov_len = VDDCPACKETSIZE;
      datagram[DDC].msg_iov = &iovecinst[DDC];
      datagram[DDC].msg_iovlen = 1;
      datagram[DDC].msg_name = &DestAddr[DDC];                   // MAC addr & port to send to
      datagram[DDC].msg_namelen = sizeof(DestAddr);
    }

    t_print("starting %s\n", __FUNCTION__);

    while (SDRActive) {
      //
      // loop through all DDC I/Q buffers.
      // while there is enough I/Q data for this DDC in local (ARM) memory, make DDC Packets
      // then put any residues at the heads of the buffer, ready for new data to come in
      //
      for (int DDC = 0; DDC < VNUMDDC; DDC++) {
        //
        // Ship out DDC packets as long as there is enough data
        //
        while ((IQHeadPtr[DDC] - IQReadPtr[DDC]) > VIQBYTESPERFRAME) {
          mybuffer *mybuf = get_my_buffer(DDCMYBUF);
          mybuf->buffer[0] = (SequenceCounter[DDC] >> 24) & 0xFF;        // add seq. count
          mybuf->buffer[1] = (SequenceCounter[DDC] >> 16) & 0xFF;
          mybuf->buffer[2] = (SequenceCounter[DDC] >>  8) & 0xFF;
          mybuf->buffer[3] = (SequenceCounter[DDC]      ) & 0xFF;
          SequenceCounter[DDC]++;
          memset(mybuf->buffer + 4, 0, 8);                               // clear the timestamp data
          mybuf->buffer[12] = 0;                                         // bits per sample set to 24
          mybuf->buffer[13] = 24;                                        // bits per sample set to 24
          mybuf->buffer[14] = (VIQSAMPLESPERFRAME >> 8) & 0xFF;          // IQ samples per frame
          mybuf->buffer[15] = (VIQSAMPLESPERFRAME     ) & 0xFF;          // IQ samples per frame
          //
          // now add I/Q data & post outgoing packet
          //
          memcpy(mybuf->buffer + 16, IQReadPtr[DDC], VIQBYTESPERFRAME);
          IQReadPtr[DDC] += VIQBYTESPERFRAME;

          if (DDC < 6) {
            if (ServerActive) {
              iovecinst[DDC].iov_base = mybuf->buffer;
              memcpy(&DestAddr[DDC], &server_reply_addr, sizeof(struct sockaddr_in)); // make local copy of dest. addr.
              Error = sendmsg(SocketData[VPORTDDCIQ0 + DDC].Socketid, &datagram[DDC], 0);

              if (Error == -1) {
                t_print("Send Error, DDC=%d, errno=%d, socket id = %d\n", DDC,
                        errno, SocketData[VPORTDDCIQ0 + DDC].Socketid);
                exit(-1);
              }
            } else {
              SequenceCounter[DDC] = 0;
            }

            mybuf->free = 1;
          } else {
            saturn_post_iq_data(DDC - 6, mybuf);
          }
        }

        //
        // now copy any residue to the start of the buffer (before the data copy in point)
        // unless the buffer already starts at or below the base
        // if we do a copy, the 1st free location is always base addr
        //
        ResidueBytes = IQHeadPtr[DDC] - IQReadPtr[DDC];

        //    t_print("Residue = %d bytes\n",ResidueBytes);
        if (IQReadPtr[DDC] > IQBasePtr[DDC]) {                              // move data down
          if (ResidueBytes != 0) {  // if there is residue to move
            memcpy(IQBasePtr[DDC] - ResidueBytes, IQReadPtr[DDC], ResidueBytes);
            IQReadPtr[DDC] = IQBasePtr[DDC] - ResidueBytes;
          } else {
            IQReadPtr[DDC] = IQBasePtr[DDC];
          }

          IQHeadPtr[DDC] = IQBasePtr[DDC];                            // ready for new data at base
        }
      }

      //
      // Packet sending complete for all DDCs: there are no DDC buffers with enough data to send out.
      // bring in more data by DMA if there is some, else sleep for a while and try again
      // we have the same issue with DMA: a transfer isn't exactly aligned to the amount we can read out
      // according to the DDC settings. So we either need to have the part-used DDC transfer variables
      // persistent across DMAs, or we need to recognise an incomplete fragment of a frame as such
      // and copy it like we do with IQ data so the next readout begins at a new frame
      // the latter approach seems easier!
      //
      Depth = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                     &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

      if (FIFOOverThreshold) {
        t_print("RX DDC FIFO Overthreshold, depth now = %d\n", Current);
      }

      // note this could often generate a message at low sample rate because we deliberately read it down to zero.
      // this isn't a problem as we can send the data on without the code becoming blocked. so not a useful trap.
      //if(FIFOUnderflow)
      //  t_print("RX DDC FIFO Underflowed, depth now = %d\n", Current);
      //  t_print("read: depth = %d\n", Depth);
#endif

      //    t_print("read: depth = %d\n", Depth);
      while (Depth < (DMATransferSize / 8U)) { // 8 bytes per location
        usleep(500);               // 1ms wait
        Depth = ReadFIFOMonitorChannel(eRXDDCDMA, &FIFOOverflow, &FIFOOverThreshold, &FIFOUnderflow,
                                       &Current);  // read the FIFO Depth register
#ifdef DISPLAY_OVER_UNDER_FLOWS

        if (FIFOOverThreshold) {
          t_print("RX DDC FIFO Overthreshold, depth now = %d\n", Current);
        }

        // note this could often generate a message at low sample rate because we deliberately read it down to zero.
        // this isn't a problem as we can send the data on without the code becoming blocked. so not a useful trap.
        //if(FIFOUnderflow)
        //  t_print("RX DDC FIFO Underflowed, depth now = %d\n", Current);
        //  t_print("read: depth = %d\n", Depth);
#endif
      }

      if (Depth > 4096) {
        DMATransferSize = 32768;
      } else if (Depth > 2048) {
        DMATransferSize = 16384;
      } else if (Depth > 1024) {
        DMATransferSize = 8192;
      } else {
        DMATransferSize = 4096;
      }

      DMAReadFromFPGA(IQReadfile_fd, DMAHeadPtr, DMATransferSize, VADDRDDCSTREAMREAD);
      DMAHeadPtr += DMATransferSize;

      //
      // find header: may not be the 1st word
      //
      if (HeaderFound == false)                                                   // 1st time: look for header
        for (int Cntr = 16; Cntr < (DMAHeadPtr - DMAReadPtr); Cntr += 8) {           // search for rate word; ignoring 1st
          if (*(DMAReadPtr + Cntr + 7) == 0x80) {
            //                        t_print("found header at offset=%x\n", Cntr);
            HeaderFound = true;
            DMAReadPtr += Cntr;                                             // point read buffer where header is
            break;
          }
        }

      if (HeaderFound == false) {                                      // if rate flag not set
        t_print("%s: Rate word not found when expected. rate= %08x\n", __FUNCTION__, RateWord);
        exit(1);
      }

      //
      // finally copy data to DMA buffers according to the embedded DDC rate words
      // the 1st word is pointed by DMAReadPtr and it should point to a DDC rate word
      // search for it if not!
      // (it should always be left in that state).
      // the top half of the 1st 64 bit word should be 0x8000
      // and that is located in the 2nd 32 bit location.
      // assume that DMA is > 1 frame.
      //            t_print("headptr = %x readptr = %x\n", DMAHeadPtr, DMAReadPtr);
      DecodeByteCount = DMAHeadPtr - DMAReadPtr;

      //
      // Note CAST-ALIGN:
      // - DMAReadPtr is initialised on a 4092-byte boundary and then moves in multiples of 8 bytes,
      //   therefore always 64-bit aligned
      // - IQHeadPtr is initialised with malloc() and thus aligned, and moves in multiples of 2 bytes,
      //   threfore always 16-bit aligned
      //
      // Three lines in the following block will be flagged by the compiler with strict alignment
      // check (clang: -Wcast-align, gcc: -Wcast-align=strict) but these three warnings can be
      // ignored.
      //
      //
      while (DecodeByteCount >= 16) {                     // minimum size to try!
        if (*(DMAReadPtr + 7) != 0x80) {
          t_print("%s: header not found for rate word at addr %p\n", __FUNCTION__, DMAReadPtr);
          exit(1);
        } else {                                                                          // analyse word, then process
          // cppcheck-suppress constVariablePointer
          uint32_t *LongWordPtr = (uint32_t*)DMAReadPtr;                                  // get 32 bit ptr (CAST OK)
          RateWord = *LongWordPtr;                                                        // read rate word

          if (RateWord != PrevRateWord) {
            FrameLength = AnalyseDDCHeader(RateWord, &DDCCounts[0]);                      // read new settings
            //                        t_print("new framelength = %d\n", FrameLength);
            PrevRateWord = RateWord;                                                      // so so we know its analysed
          }

          if (DecodeByteCount >= ((FrameLength + 1) * 8)) {                               // if bytes for header & frame
            //THEN COPY DMA DATA TO I / Q BUFFERS
            DMAReadPtr += 8;                                                              // point to 1st location past rate word
            SrcWordPtr = (uint16_t*)DMAReadPtr;                                           // 16-bit chunk (CAST OK)

            for (int DDC = 0; DDC < VNUMDDC; DDC++) {
              HdrWord = DDCCounts[DDC];                                                   // number of words for this DDC. reuse variable

              if (HdrWord != 0) {
                DestWordPtr = (uint16_t *)IQHeadPtr[DDC];                                 // (CAST OK)

                for (unsigned int Cntr = 0; Cntr < HdrWord; Cntr++) {                     // count 64 bit words
                  *DestWordPtr++ = *SrcWordPtr++;                                         // move 48 bits of sample data
                  *DestWordPtr++ = *SrcWordPtr++;
                  *DestWordPtr++ = *SrcWordPtr++;
                  SrcWordPtr++;                                                           // and skip 16 bits where theres no data
                }

                IQHeadPtr[DDC] += 6 * HdrWord;                                            // 6 bytes per sample
              }

              // read N samples; write at head ptr
            }

            DMAReadPtr += FrameLength * 8;                                                // that's how many bytes we read out
            DecodeByteCount -= (FrameLength + 1) * 8;
          } else {
            break;  // if not enough left, exit loop
          }
        }
      }

      //
      // now copy any residue to the start of the buffer (before the data copy in point)
      // unless the buffer already starts at or below the base
      // if we do a copy, the 1st free location is always base addr
      //
      ResidueBytes = DMAHeadPtr - DMAReadPtr;

      //    t_print("Residue = %d bytes\n",ResidueBytes);
      if (DMAReadPtr > DMABasePtr) {                              // move data down
        if (ResidueBytes != 0) {  // if there is residue to move
          memcpy(DMABasePtr - ResidueBytes, DMAReadPtr, ResidueBytes);
          DMAReadPtr = DMABasePtr - ResidueBytes;
        } else {
          DMAReadPtr = DMABasePtr;
        }

        DMAHeadPtr = DMABasePtr;                            // ready for new data at base
      }
    }
  }

  t_print("ending: %s\n", __FUNCTION__);
  return NULL;
}

void saturn_init() {
  //
  // Called from new_protocol.c, when the P2 Saturn radio
  // is initialised
  //
  saturn_init_speaker_audio();
  saturn_init_duc_iq();
  saturn_rx_thread_id = g_thread_new( "SATURN RX", saturn_rx_thread, NULL);
  saturn_micaudio_thread_id = g_thread_new( "SATURN MIC", saturn_micaudio_thread, NULL);
  saturn_high_priority_thread_id = g_thread_new( "SATURN HP OUT", saturn_high_priority_thread, NULL);
}

void saturn_handle_high_priority_server(const unsigned char *UDPInBuffer) {
  //
  // HighPrio Packet that came via ethernet:
  // only handle start/stop and DDC frequencies
  // map DDC0:5 to DDC0:5
  //
  for (int i = 0; i < 6; i++) {
    uint32_t LongWord = ((UDPInBuffer[4 * i +  9] & 0xFF) << 24) |
                        ((UDPInBuffer[4 * i + 10] & 0xFF) << 16) |
                        ((UDPInBuffer[4 * i + 11] & 0xFF) <<  8) |
                        ((UDPInBuffer[4 * i + 12] & 0xFF)      );
    SetDDCFrequency(i, LongWord, true);
  }

  int RunBit = UDPInBuffer[4] & 0x01;

  if (RunBit) {
    StartBitReceived = true;

    if (ReplyAddressSet) {
      ServerActive = true;  // only set active if we have replay address too
    }
  } else {
    ServerActive = false;

    // disable DDC0:6
    for (int i = 0; i < 6; i++) {
      SetP2SampleRate(i, false, 48, false);
    }

    WriteP2DDCRateRegister();
    t_print("Server set to inactive by client app\n");
    StartBitReceived = false;
  }
}

void saturn_handle_high_priority(const unsigned char *UDPInBuffer) {
  //
  // HighPrio Packet that came via XDMA
  //
  bool RunBit;                                          // true if "run" bit set
  uint8_t Byte, Byte2;                                  // received dat being decoded
  uint32_t LongWord;
  uint16_t Word;

  //
  // map DDC0:3 in packet to DDC6:9
  //
  for (int i = 0; i < 4; i++) {
    LongWord = ((UDPInBuffer[4 * i +  9] & 0xFF) << 24) |
               ((UDPInBuffer[4 * i + 10] & 0xFF) << 16) |
               ((UDPInBuffer[4 * i + 11] & 0xFF) <<  8) |
               ((UDPInBuffer[4 * i + 12] & 0xFF)      );
    SetDDCFrequency(i + 6, LongWord, true);
  }

  Byte = UDPInBuffer[4] & 0xFF;
  RunBit  = (bool)(Byte & 0x01);
  HaveMOX = (bool)(Byte & 0x02);

  if (RunBit) {
    SDRActive = true;
    SetTXEnable(true);
  } else {
    SDRActive = false;
    SetTXEnable(false);
    HaveMOX = false;
    SetMOX(false);
    EnableCW(false, false);
  }

  SetMOX(HaveMOX);
  //
  // DUC frequency & drive level
  //
  LongWord = ((UDPInBuffer[329] & 0xFF) << 24) |
             ((UDPInBuffer[330] & 0xFF) << 16) |
             ((UDPInBuffer[331] & 0xFF) <<  8) |
             ((UDPInBuffer[332] & 0xFF)      );
  SetDUCFrequency(LongWord, true);

  Byte = UDPInBuffer[345] & 0xFF;
  SetTXDriveLevel(Byte);

  //
  // Byte 1398:1399 (CAT port) not used
  //

  // transverter, speaker mute, open collector, user outputs
  // open collector data is in bits 7:1; move to 6:0
  //
  Byte = UDPInBuffer[1400] & 0xFF;
  bool XvtEn = Byte & 0x01;
  bool SpkMt = Byte & 0x02;

  SetXvtrEnable(XvtEn);
  SetSpkrMute(SpkMt);

  // According to  P2, the seven OC bits are b1:7
  Byte = (UDPInBuffer[1401] >> 1) & 0x7F;
  SetOpenCollectorOutputs(Byte);

  //
  // Alex behaviour needs to be FPGA version specific: at V12, separate register added for Alex TX antennas
  // - if new FPGA version: we write the word with TX ANT (byte 1428) to a new register,
  //   and the "old" word to original register
  // - if we don't have a new TX ant bit set, just write "old" word data (byte 1432) to both registers
  //   this is to allow safe operation with legacy client apps
  //
  // This is not part of the protocol, but adds to safety:
  // - if AlexTX bit27 is not set, disable PA
  //
  Byte =  UDPInBuffer[1428] & 0x07;                      // Alex0[26:24] TX data: ANT1/2/3 (if zero: old host program)
  bool PAEnable;

  if ((FPGA_MinorVersion >= 12) && (Byte != 0)) {             // if new firmware && client app supports it
    //t_print("new FPGA code, new client data\n");
    Word = ((UDPInBuffer[1428] & 0xFF) << 8 ) | (UDPInBuffer[1429] & 0xFF);  // "Backup" Alex0 TX settings
    PAEnable = Word & 0x0800;  // bit11 in Word, bit27 in AlexWord
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, true);
    Word = ((UDPInBuffer[1432] & 0xFF) << 8 ) | (UDPInBuffer[1433] & 0xFF); // "Current" Alex0 TX settings
    AlexManualTXFilters(Word, false);
  } else if (FPGA_MinorVersion >= 12) {                       // new hardware but no client app support
    //t_print("new FPGA code, new client data\n");
    Word = ((UDPInBuffer[1432] & 0xFF) << 8 ) | (UDPInBuffer[1433] & 0xFF);  // "Current" Alex0 TX settings
    PAEnable = Word & 0x0800;
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, true);
    AlexManualTXFilters(Word, false);
  } else {                                              // old FPGA hardware
    //t_print("old FPGA code\n");
    Word = ((UDPInBuffer[1432] & 0xFF) << 8 ) | (UDPInBuffer[1433] & 0xFF);  // "Current" Alex0 TX settings
    PAEnable = Word & 0x0800;
    //t_print("new FPGA code, legacy client data, PA enable = %d\n", (int)PAEnable);
    AlexManualTXFilters(Word, false);
  }

  SetPAEnabled(PAEnable); // activate PA if client app wants it

  //
  // RX filters
  //
  Word = ((UDPInBuffer[1430] & 0xFF) << 8 ) | (UDPInBuffer[1431] & 0xFF);  // Alex1 RX settings
  AlexManualRXFilters(Word, 2);
  Word = ((UDPInBuffer[1434] & 0xFF) << 8 ) | (UDPInBuffer[1435] & 0xFF);  // Alex0 RX settings
  AlexManualRXFilters(Word, 0);

  //
  // RX atten during TX and RX
  //
  Byte  = UDPInBuffer[1442] & 0x1F;      // ADC1 atten
  Byte2=  UDPInBuffer[1443] & 0x1F;      // ADC0 atten
  SetADCAttenuator(Byte2, true, false, Byte, true, false);

  //
  // CWX bits - currently not used by piHPSDR
  //
  //Byte = UDPInBuffer[5] & 0xFF;
  //bool CWXEnabled = (bool) (Byte & 0x01);
  //bool CWXDot     = (bool) (Byte & 0x02);
  //bool CWXDash    = (bool) (Byte & 0x04);
  //SetCWXBits(CWXEnabled, CWXDot, CWXDash);

  return;
}

void saturn_handle_general_packet(const unsigned char *PacketBuffer) {
  uint8_t Byte;

  //
  // ALEX is enabled by default, so *only* the "PA enable" bit is processed.
  //
  Byte = PacketBuffer[58] & 0xFF;                     // b0: PA-enable
  bool PaEn = Byte & 0x01;
  SetPAEnabled(PaEn);
  return;
}

void saturn_handle_ddc_specific_server(const unsigned char *UDPInBuffer) {
  //
  // Handle DDC-specific packet from the Ethernet. DO NOT apply any
  // changes to the ADCs, but apply ADC mappings for DDC0:6
  //
  //
  // main settings for each DDC
  // reuse "dither" for interleaved with next;
  // reuse "random" for DDC enabled.
  // be aware an interleaved "odd" DDC will usually be set to disabled, and we need to revert this!
  //
  uint16_t Word = ((UDPInBuffer[8] & 0xFF) << 8) | (UDPInBuffer[7] & 0xFF); // DDC enabled[15:0] (low byte first!)

  for (int i = 0; i < 6; i++) {
    bool Enabled, Interleaved;                        // DDC settings
    Enabled = (bool)(Word & 1);                       // get enable state
    uint8_t Byte1 = UDPInBuffer[6 * i + 17] & 0xFF;           // ADC for this DDC
    uint16_t Word2 = ((UDPInBuffer[6 * i + 18] & 0xFF) << 8) | (UDPInBuffer[6 * i + 19] & 0xFF); // sample rate
    EADCSelect ADC = eADC1;                               // ADC to use for a DDC

    if (Byte1 == 0) {
      ADC = eADC1;
    } else if (Byte1 == 1) {
      ADC = eADC2;
    } else if (Byte1 == 2) {
      ADC = eTXSamples;
    }

    SetDDCADC(i, ADC);
    Interleaved = false;                                 // assume no synch

    //
    // Synchronised DDC pairs:
    // If DDC0/1 are synchronized, set "Interleaved" for DDC0 and "Enabled" for DDC1
    // If DDC2/3 are synchronized, set "Interleaved" for DDC2 and "Enabled" for DDC3
    // If DDC4/5 are synchronized, set "Interleaved" for DDC4 and "Enabled" for DDC5
    //

    switch (i) {
    case 0:
      Byte1 = UDPInBuffer[1363] & 0xFF;  // DDC0 synch

      if (Byte1 == 0b00000010) {
        Interleaved = true;  // set interleave
      }

      break;
    case 1:
      Byte1 = UDPInBuffer[1363] & 0xFF; // DDC0 synch

      if (Byte1 == 0b00000010) {                        // if synch to DDC1
        Enabled = true;  // enable DDC1
      }

      break;

    case 2:
      Byte1 = UDPInBuffer[1365] & 0xFF; // DDC2 synch

      if (Byte1 == 0b00001000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 3:
      Byte1 = UDPInBuffer[1365] & 0xFF; // DDC2 synch

      if (Byte1 == 0b00001000) {                        // if synch to DDC3
        Enabled = true;  // enable DDC3
      }

      break;

    case 4:
      Byte1 = UDPInBuffer[1367] & 0xFF; // DDC4 synch

      if (Byte1 == 0b00100000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 5:
      Byte1 = UDPInBuffer[1367] & 0xFF; // DDC4 synch

      if (Byte1 == 0b00100000) {                        // if synch to DDC5
        Enabled = true;  // enable DDC5
      }

      break;
    }

    SetP2SampleRate(i, Enabled, Word2, Interleaved);
    Word = Word >> 1;                                 // move onto next DDC enabled bit
  }

  WriteP2DDCRateRegister();
  return;
}

void saturn_handle_ddc_specific(const unsigned char *UDPInBuffer) {
  //
  // Handle DDC-specific packet from XDMA
  //
  uint8_t Byte1, Byte2;                                 // received data

  bool Dither1, Random1;                                // ADC1 bits
  bool Dither2, Random2;                                // ADC1 bits
  Byte1 = UDPInBuffer[5] & 0xFF;                        // Dither bits
  Byte2 = UDPInBuffer[6] & 0xFF;                        // Random bits
  Dither1  = (bool)(Byte1 & 1);
  Random1  = (bool)(Byte2 & 1);
  Dither2  = (bool)(Byte1 & 2);
  Random2  = (bool)(Byte2 & 2);
  SetADCOptions(false, Dither1, Random1, false, Dither2, Random2);

  //
  // main settings for each DDC
  // reuse "dither" for interleaved with next;
  // reuse "random" for DDC enabled.
  // be aware an interleaved "odd" DDC will usually be set to disabled, and we need to revert this!
  //
  uint16_t Word = ((UDPInBuffer[8] & 0xFF) << 8) | (UDPInBuffer[7] & 0xFF); // DDC enabled[15:0] (low byte first!)

  for (int i = 0; i < 4; i++) {
    //
    // ATTENTION: DDC0:3 from packet is mapped to DDC6:9 for XDMA
    //
    EADCSelect ADC = eADC1;                            // ADC to use for a DDC
    uint16_t Word2;                                   // 16 bit read value
    bool Enabled, Interleaved;                        // DDC settings
    Enabled = (bool)(Word & 1);                       // get enable state
    Byte1 = UDPInBuffer[6 * i + 17] & 0xFF;           // ADC for this DDC
    Word2 = ((UDPInBuffer[6 * i + 18] & 0xFF) << 8) | (UDPInBuffer[6 * i + 19] & 0xFF); // sample rate

    if (Byte1 == 0) {
      ADC = eADC1;
    } else if (Byte1 == 1) {
      ADC = eADC2;
    } else if (Byte1 == 2) {
      ADC = eTXSamples;
    }

    SetDDCADC(i + 6, ADC);
    Interleaved = false;                                 // assume no synch

    //
    // Synchronised DDC pairs:
    // If DDC0/1 are synchronized, set "Interleaved" for DDC0 and "Enabled" for DDC1
    // If DDC2/3 are synchronized, set "Interleaved" for DDC0 and "Enabled" for DDC1
    //
    switch (i) {
    case 0:
      Byte1 = UDPInBuffer[1363] & 0xFF;  // DDC0 synch

      if (Byte1 == 0b00000010) {
        Interleaved = true;  // set interleave
      }

      break;

    case 1:
      Byte1 = UDPInBuffer[1363] & 0xFF; // DDC0 synch

      if (Byte1 == 0b00000010) {                        // if synch to DDC1
        Enabled = true;  // enable DDC1
      }

      break;

    case 2:
      Byte1 = UDPInBuffer[1365] & 0xFF; // DDC2 synch

      if (Byte1 == 0b00001000) {
        Interleaved = true;  // set interleave
      }

      break;

    case 3:
      Byte1 = UDPInBuffer[1365] & 0xFF; // DDC2 synch

      if (Byte1 == 0b00001000) {                        // if synch to DDC3
        Enabled = true;  // enable DDC3
      }

      break;
    }

    SetP2SampleRate(i + 6, Enabled, Word2, Interleaved);
    Word = Word >> 1;
  }

  WriteP2DDCRateRegister();
  return;
}

void saturn_handle_duc_specific(const unsigned char *UDPInBuffer) {
  uint8_t Byte1, Byte2;

  //
  // CW settings
  //
  Byte1 = UDPInBuffer[5] & 0xFF;  // keyer bool bits (bit0 EER not used)
  bool CWEnabled       = Byte1 & 0x02;
  bool ReverseKeys     = Byte1 & 0x04;
  bool CWIambic        = Byte1 & 0x08;
  bool CWSideEnabled   = Byte1 & 0x10;
  bool IambicModeB     = Byte1 & 0x20;
  bool CWStrictSpacing = Byte1 & 0x40;
  bool CWBreakIn       = Byte1 & 0x80;

  uint8_t SideToneVolume = UDPInBuffer[6] & 0xFF;
  uint16_t SideToneFreq  = ((UDPInBuffer[7] & 0xFF) << 8) | (UDPInBuffer[8] & 0xFF);
  uint8_t IambicSpeed = UDPInBuffer[9] & 0xFF;
  uint8_t IambicWeight = UDPInBuffer[10] & 0xFF;
  uint16_t CWHangTime    = ((UDPInBuffer[11] & 0xFF) << 8) | (UDPInBuffer[12] & 0xFF);
  uint8_t CWRFDelay      = UDPInBuffer[13] & 0xFF;
  uint8_t CWRampTime     = UDPInBuffer[17] & 0xFF;

  // This goes to VADDRIAMBICCONFIG
  SetCWIambicKeyer(IambicSpeed, IambicWeight, ReverseKeys, IambicModeB, CWStrictSpacing, CWIambic, CWBreakIn);

  // This goes to VADDRSIDETONECONFIGREG
  SetCWSideTone(CWSideEnabled, SideToneVolume, SideToneFreq);

  // This sets the modulation source (VADDRTXCONFIGREG)
  // and activates the keyer (VADDRKEYERCONFIGREG)
  EnableCW(CWEnabled, CWBreakIn);

  // This goes to VADDRKEYERCONFIGREG
  SetKeyerParams(CWRFDelay, CWHangTime, CWRampTime);

  //
  // Codec Input and Orion Mic options
  //
  bool LineIn;
  bool MicBoost;
  bool OrionMicPTT;
  bool OrionBiasRing;
  bool OrionBiasEnable;
  bool SaturnXLRInput;
  int  LineInGain;

  Byte1           = UDPInBuffer[50] & 0xFF;
  LineIn          = Byte1 & 0x01;
  MicBoost        = Byte1 & 0x02;
  OrionMicPTT     = ~Byte1 & 0x04;  // zero bit means PTT enabled
  OrionBiasRing   = Byte1 & 0x08;
  OrionBiasEnable = Byte1 & 0x10;
  SaturnXLRInput  = Byte1 & 0x20;
  LineInGain      = UDPInBuffer[51] & 0x1F; // restrict to 0-31

  SetCodecInputParams(LineIn, MicBoost, LineInGain);
  SetOrionMicOptions(OrionBiasRing, OrionBiasEnable, OrionMicPTT);
  SetBalancedMicInput(SaturnXLRInput);

  //
  // RF Attenuator values during transmit
  //
  Byte1 = UDPInBuffer[58] & 0x1F;                   // ADC1 att on TX
  Byte2 = UDPInBuffer[59] & 0x1F;                   // ADC0 att on TX
  SetADCAttenuator(Byte2, false, true, Byte1, false, true);
  return;
}
