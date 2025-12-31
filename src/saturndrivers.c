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
// this application uses C code to emulate HPSDR protocol 1
//
// Contribution of interfacing to PiHPSDR from N1GP (Rick Koch)
//
// saturndrivers.c:
// Drivers for minor IP cores
//
//////////////////////////////////////////////////////////////

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 500
#include <stdlib.h>                     // for function min()
#include <math.h>
#include <semaphore.h>
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "message.h"
#include "saturndrivers.h"

//
// Global "version" variables, determined upon startup
//
uint32_t FPGA_MinorVersion;
uint32_t Saturn_PCB_Version;

////////////////////////////////////////////////////////////////////////////////
//
// - open/close connection to the XDMA device driver for register and DMA access
// - DMA read and write operations from/to FPGA
// - register read and write
// - monitoring FIFOs
// - Analyse DDC headers
//
////////////////////////////////////////////////////////////////////////////////
static int register_fd = -1;                     // device identifier, -1 means "not open"

int OpenXDMADriver(void) {
  int Result = 0;

  //
  // If hitting "discover" several times on the startup screen,
  // XDMA is opened repeatedly, so close it first
  //
  if (register_fd >= 0) { close(register_fd); }
  //
  // Note this fd is used for both pread() and pwrite() so use read-write mode
  //
  if ((register_fd = open("/dev/xdma0_user", O_RDWR)) == -1) {
    t_print("register R/W address space not available\n");
  } else {
    t_print("register access connected to /dev/xdma0_user\n");
    Result = 1;
  }

  return Result;
}

//
// close connection to the XDMA device driver for register and DMA access
//
void CloseXDMADriver(void) {
  if (register_fd >= 0) {
    close(register_fd);
    register_fd = -1;
  }
}

//
// initiate a DMA to the FPGA with specified parameters
// returns 0 if success, else -EIO
// fd: file device (an open file)
// SrcData: pointer to memory block to transfer
// Length: number of bytes to copy
// AXIAddr: offset address in the FPGA window
//
int DMAWriteToFPGA(int fd, unsigned char*SrcData, uint32_t Length, uint32_t AXIAddr) {
  ssize_t rc;                 // response code
  // write data to FPGA from memory buffer
  rc = pwrite(fd, SrcData, Length, (off_t) AXIAddr);

  if (rc < 0) {
    t_print("write 0x%x @ 0x%lx failed %ld.\n", (int)Length, (long)AXIAddr, (long)rc);
    t_perror("DMA write");
    return -EIO;
  }

  return 0;
}

//
// initiate a DMA from the FPGA with specified parameters
// returns 0 if success, else -EIO
// fd: file device (an open file)
// DestData: pointer to memory block to transfer
// Length: number of bytes to copy
// AXIAddr: offset address in the FPGA window
//
int DMAReadFromFPGA(int fd, unsigned char*DestData, uint32_t Length, uint32_t AXIAddr) {
  ssize_t rc;                 // response code
  // read data from FPGA to memory buffer
  rc = pread(fd, DestData, Length, (off_t) AXIAddr);

  if (rc < 0) {
    t_print("read 0x%x @ 0x%lX failed %ld.\n", (int)Length, (long)AXIAddr, (long)rc);
    t_perror("DMA read");
    return -EIO;
  }

  return 0;
}

//
// 32 bit register read over the AXILite bus
//
uint32_t RegisterRead(uint32_t Address) {
  uint32_t result = 0;

  if (register_fd < 0) {
    return result;
  }

  if (pread(register_fd, &result, sizeof(result), (off_t) Address) != sizeof(result)) {
    t_print("ERROR: register read: addr=0x%lX   error=%s\n", (long)Address, strerror(errno));
  }

  return result;
}

//
// 32 bit register write over the AXILite bus
//
void RegisterWrite(uint32_t Address, uint32_t Data) {
  if (register_fd < 0) {
    return;
  }

  if (pwrite(register_fd, &Data, sizeof(Data), (off_t) Address) != sizeof(Data)) {
    t_print("ERROR: Write: addr=0x%lX   error=%s\n", (long)Address, strerror(errno));
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// from common/saturnregisters.c
//
////////////////////////////////////////////////////////////////////////////////

//
// DMA FIFO depths
// this is the number of *64 bit* FIFO locations
// this is now version dependent, and updated by InitialiseFIFOSizes()
//
static uint32_t DMAFIFODepths[VNUMDMAFIFO] = {
  8192,             //  eRXDDCDMA,    selects RX
  1024,             //  eTXDUCDMA,    selects TX
  256,              //  eMicCodecDMA, selects mic samples
  256               //  eSpkCodecDMA  selects speaker samples
};

////////////////////////////////////////////////////////////////////////////////
//
// from common/saturndrivers.c
//
////////////////////////////////////////////////////////////////////////////////

//
// void SetupFIFOMonitorChannel(EDMAStreamSelect Channel, bool EnableInterrupt);
//
// Setup a single FIFO monitor channel.
//   Channel:     IP channel number (enum)
//   EnableInterrupt: true if interrupt generation enabled for overflows
// modified 28/9/2023 to remove "write FIFO": FPGA now detects overflow AND underflow
//
void SetupFIFOMonitorChannel(EDMAStreamSelect Channel, bool EnableInterrupt) {
  uint32_t Address;             // register address
  uint32_t Data;                // register content
  static bool GFIFOSizesInitialised = false;

  if (!GFIFOSizesInitialised) {
    if (FPGA_MinorVersion < 10) {
      t_print("loading new FIFO sizes for 0.x firmware\n");
      DMAFIFODepths[0] = 8192;        //  eRXDDCDMA (RX)
      DMAFIFODepths[1] = 1024;        //  eTXDUCDMA (TX)
      DMAFIFODepths[2] = 256;         //  eMicCodecDMA (Mic)
      DMAFIFODepths[3] = 256;         //  eSpkCodecDMA (Headphone)
    } else if (FPGA_MinorVersion <= 12) {
      t_print("loading new FIFO sizes for 1.0, 1.1, 1.2 firmware\n");
      DMAFIFODepths[0] = 16384;       //  eRXDDCDMA (RX)
      DMAFIFODepths[1] = 2048;        //  eTXDUCDMA (TX)
      DMAFIFODepths[2] = 256;         //  eMicCodecDMA (Mic)
      DMAFIFODepths[3] = 1024;        //  eSpkCodecDMA (Headphone)
    } else {
      t_print("loading new FIFO sizes for firmware version 1.3 and newer\n");
      DMAFIFODepths[0] = 16384;       //  eRXDDCDMA (RX)
      DMAFIFODepths[1] = 4096;        //  eTXDUCDMA (TX)
      DMAFIFODepths[2] = 256;         //  eMicCodecDMA (Mic)
      DMAFIFODepths[3] = 1024;        //  eSpkCodecDMA (Headphone)
    }

    GFIFOSizesInitialised = true;
  }

  Address = VADDRFIFOMONBASE + 4 * Channel + 0x10;      // config register address
  Data = DMAFIFODepths[(int)Channel];                   // memory depth

  if (EnableInterrupt) {
    Data += 0x80000000;  // bit 31
  }

  RegisterWrite(Address, Data);
}

//
// uint32_t ReadFIFOMonitorChannel(EDMAStreamSelect Channel, bool* Overflowed);
//
// Read number of locations in a FIFO
// for a read FIFO: returns the number of occupied locations available to read
// for a write FIFO: returns the number of free locations available to write
//   Channel:     IP core channel number (enum)
//   Overflowed:    true if an overflow has occurred. Reading clears the overflow bit.
//   OverThreshold:   true if overflow occurred  measures by threshold. Cleared by read.
//   Underflowed:       true if underflow has occurred. Cleared by read.
//   Current:           number of locations occupied (in either FIFO type)
//
uint32_t ReadFIFOMonitorChannel(EDMAStreamSelect Channel, bool* Overflowed, bool* OverThreshold, bool* Underflowed,
                                unsigned int* Current) {
  uint32_t Address;             // register address
  uint32_t Data = 0;            // register content
  bool Overflow = false;
  bool OverThresh = false;
  bool Underflow = false;
  Address = VADDRFIFOMONBASE + 4 * (uint32_t)Channel;     // status register address
  Data = RegisterRead(Address);

  if (Data & 0x80000000) {                  // if top bit set, declare overflow
    Overflow = true;
  }

  if (Data & 0x40000000) {                  // if bit 30 set, declare over threshold
    OverThresh = true;
  }

  if (Data & 0x20000000) {                  // if bit 29 set, declare underflow
    Underflow = true;
  }

  Data = Data & 0xFFFF;                   // strip to 16 bits
  *Current = Data;
  *Overflowed = Overflow;                 // send out overflow result
  *OverThreshold = OverThresh;            // send out over threshold result
  *Underflowed = Underflow;               // send out underflow result

  //
  // If it is a "write" channel, return number of free locations instead
  // of the currentfilling
  //
  if ((Channel == eTXDUCDMA) || (Channel == eSpkCodecDMA)) {
    Data = DMAFIFODepths[Channel] - Data;
  }

  return Data;                        // return 16 bit FIFO count
}

//
// reset a stream FIFO
//
void ResetDMAStreamFIFO(EDMAStreamSelect DDCNum) {
  uint32_t Data;                    // DDC register content
  uint32_t DataBit = 0;
  static pthread_mutex_t ResetMutex = PTHREAD_MUTEX_INITIALIZER;

  switch (DDCNum) {
  case eRXDDCDMA:             // selects RX
    DataBit = (1 << VBITDDCFIFORESET);
    break;

  case eTXDUCDMA:             // selects TX
    DataBit = (1 << VBITDUCFIFORESET);
    break;

  case eMicCodecDMA:            // selects mic samples
    DataBit = (1 << VBITCODECMICFIFORESET);
    break;

  case eSpkCodecDMA:            // selects speaker samples
    DataBit = (1 << VBITCODECSPKFIFORESET);
    break;
  }

  // The Mutex takes care we get a clean "data pulse"
  pthread_mutex_lock(&ResetMutex);
  Data = RegisterRead(VADDRFIFORESET);        // read current content
  Data = Data & ~DataBit;
  RegisterWrite(VADDRFIFORESET, Data);        // set reset bit to zero
  Data = Data | DataBit;
  RegisterWrite(VADDRFIFORESET, Data);        // set reset bit to 1
  pthread_mutex_unlock(&ResetMutex);
}

//
// number of samples to read for each DDC setting
// these settings must match behaviour of the FPGA IP!
// a value of "7" indicates an interleaved DDC
// and the rate value is stored for *next* DDC
//
static const uint32_t DDCSampleCounts[] = {
  0,            // set to zero so no samples transferred
  1,
  2,
  4,
  8,
  16,
  32,
  0           // when set to 7, use next value & double it
};

//
// uint32_t AnalyseDDCHeader(unit32_t Header, unit32_t* DDCCounts)
// parameters are the header read from the DDC stream, and
// a pointer to an array [DDC count] of ints
// the array of ints is populated with the number of samples to read for each DDC
// returns the number of words per frame, which helps set the DMA transfer size
// This limits the total number of DDCs to 10, since each DDC is described
// by 3 bits in the Header word.
//
uint32_t AnalyseDDCHeader(uint32_t Header, uint32_t* DDCCounts) {
  uint32_t DDC;               // DDC counter
  uint32_t Count;
  uint32_t Total = 0;

  for (DDC = 0; DDC < VNUMDDC; DDC++) {
    // 3 bit value for this DDC
    uint32_t Rate = Header & 7;

    if (Rate != 7) {
      Count = DDCSampleCounts[Rate];
      DDCCounts[DDC] = Count;
      Total += Count;
    } else {
      // This and the next DDC channel form a "synchronised pair"
      // which share the sample rate and where all samples are
      // delivered pair-wise in the DDC stream of the first member
      // of the pair.
      Header = Header >> 3;
      Rate = Header & 7;
      Count = 2 * DDCSampleCounts[Rate];
      DDCCounts[DDC] = Count;   // This one gets all the samples for the pair
      Total += Count;
      DDCCounts[DDC + 1] = 0;   // This one gets no samples
      DDC += 1;
    }

    Header = Header >> 3;         // ready for next DDC rate
  }

  return Total;
}
