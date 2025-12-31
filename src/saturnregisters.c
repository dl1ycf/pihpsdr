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
// saturnregisters.c:
// Hardware access to FPGA registers in the Saturn FPGA
//  at the level of "set TX frequency" or set DDC frequency"
//
//////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <semaphore.h>

#include "saturndrivers.h"
#include "saturnregisters.h"
#include "message.h"

//
// Mutexes to protect register updates:
// In the following list, you find at the end variable names. They are used
// to store the value of a FPGA register to suppress unnecessary DMA writes.
// Between reading and updating this variables we need exclusive access
// ensured by a mutex.
// The code always looks like this:
//
// pthread_mutex_lock(&Mutex);
// if (new_value != stored_value) {
//   RegisterWrite(register, new_value);
//   stored_value = new_value;
// }
// pthread_mutex_unlock(&Mutex);
//
//
static pthread_mutex_t CodecMutex     = PTHREAD_MUTEX_INITIALIZER;    // GCodecPath, GCodecGain
static pthread_mutex_t GPIOMutex      = PTHREAD_MUTEX_INITIALIZER;    // GPIORegValue
static pthread_mutex_t DDCInSelMutex  = PTHREAD_MUTEX_INITIALIZER;    // DDCInSelReg
static pthread_mutex_t TXConfigMutex  = PTHREAD_MUTEX_INITIALIZER;    // TXConfigRegValue
static pthread_mutex_t DDCRateMutex   = PTHREAD_MUTEX_INITIALIZER;    // DDCRateReg, GDDCEnable
static pthread_mutex_t IambicMutex    = PTHREAD_MUTEX_INITIALIZER;    // GIambicConfigReg
static pthread_mutex_t KeyerMutex     = PTHREAD_MUTEX_INITIALIZER;    // GCWKeyerSetup, GCWKeyerRampLength
static pthread_mutex_t DDCregMutex    = PTHREAD_MUTEX_INITIALIZER;    // DDCDeltaPhase[]
static pthread_mutex_t DUCregMutex    = PTHREAD_MUTEX_INITIALIZER;    // DUCDeltaPhase
static pthread_mutex_t AlexRXMutex    = PTHREAD_MUTEX_INITIALIZER;    // GAlexRXRegister
static pthread_mutex_t AlexTXMutex    = PTHREAD_MUTEX_INITIALIZER;    // GAlexTXAntRegister, GAlexTXFiltRegister
static pthread_mutex_t TXdriveMutex   = PTHREAD_MUTEX_INITIALIZER;    // GTXDACCtrl
static pthread_mutex_t SideToneMutex  = PTHREAD_MUTEX_INITIALIZER;    // GSideToneReg
static pthread_mutex_t AttenMutex     = PTHREAD_MUTEX_INITIALIZER;    // GRXADCCtrl

//
// 8 bit Codec register write over the AXILite bus via SPI
// using simple SPI writer IP
// given 7 bit register address and 9 bit data
//
// Note: "protection codec" is in the caller
//
static void CodecRegisterWrite(unsigned int Address, unsigned int Data) {
  uint32_t WriteData;
  WriteData = (Address << 9) | (Data & 0x01FF);
  RegisterWrite(VADDRCODECSPIWRITEREG, WriteData);
  usleep(5);
}

//
// ROMs for DAC Current Setting and 0.5dB step digital attenuator
//
static unsigned int DACCurrentROM[256];           // used for residual attenuation
static unsigned int DACStepAttenROM[256];         // provides most atten setting

//
// local copies of values written to registers
//
static uint32_t DDCDeltaPhase[VNUMDDC];           // DDC frequency settings                 (DDCregMutex)
static uint32_t DUCDeltaPhase;                    // DUC frequency setting                  (DUCregMutex)
static uint32_t GStatusRegister;                  // most recent status register setting    (no mutex necessary)
static uint32_t GPIORegValue;                     // value stored into GPIO                 (GPIOMutex)
static uint32_t TXConfigRegValue;                 // value written into TX config register  (TXConfigMutex)
static uint32_t DDCInSelReg;                      // value written into DDC config register (DDCInSelMutex)
static uint32_t DDCRateReg;                       // value written into DDC rate register   (DDCrateMutex)
static uint32_t GDDCEnabled;                      // 1 bit per DDC                          (DDCrateMutex)
static uint32_t GTXDACCtrl;                       // TX DAC current setting & atten         (TXdriveMutex)
static uint32_t GRXADCCtrl;                       // RX1 & 2 attenuations                   (AttenMutex)
static uint32_t GAlexTXFiltRegister;              // 16 bit used of 32                      (AlexTXMutex)
static uint32_t GAlexTXAntRegister;               // 16 bit used of 32                      (AlexTXMutex)
static uint32_t GAlexRXRegister;                  // 32 bit RX register                     (AlexRXMutex)
static uint32_t GIambicConfigReg;                 // copy of iambic comfig register         (IambicMutex)
static uint32_t GCWKeyerSetup;                    // keyer control register                 (KeyerMutex)
static uint32_t GSideToneReg;                     // side tone configuration                (SideToneMutex)
static bool GCWEnabled;                           // true if CW mode                        (no mutex necessary)
static bool GBreakinEnabled;                      // true if break-in is enabled            (no mutex necessary)
static unsigned int GCWKeyerRampLength = 0;       // ramp length for keyer, in samples      (KeyerMutex)

//
// local copies of Codec registers
//
static unsigned int GCodecGain;                   // Codec gain register                    (CodecMutex)
static unsigned int GCodecPath;                   // Codec path register                    (CodecMutex)


//
// Saturn PCB Version, needed for codec ID
// (PCB version 3 onwards will have a TLV320AIC3204)
static ECodecType InstalledCodec;                 // Codec type on the Saturn board

//
// addresses of the DDC frequency registers
//
static uint32_t DDCRegisters[VNUMDDC] = {
  VADDRDDC0REG,
  VADDRDDC1REG,
  VADDRDDC2REG,
  VADDRDDC3REG,
  VADDRDDC4REG,
  VADDRDDC5REG,
  VADDRDDC6REG,
  VADDRDDC7REG,
  VADDRDDC8REG,
  VADDRDDC9REG
};

//
// ALEX SPI registers
//
#define VOFFSETALEXTXFILTREG 0                    // offset addr in IP core: TX filt, RX ant
#define VOFFSETALEXRXREG 4                        // offset addr in IP core
#define VOFFSETALEXTXANTREG 8                     // offset addr in IP core: TX filt, TX ant

//
// GPIO output bits
//
#define VMICBIASENABLEBIT    0
#define VMICPTTSELECTBIT     1
#define VMICSIGNALSELECTBIT  2
#define VMICBIASSELECTBIT    3
#define VSPKRMUTEBIT         4
#define VBALANCEDMICSELECT   5
#define VADC1RANDBIT         8
#define VADC1PGABIT          9
#define VADC1DITHERBIT       10
#define VADC2RANDBIT         11
#define VADC2PGABIT          12
#define VADC2DITHERBIT       13
#define VOPENCOLLECTORBITS   16                   // bits 16-22
#define VMOXBIT              24
#define VTXENABLEBIT         25
#define VDATAENDIAN          26
#define VTXRELAYDISABLEBIT   27
#define VPURESIGNALENABLE    28                   // not used by this hardware
#define VATUTUNEBIT          29
#define VXVTRENABLEBIT       30

//
// GPIO input buts
//
#define VKEYINA              2                    // dot key
#define VKEYINB              3                    // dash key
#define VUSERIO4             4
#define VUSERIO5             5
#define VUSERIO6             6
#define VUSERIO8             7
#define V13_8VDETECTBIT      8
#define VATUTUNECOMPLETEBIT  9
#define VPLLLOCKED           10
#define VCWKEYDOWN           11                   // keyer output
#define VCWKEYPRESSED        12                   // keyer request for TX active

//
// Keyer setup register defines
//
#define VCWKEYERENABLE 31                         // enable bit
#define VCWKEYERDELAY 0                           // delay bits 7:0
#define VCWKEYERHANG 8                            // hang time is 17:8
#define VCWKEYERRAMP 18                           // ramp time
#define VRAMPSIZE 4096                            // max ramp length in words

//
// Iambic config register defines
//
#define VIAMBICSPEED 0                            // speed bits 7:0
#define VIAMBICWEIGHT 8                           // weight bits 15:8
#define VIAMBICREVERSED 16                        // keys reversed bit 16
#define VIAMBICENABLE 17                          // keyer enabled bit 17
#define VIAMBICMODE 18                            // mode bit 18
#define VIAMBICSTRICT 19                          // strict spacing bit 19
#define VIAMBICCWX 20                             // CWX enable bit 20
#define VIAMBICCWXDOT 21                          // CWX dox bit 21
#define VIAMBICCWXDASH 22                         // CWX dash bit 22
#define VCWBREAKIN 23                             // breakin bit (CW not Iambic strictly!)
#define VIAMBICCWXBITS 0x00700000                 // all CWX bits
#define VIAMBICBITS 0x000FFFFF                    // all non CWX bits

//
// TX config register defines
//

#define VTXCONFIGDATASOURCEBIT     0
#define VTXCONFIGSAMPLEGATINGBIT   2
#define VTXCONFIGPROTOCOLBIT       3
#define VTXCONFIGSCALEBIT          4
#define VTXCONFIGHPFENABLE         27
#define VTXCONFIGWATCHDOGOVERRIDE  28
#define VTXCONFIGMUXRESETBIT       29
#define VTXCONFIGIQDEINTERLEAVEBIT 30
#define VTXCONFIGIQSTREAMENABLED   31

////////////////////////////////////////////////////////////////////////////////////
//
// initialise the DAC Atten ROMs.
// The "drive level" of the HPSDR protocol has a voltage
// amplitude in mind, with values 0-255.
//
// The Saturn hardware controls its drive output through
// a step attenuator (0...63 --> 0.0...31.5 dB in  0.5 dB steps)
// and a fine-tuning through a "DACdrive" which is an amplitude
// 0..255.
// This way, most of the control goes to the Attenuator, while
// the DACdrive assumes values between 240 and 255, doing the
// interpolation between two adjacent 0.5-db-steps.
// Some data from the table, with a TX power that is 100 Watts
// at full scale:
//
// Level     Step   DACdrive  Watt
// --------------------------------
//    0       63        0       0
//   26       63      245       1
//   57       26      254       5
//   81       19      241      10
//  128       11      241      25
//  180       26      254      50
//  221        2      247      75
//  255        0      255     100
////////////////////////////////////////////////////////////////////////////////////

void InitialiseDACAttenROMs(void) {
  //
  // do the max atten values separately; then calculate point by point
  //
  DACCurrentROM[0] = 0;    // min level
  DACStepAttenROM[0] = 63; // max atten

  for (unsigned int Level = 1; Level < 256; Level++) {
    // this is the atten value we want after the high speed DAC
    double DesiredAtten = 20.0 * log10(255.0 / (double)Level);
    // convert to integer and clip to 6 bits
    unsigned int StepValue = (int)(2.0 * DesiredAtten);

    if (StepValue > 63) { StepValue = 63; }

    // atten to go in the current setting DAC
    // this needs to be achieved through the current setting drive
    double ResidualAtten = DesiredAtten - ((double)StepValue * 0.5);
    // convert to integer
    unsigned int DACDrive = (unsigned int)(255.0 / pow(10.0, (ResidualAtten / 20.0)));

    // write data pair to the ROM
    DACCurrentROM[Level] = DACDrive;
    DACStepAttenROM[Level] = StepValue;
  }
}

//
// SetByteSwapping(bool)
// set whether byte swapping is enabled. True if yes, to get data in network byte order.
//
void SetByteSwapping(bool IsSwapped) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;

  if (IsSwapped) {
    Register |= (1 << VDATAENDIAN);  // set bit for swapped to network order
  } else {
    Register &= ~(1 << VDATAENDIAN);  // clear bit for raspberry pi local order
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;
    RegisterWrite(VADDRRFGPIOREG, Register);
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// internal function to set the keyer on or off
// needed because keyer setting can change by message, of by TX operation
//
static void ActivateCWKeyer(bool Keyer) {
  pthread_mutex_lock(&KeyerMutex);
  uint32_t Register = GCWKeyerSetup;

  if (Keyer) {
    Register |= (1U<<VCWKEYERENABLE);
  } else {
    Register &= ~(1U<<VCWKEYERENABLE);
  }

  if (Register != GCWKeyerSetup) {
    GCWKeyerSetup = Register;
    RegisterWrite(VADDRKEYERCONFIGREG, Register);
  }

  pthread_mutex_unlock(&KeyerMutex);
}

//
// SetMOX(bool Mox)
// sets or clears TX state
// set or clear the relevant bit in GPIO
// and enable keyer if CW
//
void SetMOX(bool Mox) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;

  if (Mox) {
    Register |= (1 << VMOXBIT);
  } else {
    Register &= ~(1 << VMOXBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;
    RegisterWrite(VADDRRFGPIOREG, Register);
  }

  pthread_mutex_unlock(&GPIOMutex);

  //
  // now set CW keyer if required
  //
  if (Mox) {
    ActivateCWKeyer(GCWEnabled);
  } else {        // disable keyer unless CW & breakin
    ActivateCWKeyer(GCWEnabled && GBreakinEnabled);
  }
}

//
// SetTXEnable(bool Enabled)
// sets or clears TX enable bit
// set or clear the relevant bit in GPIO
//
void SetTXEnable(bool Enabled) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;

  if (Enabled) {
    Register |= (1 << VTXENABLEBIT);
  } else {
    Register &= ~(1 << VTXENABLEBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;
    RegisterWrite(VADDRRFGPIOREG, Register);
  }

  pthread_mutex_unlock(&GPIOMutex);
}

void SetP2SampleRate(unsigned int DDC, bool Enabled, unsigned int SampleRate, bool InterleaveWithNext) {
  uint32_t RegisterValue;
  uint32_t Mask;
  ESampleRate Rate;
  Mask = 7 << (DDC * 3);                      // 3 bits in correct position

  //
  // Although we do not (yet) write DDCRateReg to the FPGA, we need
  // mutex here to protect the  read-modify-write cycles for GDDCEnabled and DDCRateReg
  //
  pthread_mutex_lock(&DDCRateMutex);
  if (!Enabled) {                             // if not enabled, clear sample rate value & enabled flag
    GDDCEnabled &= ~(1 << DDC);               // clear enable bit
    Rate = eDisabled;
  } else {
    GDDCEnabled |= (1 << DDC);                // set enable bit

    if (InterleaveWithNext) {
      Rate = eInterleaveWithNext;
    } else {
      // look up enum value
      Rate = e48KHz;                          // assume 48KHz; then check other rates

      if (SampleRate == 96) {
        Rate = e96KHz;
      } else if (SampleRate == 192) {
        Rate = e192KHz;
      } else if (SampleRate == 384) {
        Rate = e384KHz;
      } else if (SampleRate == 768) {
        Rate = e768KHz;
      } else if (SampleRate == 1536) {
        Rate = e1536KHz;
      }
    }
  }

  RegisterValue = DDCRateReg;                 // get current register setting
  RegisterValue &= ~Mask;                     // strip current bits
  Mask = (uint32_t)Rate;                      // new bits
  Mask = Mask << (DDC * 3);                   // get new bits to right bit position
  RegisterValue |= Mask;
  DDCRateReg = RegisterValue;                 // don't save to hardware
  pthread_mutex_unlock(&DDCRateMutex);
}

//
// void WriteP2DDCRateRegister(void)
// writes the DDCRateRegister, once all settings have been made
// this is done so the number of changes to the DDC rates are minimised
// and the information all comes form one P2 message anyway.
//
void WriteP2DDCRateRegister(void) {
  static uint32_t OldValue = 0;
  pthread_mutex_lock(&DDCRateMutex);

  if (DDCRateReg != OldValue) {
    OldValue = DDCRateReg;
    RegisterWrite(VADDRDDCRATES, DDCRateReg);
  }

  pthread_mutex_unlock(&DDCRateMutex);
}

//
// uint32_t GetDDCEnabled(void)
// get enable bits for each DDC; 1 bit per DDC
// this is needed to set timings and sizes for DMA transfers
//
uint32_t GetDDCEnables(void) {
  return GDDCEnabled;
}

//
// SetOpenCollectorOutputs(unsigned int bits)
// sets the 7 open collector output bits
// data must be provided in bits 6:0
//
void SetOpenCollectorOutputs(unsigned int bits) {
  uint32_t Register;                              // FPGA register content
  uint32_t BitMask;                               // bitmask for 7 OC bits
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings
  BitMask = (0b1111111) << VOPENCOLLECTORBITS;
  Register = Register & ~BitMask;                 // strip old bits, add new
  Register |= (bits << VOPENCOLLECTORBITS);       // OC bits are in bits (6:0)

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                        // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetADCOptions(bool PGA1, bool Dither1, bool Random1, bool PGA2, bool Dither2, bool Random2);
// sets the ADC contol bits for both ADCs
//
void SetADCOptions(bool PGA1, bool Dither1, bool Random1, bool PGA2, bool Dither2, bool Random2) {
  uint32_t Register;                              // FPGA register content

  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings
  Register &= ~(1 << VADC1RANDBIT);               // strip old bits
  Register &= ~(1 << VADC1PGABIT);
  Register &= ~(1 << VADC1DITHERBIT);
  Register &= ~(1 << VADC2RANDBIT);               // strip old bits
  Register &= ~(1 << VADC2PGABIT);
  Register &= ~(1 << VADC2DITHERBIT);

  if (PGA1)    { Register |= (1 << VADC1PGABIT); }
  if (PGA2)    { Register |= (1 << VADC2PGABIT); }
  if (Dither1) { Register |= (1 << VADC1DITHERBIT); }
  if (Dither2) { Register |= (1 << VADC2DITHERBIT); }
  if (Random1) { Register |= (1 << VADC1RANDBIT); }
  if (Random2) { Register |= (1 << VADC2RANDBIT); }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                    // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);  // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetDDCFrequency(uint32_t DDC, uint32_t Value, bool IsDeltaPhase)
// sets a DDC frequency.
// DDC: DDC number (0-9)
// Value: 32 bit phase word or frequency word (1Hz resolution)
// IsDeltaPhase: true if a delta phase value, false if a frequency value (P1)
// calculate delta phase if required. Delta=2^32 * (F/Fs)
// store delta phase; write to FPGA register.
//
void SetDDCFrequency(uint32_t DDC, uint32_t Value, bool IsDeltaPhase) {
  uint32_t DeltaPhase;                    // calculated deltaphase value

  if (DDC >= VNUMDDC) {                   // limit the DDC count to actual regs!
    DDC = VNUMDDC - 1;
  }

  if (!IsDeltaPhase) {
    //
    // We never arrive here, since this conversion is done in new_protocol.c
    // the "obscure" constant 34.95233...  is 2^32 / 12288000 (sample rate)
    //
    DeltaPhase = (uint32_t)((double)Value * 34.952533333333333333333333333333);
  } else {
    DeltaPhase = Value;
  }

  //
  // only write back if changed
  //
  pthread_mutex_lock(&DDCregMutex);

  if (DDCDeltaPhase[DDC] != DeltaPhase) {
    DDCDeltaPhase[DDC] = DeltaPhase;
    uint32_t RegAddress = DDCRegisters[DDC];
    RegisterWrite(RegAddress, DeltaPhase);
  }

  pthread_mutex_unlock(&DDCregMutex);
}

#define DELTAPHIHPFCUTIN 1712674133L            // delta phi for 49MHz
//
// SetDUCFrequency(unsigned int Value, bool IsDeltaPhase)
// sets a DUC frequency. (Currently only 1 DUC, therefore DUC must be 0)
// Value: 32 bit phase word or frequency word (1Hz resolution)
// IsDeltaPhase: true if a delta phase value, false if a frequency value (P1)
//
void SetDUCFrequency(unsigned int Value, bool IsDeltaPhase) { // only accepts DUC=0
  uint32_t DeltaPhase;                    // calculated deltaphase value

  if (!IsDeltaPhase) {
    //
    // We never arrive here, since this conversion is done in new_protocol.c
    // the "obscure" constant 34.95233...  is 2^32 / 12288000 (sample rate)
    //
    DeltaPhase = (uint32_t)((double)Value * 34.952533333333333333333333333333);
  } else {
    DeltaPhase = (uint32_t)Value;
  }

  pthread_mutex_lock(&DUCregMutex);

  if (DeltaPhase != DUCDeltaPhase) {
    DUCDeltaPhase = DeltaPhase;             // store this delta phase
    RegisterWrite(VADDRTXDUCREG, DeltaPhase);  // and write to it
  }

  pthread_mutex_unlock(&DUCregMutex);

  //
  // PCB V3+: now enable high pass filter if above 49MHz
  //
  if(Saturn_PCB_Version >= 3) {
    bool NeedsHPF = false;

    if (DeltaPhase > DELTAPHIHPFCUTIN) { NeedsHPF = true; }

    pthread_mutex_lock(&TXConfigMutex);
    uint32_t Register = TXConfigRegValue;                       // get current settings
    Register &= ~(1<<VTXCONFIGHPFENABLE);                       // remove old HPF bit
    if(NeedsHPF) { Register |= (1 << VTXCONFIGHPFENABLE); }     // add new bit if HPF to be enabled

    if (Register != TXConfigRegValue) {
      TXConfigRegValue = Register;                                // store it back
      RegisterWrite(VADDRTXCONFIGREG, Register);                  // and write to it
    }

    pthread_mutex_unlock(&TXConfigMutex);
  }
}

//////////////////////////////////////////////////////////////////////////////////
//
// Alex layout is relevant for P1 only. P2 does this is new-protocol.c
// We keep the list here just for information
//
//////////////////////////////////////////////////////////////////////////////////
//  data to send to Alex Tx filters is in the following format:
//  Bit  0 - NC               U3 - D0       0
//  Bit  1 - NC               U3 - D1       0
//  Bit  2 - txrx_status      U3 - D2       TXRX_Relay strobe
//  Bit  3 - Yellow Led       U3 - D3       RX2_GROUND: from C0=0x24: C1[7]
//  Bit  4 - 30/20m LPF       U3 - D4       LPF[0] : from C0=0x12: C4[0]
//  Bit  5 - 60/40m LPF       U3 - D5       LPF[1] : from C0=0x12: C4[1]
//  Bit  6 - 80m LPF          U3 - D6       LPF[2] : from C0=0x12: C4[2]
//  Bit  7 - 160m LPF         U3 - D7       LPF[3] : from C0=0x12: C4[3]
//  Bit  8 - Ant #1           U5 - D0       Gate from C0=0:C4[1:0]=00
//  Bit  9 - Ant #2           U5 - D1       Gate from C0=0:C4[1:0]=01
//  Bit 10 - Ant #3           U5 - D2       Gate from C0=0:C4[1:0]=10
//  Bit 11 - T/R relay        U5 - D3       T/R relay. 1=TX TXRX_Relay strobe
//  Bit 12 - Red Led          U5 - D4       TXRX_Relay strobe
//  Bit 13 - 6m LPF           U5 - D5       LPF[4] : from C0=0x12: C4[4]
//  Bit 14 - 12/10m LPF       U5 - D6       LPF[5] : from C0=0x12: C4[5]
//  Bit 15 - 17/15m LPF       U5 - D7       LPF[6] : from C0=0x12: C4[6]
//
// bit 4 (or bit 11 as sent by AXI) replaced by TX strobe
//
//  data to send to Alex Rx filters is in the folowing format:
//  bits 15:0 - RX1; bits 31:16 - RX1
// (IC designators and functions for 7000DLE RF board)
//
//  Bit  0 - Yellow LED       U6 - QA       0
//  Bit  1 - 10-22 MHz BPF    U6 - QB       BPF[0]: from C0=0x12: C3[0]
//  Bit  2 - 22-35 MHz BPF    U6 - QC       BPF[1]: from C0=0x12: C3[1]
//  Bit  3 - 6M Preamp        U6 - QD       10/6M LNA: from C0=0x12: C3[6]
//  Bit  4 - 6-10MHz BPF      U6 - QE       BPF[2]: from C0=0x12: C3[2]
//  Bit  5 - 2.5-6 MHz BPF    U6 - QF       BPF[3]: from C0=0x12: C3[3]
//  Bit  6 - 1-2.5 MHz BPF    U6 - QG       BPF[4]: from C0=0x12: C3[4]
//  Bit  7 - N/A              U6 - QH       0
//  Bit  8 - Transverter      U10 - QA      Gated C122_Transverter. True if C0=0: C3[6:5]=11
//  Bit  9 - Ext1 In          U10 - QB      Gated C122_Rx_2_in. True if C0=0: C3[6:5]=10
//  Bit 10 - N/A              U10 - QC      0
//  Bit 11 - PS sample select U10 - QD      Selects main or RX_BYPASS_OUT Gated C122_Rx_1_in True if C0=0: C3[6:5]=01
//  Bit 12 - RX1 Filt bypass  U10 - QE      BPF[5]: from C0=0x12: C3[5]
//  Bit 13 - N/A              U10 - QF      0
//  Bit 14 - RX1 master in    U10 - QG      (selects main, or transverter/ext1) Gated. True if C0=0: C3[6:5]=11 or C0=0: C3[6:5]=10
//  Bit 15 - RED LED          U10 - QH      0
//  Bit 16 - Yellow LED       U7 - QA       0
//  Bit 17 - 10-22 MHz BPF    U7 - QB       BPF2[0]: from C0=0x24: C1[0]
//  Bit 18 - 22-35 MHz BPF    U7 - QC       BPF2[1]: from C0=0x24: C1[1]
//  Bit 19 - 6M Preamp        U7 - QD       10/6M LNA2: from C0=0x24: C1[6]
//  Bit 20 - 6-10MHz BPF      U7 - QE       BPF2[2]: from C0=0x24: C1[2]
//  Bit 21 - 2.5-6 MHz BPF    U7 - QF       BPF2[3]: from C0=0x24: C1[3]
//  Bit 22 - 1-2.5 MHz BPF    U7 - QG       BPF2[4]: from C0=0x24: C1[4]
//  Bit 23 - N/A              U7 - QH       0
//  Bit 24 - RX2_GROUND       U13 - QA      RX2_GROUND: from C0=0x24: C1[7]
//  Bit 25 - N/A              U13 - QB      0
//  Bit 26 - N/A              U13 - QC      0
//  Bit 27 - N/A              U13 - QD      0
//  Bit 28 - HPF_BYPASS 2     U13 - QE      BPF2[5]: from C0=0x24: C1[5]
//  Bit 29 - N/A              U13 - QF      0
//  Bit 30 - N/A              U13 - QG      0
//  Bit 31 - RED LED 2        U13 - QH      0
//
//
//////////////////////////////////////////////////////////////////////////////////

//
// AlexManualRXFilters(unsigned int Bits, int RX)
// P2: provides a 16 bit word with all of the Alex settings for a single RX
// must be formatted according to the Alex specification
// RX=0 or 1: RX1; RX=2: RX2
//
void AlexManualRXFilters(unsigned int Bits, int RX) {
  pthread_mutex_lock(&AlexRXMutex);
  uint32_t Register = GAlexRXRegister;                             // copy original register

  if (RX != 2) {
    Register &= 0xFFFF0000;                             // turn off all affected bits
    Register |= Bits;                                   // add back all new bits
  } else {
    Register &= 0x0000FFFF;                             // turn off all affected bits
    Register |= (Bits << 16);                           // add back all new bits
  }

  if (Register != GAlexRXRegister) {                  // write back if changed
    GAlexRXRegister = Register;
    RegisterWrite(VADDRALEXSPIREG + VOFFSETALEXRXREG, Register); // and write to it
  }

  pthread_mutex_unlock(&AlexRXMutex);
}

//
// AlexManualTXFilters(unsigned int Bits)
// P2: provides a 16 bit word with all of the Alex settings for TX
// must be formatted according to the Alex specification
// FPGA V12 onwards: uses an additional register with TX ant settings
// HasTXAntExplicitly true if data is for the new TXfilter, TX ant register
//
void AlexManualTXFilters(unsigned int Bits, bool HasTXAntExplicitly) {
  uint32_t Register = Bits;                         // new setting

  pthread_mutex_lock(&AlexTXMutex);

  if (HasTXAntExplicitly && (Register != GAlexTXAntRegister)) {
    GAlexTXAntRegister = Register;
    RegisterWrite(VADDRALEXSPIREG + VOFFSETALEXTXANTREG, Register);  // and write to it
  } else if (!HasTXAntExplicitly && (Register != GAlexTXFiltRegister)) {
    GAlexTXFiltRegister = Register;
    RegisterWrite(VADDRALEXSPIREG + VOFFSETALEXTXFILTREG, Register); // and write to it
  }

  pthread_mutex_unlock(&AlexTXMutex);
}

//
// SetTXDriveLevel(unsigned int Level)
// sets the TX DAC current via a PWM DAC output
// level: 0 to 255 drive level value (255 = max current)
// sets both step attenuator drive and PWM DAC drive for high speed DAC current,
// using ROMs calculated at initialise.
//
void SetTXDriveLevel(unsigned int Level) {
  uint32_t RegisterValue = 0;
  uint32_t DACDrive, AttenDrive;
  Level &= 0xFF;                                  // make sure 8 bits only
  DACDrive = DACCurrentROM[Level];                // get PWM
  AttenDrive = DACStepAttenROM[Level];            // get step atten
  RegisterValue = DACDrive;                       // set drive level when RX
  RegisterValue |= (DACDrive << 8);               // set drive level when TX
  RegisterValue |= (AttenDrive << 16);            // set step atten when RX
  RegisterValue |= (AttenDrive << 24);            // set step atten when TX

  pthread_mutex_lock(&TXdriveMutex);

  if (RegisterValue != GTXDACCtrl) {
    GTXDACCtrl = RegisterValue;
    RegisterWrite(VADDRDACCTRLREG, RegisterValue);  // and write to it
  }

  pthread_mutex_unlock(&TXdriveMutex);
}


//
// EnableLine: true: enable Line input, false: enable Mic input
// MicBoost:   true: use 20dB mic boost, false: no boost
// LineInGain: LineIn gain vaule
//
// MicBoost has no effect if EnableLine is true
// LineInGain has no effect if MicLine is true
//
void SetCodecInputParams(bool EnableLine, bool EnableBoost, int LineInGain) {
  unsigned int Path, Gain;
  pthread_mutex_lock(&CodecMutex);
  switch (InstalledCodec) {
  case e23b:
    // Path = Register 4, Gain = Register 0
    Path = GCodecPath & 0xFFF8;  // Clear InSel, MicMute, MicBoost
    Gain = GCodecGain & 0xFFE0;

    if (EnableLine) {
      Path |= 0x02;  // Line-In, Mic muted, no Boost
      Gain |= (LineInGain & 0x001F);   // 5-bit value
    } else {
      Path |= 0x04;  // Mic-In, Mic normal (unmuted)
      if (EnableBoost) { Path |= 0x0001; } // Set MicBoost bit
    }

    if(Path != GCodecPath) {
      GCodecPath = Path;
      CodecRegisterWrite(4, Path);
    }

    if(Gain != GCodecGain) {
      GCodecGain = Gain;
      CodecRegisterWrite(0, Gain);
    }

    break;
  case e3204:
    // Path = Registers 52/55, Gain = Registers 59/60
    if (EnableLine) {
      // Route LineIn, and set gain
      Path = 0xC0; // IN1 routed to MICPGA with 40k resistance
      Gain = 3*(LineInGain & 0x001F);  // in 0.5 dB steps, from 0 to 46.5 dB
    } else {
      // Route MicIn, set gain to 23 or 3 dB
      Path = 0x04; // IN3 routed to PGA with 10k resistance
      Gain = EnableBoost? 46 : 6;
    }

    if (Path != GCodecPath) {
      // Select Page 1, update registers 52 and 55
      GCodecPath = Path;
      CodecRegisterWrite(0x00, 0x01);
      CodecRegisterWrite(52, Path);
      CodecRegisterWrite(55, Path);
    }

    // Legal values for Gain at this place are 0 - 95, we use 0 - 93

    if (Gain != GCodecGain) {
      // Select Page 1, update registers 59 and 60
      GCodecGain = Gain;
      CodecRegisterWrite(0x00, 0x01);
      CodecRegisterWrite(59, Gain);
      CodecRegisterWrite(60, Gain);
    }
    break;
  default:
    t_print("%s: Invalid Installed Codec\n", __FUNCTION__);
    break;
  }
  pthread_mutex_unlock(&CodecMutex);
}

//
// SetOrionMicOptions(bool MicRing, bool EnableBias, bool EnablePTT)
// sets the microphone control inputs
// write the bits to GPIO. Note the register bits aren't directly the protocol input bits.
// note also that EnablePTT is actually a DISABLE signal (enabled = 0)
//
void SetOrionMicOptions(bool MicRing, bool EnableBias, bool EnablePTT) {
  uint32_t Register;                              // FPGA register content
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings
  Register &= ~(1 << VMICBIASENABLEBIT);          // strip old bits
  Register &= ~(1 << VMICPTTSELECTBIT);           // strip old bits
  Register &= ~(1 << VMICSIGNALSELECTBIT);
  Register &= ~(1 << VMICBIASSELECTBIT);

  if (!MicRing) {                                   // add new bits where set
    Register &= ~(1 << VMICSIGNALSELECTBIT);    // mic on tip
    Register |= (1 << VMICBIASSELECTBIT);       // and hence mic bias on tip
    Register &= ~(1 << VMICPTTSELECTBIT);       // PTT on ring
  } else {
    Register |= (1 << VMICSIGNALSELECTBIT);     // mic on ring
    Register &= ~(1 << VMICBIASSELECTBIT);      // bias on ring
    Register |= (1 << VMICPTTSELECTBIT);        // PTT on tip
  }

  if (EnableBias) {
    Register |= (1 << VMICBIASENABLEBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                        // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);      // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetBalancedMicInput(bool Balanced)
// selects the balanced microphone input, not supported by current protocol code.
// just set the bit into GPIO
//
void SetBalancedMicInput(bool Balanced) {
  uint32_t Register;                              // FPGA register content
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings
  Register &= ~(1 << VBALANCEDMICSELECT);         // strip old bit

  if (Balanced) {
    Register |= (1 << VBALANCEDMICSELECT);  // set new bit
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                        // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);      // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetADCAttenuator(unsigned int Atten1, bool RXAtten1, bool TXAtten1, unsigned int Atten2, bool RXAtten2, bool TXAtten2)
// sets the  stepped attenuator on the ADC input
// Atten provides a 5 bit atten value
// RXAtten: if true, sets atten to be used during RX
// TXAtten: if true, sets atten to be used during TX
//
// In P2, the RX attenuators are set from the HighPrio handler, while te
// TX attenuators are set from the DUCspecific handler.
//
void SetADCAttenuator(unsigned int Atten1, bool RXAtten1, bool TXAtten1, unsigned int Atten2, bool RXAtten2, bool TXAtten2) {

  pthread_mutex_lock(&AttenMutex);
  uint32_t Register = GRXADCCtrl;                          // get existing settings

  const uint32_t RXMask1 = 0b00000000000000011111;
  const uint32_t TXMask1 = 0b00000000001111100000;
  const uint32_t RXMask2 = 0b00000111110000000000;
  const uint32_t TXMask2 = 0b11111000000000000000;

  if (RXAtten1) {
     Register &=  ~RXMask1;
     Register |=  (Atten1 & 0x1F);
  }

  if (TXAtten1) {
     Register &=  ~TXMask1;
     Register |=  (Atten1 & 0x1F) << 5;
  }

  if (RXAtten2) {
     Register &=  ~RXMask2;
     Register |=  (Atten2 & 0x1F) << 10;
  }

  if (TXAtten2) {
     Register &=  ~TXMask2;
     Register |=  (Atten2 & 0x1F) << 15;
  }

  if (Register != GRXADCCtrl) {
    GRXADCCtrl = Register;
    RegisterWrite(VADDRADCCTRLREG, Register);      // and write to it
  }
  pthread_mutex_unlock(&AttenMutex);
}

//
//void SetCWIambicKeyer(...)
// setup CW iambic keyer parameters
// Speed: keyer speed in WPM
// weight: typically 50
// ReverseKeys: swaps dot and dash
// mode: true if mode B
// strictSpacing: true if it enforces character spacing
// IambicEnabled: if false, reverts to straight CW key
//
void SetCWIambicKeyer(uint8_t Speed, uint8_t Weight, bool ReverseKeys, bool Mode,
                      bool StrictSpacing, bool IambicEnabled, bool Breakin) {
  uint32_t Register;
  //
  // Need Mutex since this is called from DUC spec, but SetCWXBits from HighPrio
  //
  pthread_mutex_lock(&IambicMutex);
  Register = GIambicConfigReg;                    // copy of H/W register
  Register &= ~VIAMBICBITS;                       // strip off old iambic bits
  // set new data
  Register |= Speed;
  Register |= (Weight << VIAMBICWEIGHT);

  if (ReverseKeys) { Register |= (1 << VIAMBICREVERSED); }

  if (Mode) { Register |= (1 << VIAMBICMODE); }

  if (StrictSpacing) { Register |= (1 << VIAMBICSTRICT); }

  if (IambicEnabled) { Register |= (1 << VIAMBICENABLE); }

  if (Breakin) { Register |= (1 << VCWBREAKIN); }

  if (Register != GIambicConfigReg) {
    GIambicConfigReg = Register;
    RegisterWrite(VADDRIAMBICCONFIG, Register);
  }
  pthread_mutex_unlock(&IambicMutex);
}

//
// void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot)
// setup CWX (host generated dot and dash)
// Note: piHPSDR currently does not use this feature.
//
void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot) {
  uint32_t Register;
  pthread_mutex_lock(&IambicMutex);
  Register = GIambicConfigReg;                    // copy of H/W register
  Register &= ~VIAMBICCWXBITS;                    // strip off old CWX bits

  if (CWXEnabled) {
    Register |= (1 << VIAMBICCWX);  // set bit if enabled
  }

  if (CWXDot) {
    Register |= (1 << VIAMBICCWXDOT);  // set bit if enabled
  }

  if (CWXDash) {
    Register |= (1 << VIAMBICCWXDASH);  // set bit if enabled
  }

  if (Register != GIambicConfigReg) {             // save if changed
    GIambicConfigReg = Register;
    RegisterWrite(VADDRIAMBICCONFIG, Register);
  }
  pthread_mutex_unlock(&IambicMutex);
}

//
// SetDDCADC(int DDC, EADCSelect ADC)
// sets the ADC to be used for each DDC
// DDC = 0 to 9
//
void SetDDCADC(int DDC, EADCSelect ADC) {
  uint32_t Register;
  uint32_t ADCSetting;
  uint32_t Mask;

  ADCSetting = ((uint32_t)ADC & 0x3) << (DDC * 2); // 2 bits with ADC setting
  Mask = 0x3 << (DDC * 2);                       // 0,2,4,6,8,10,12,14,16,18 bit positions
  pthread_mutex_lock(&DDCInSelMutex);                       // get protected access
  Register = DDCInSelReg;                    // get current register setting
  Register &= ~Mask;                         // strip ADC bits
  Register |= ADCSetting;

  if (Register != DDCInSelReg) {
    DDCInSelReg = Register;                    // write back
    RegisterWrite(VADDRDDCINSEL, Register);    // and write to it
  }

  pthread_mutex_unlock(&DDCInSelMutex);                       // get protected access
}

//
// void SetRXDDCEnabled(bool IsEnabled);
// sets enable bit so DDC operates normally. Resets input FIFO when starting.
//
void SetRXDDCEnabled(bool IsEnabled) {
  uint32_t Address;                 // register address
  uint32_t Data;                    // register content
  Address = VADDRDDCINSEL;              // DDC config register address
  pthread_mutex_lock(&DDCInSelMutex);                       // get protected access
  Data = DDCInSelReg;                                 // get current register setting

  if (IsEnabled) {
    Data |= (1 << 30);  // set new bit
  } else {
    Data &= ~(1 << 30);  // clear new bit
  }

  if (Data != DDCInSelReg) {
    DDCInSelReg = Data;          // write back
    RegisterWrite(Address, Data);         // write back
  }

  pthread_mutex_unlock(&DDCInSelMutex);                       // get protected access
}


#define VMINCWRAMPDURATION         5             // 5ms min
#define VMAXCWRAMPDURATION        10             // 10ms max
#define VMAXCWRAMPDURATIONV14PLUS 20             // 20ms max, for firmware V1.4 and later

//
// InitialiseCWKeyerRamp(uint32_t Length)
// calculates an "S" shape ramp curve and loads into RAM
// needs to be called before keyer enabled!
// parameter is length in milliseconds; typically 9
// setup ramp memory and rampl length fields
// only calculate if parameters have changed!
//
// NOTE: only in-lined into SetKeyerParams() where it is protected
//       by the KeyerMutex
//
static inline void InitialiseCWKeyerRamp(uint8_t Length) {

  if (FPGA_MinorVersion >= 14) {
    if (Length > VMAXCWRAMPDURATIONV14PLUS) { Length = VMAXCWRAMPDURATIONV14PLUS; }
  } else {
    if (Length > VMAXCWRAMPDURATION) { Length = VMAXCWRAMPDURATION; }
  }

  if (Length < VMINCWRAMPDURATION) { Length = VMINCWRAMPDURATION; }

  uint32_t RampLength = Length * 192;

  // now apply that ramp length
  if (RampLength != GCWKeyerRampLength) {

    // ========================================================================
    //
    // Calculate a "DL1YCF" ramp
    // -------------------------
    //
    // The "black magic" in the coefficients comes from optimizing them
    // against the spectral pollution of a string of dots,
    // namely with ramp width 7 msec for CW speed  5 - 15 wpm
    //        and  ramp width 8 msec for CW speed 16 - 32 wpm
    //        and  ramp width 9 msec for CW speed 33 - 40 wpm
    //
    // such that the spectra meet ARRL's "clean signal initiative" requirement
    // for the maximum peak strength at frequencies with a distance to the
    // carrier that is larger than an offset, namely
    //
    //  -20 dBc for offsets >   90 Hz
    //  -40 dBc for offsets >  150 Hz
    //  -60 dBc for offsets >  338 Hz
    //
    // and is also meets the extended DL1YCF criteria which restrict spectral
    // pollution at larger offsets, namely
    //
    //  -80 dBc for offsets >  600 Hz
    // -100 dBc for offsets >  900 Hz
    // -120 dBc for offsets > 1200 Hz
    //
    // ========================================================================

    for (unsigned int Cntr = 0; Cntr < RampLength; Cntr++) {
      uint32_t Sample;
      double y = (double) Cntr / (double) RampLength;     // between 0 and 1
      double y2  = y * 6.2831853071795864769252867665590;  //  2 Pi y
      double y4  = y2 + y2;                                //  4 Pi y
      double y6  = y4 + y2;                                //  6 Pi y
      double y8  = y4 + y4;                                //  8 Pi y
      double y10 = y4 + y6;                                // 10 Pi y
      double rampsample = y - 0.12182865361171612    * sin(y2)
                          - 0.018557469249199286   * sin(y4)
                          - 0.0009378783245428506  * sin(y6)
                          + 0.0008567571519403228  * sin(y8)
                          + 0.00018706912431472442 * sin(y10);
      Sample = (uint32_t) (rampsample * 8388607.0);
      RegisterWrite(VADDRCWKEYERRAM + 4 * Cntr, Sample);
    }

    for (unsigned int Cntr = RampLength; Cntr < VRAMPSIZE; Cntr++) {                     // fill remainder of RAM
      RegisterWrite(VADDRCWKEYERRAM + 4 * Cntr, 8388607);
    }

    GCWKeyerRampLength = RampLength;
  }
}

//
// SetTXModulationSource(ETXModulationSource Source)
// selects the modulation source for the TX chain.
// this will need to be called operationally to change over between CW & I/Q
//
static inline void SetTXModulationSource(ETXModulationSource Source) {
  uint32_t Register;
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings
  Register &= 0xFFFFFFFC;                             // remove old bits
  Register |= ((unsigned int)Source);                 // add new bits

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                    // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);  // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}

//
// EnableCW (bool Enabled, bool Breakin)
// enables or disables CW mode; selects CW as modulation source.
// If Breakin enabled, the key input engages TX automatically
// and generates sidetone.
//
void EnableCW (bool Enabled, bool Breakin) {
  //
  // set I/Q modulation source if CW selected
  //
  GCWEnabled = Enabled;

  if (Enabled) {
    SetTXModulationSource(eCWKeyer);  // CW source
  } else {
    SetTXModulationSource(eIQData);  // else IQ source
  }

  // now set keyer enable if CW and break-in
  GBreakinEnabled = Breakin;
  ActivateCWKeyer(GBreakinEnabled && GCWEnabled);
}

void SetCWSideTone(bool Enabled, uint8_t Volume, uint16_t Frequency) {
  uint32_t Register;
  // This is for 48000 kHz
  Register = (512 * Frequency) / 375;

  if (Enabled) {
    Register |= (Volume & 0xFF) << 24;  // add back new bits; resize to 16 bits
  }

  pthread_mutex_lock(&SideToneMutex);

  if (Register != GSideToneReg) {
    GSideToneReg = Register;                     // store it back
    RegisterWrite(VADDRSIDETONECONFIGREG, Register);   // and write to it
  }

  pthread_mutex_unlock(&SideToneMutex);
}


void SetKeyerParams(uint8_t Delay, uint16_t HangTime, uint8_t Ramp) {
  uint32_t Register;
  pthread_mutex_lock(&KeyerMutex);
  Register = GCWKeyerSetup;                           // get current settings
  Register &= 0xFFFC0000;                             // remove old bits

  Register |= (Delay & 0xFF);
  Register |= (HangTime & 0x3FF) << VCWKEYERHANG;

  if (Ramp > 0) {
    InitialiseCWKeyerRamp(Ramp);
    Register &= 0x8003FFFF;                              // strip out ramp bits
    if (FPGA_MinorVersion >= 14) {
      Register |= (GCWKeyerRampLength << VCWKEYERRAMP);          // word end address
    } else {
      Register |= ((GCWKeyerRampLength << 2) << VCWKEYERRAMP);   // byte end address
    }
  }

  if (Register != GCWKeyerSetup) {                    // write back if different
    GCWKeyerSetup = Register;                       // store it back
    RegisterWrite(VADDRKEYERCONFIGREG, Register);   // and write to it
  }

  pthread_mutex_unlock(&KeyerMutex);
}

//
// SetXvtrEnable(bool Enabled)
// enables or disables transverter. If enabled, the PA is not keyed.
//
void SetXvtrEnable(bool Enabled) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings

  if (Enabled) {
    Register |= (1 << VXVTRENABLEBIT);
  } else {
    Register &= ~(1 << VXVTRENABLEBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                    // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);      // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetPAEnabled(bool Enabled)
// true if PA is enabled.
//
void SetPAEnabled(bool Enabled) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings

  if (!Enabled) {
    Register |= (1 << VTXRELAYDISABLEBIT);
  } else {
    Register &= ~(1 << VTXRELAYDISABLEBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                    // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);  // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetSpkrMute(bool IsMuted)
// enables or disables the Codec speaker output
//
void SetSpkrMute(bool IsMuted) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings

  if (IsMuted) {
    Register |= (1 << VSPKRMUTEBIT);
  } else {
    Register &= ~(1 << VSPKRMUTEBIT);
  }

  if (Register != GPIORegValue) {
    GPIORegValue = Register;                        // store it back
    RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// ReadStatusRegister(void)
// this is a precursor to getting any of the data itself; simply reads the register to a local variable
// probably call every time an outgoig packet is put together initially
// but possibly do this one a timed basis.
//
void ReadStatusRegister(void) {
  uint32_t StatusRegisterValue = 0;
  StatusRegisterValue = RegisterRead(VADDRSTATUSREG);
  GStatusRegister = StatusRegisterValue;                        // save to global
}

//
// GetPTTInput(void)
// return true if PTT input is pressed.
// depends on the status register having been read before this is called!
//
bool GetPTTInput(void) {
  bool Result = false;
  Result = (bool)(GStatusRegister & 1);                       // get PTT bit
  return Result;
}

//
// GetKeyerDashInput(void)
// return true if keyer dash input is pressed.
// depends on the status register having been read before this is called!
//
bool GetKeyerDashInput(void) {
  bool Result = false;
  Result = (bool)((GStatusRegister >> VKEYINB) & 1);                       // get PTT bit
  return Result;
}

//
// GetKeyerDotInput(void)
// return true if keyer dot input is pressed.
// depends on the status register having been read before this is called!
//
bool GetKeyerDotInput(void) {
  bool Result = false;
  Result = (bool)((GStatusRegister >> VKEYINA) & 1);                       // get PTT bit
  return Result;
}

//
// GetCWKeyDown(void)
// return true if keyer has initiated TX.
// depends on the status register having been read before this is called!
//
bool GetCWKeyDown(void) {
  bool Result = false;
  Result = (bool)((GStatusRegister >> VCWKEYDOWN) & 1);                       // get "PTT from keyer" bit.
  return Result;
}

//
// GetP2PTTKeyInputs(void)
// return several bits from Saturn status register:
// bit 0 - true if PTT active or CW keyer active
// bit 1 - true if CW dot input active
// bit 2 - true if CW dash input active or IO8 active
// bit 4 - true if 10MHz to 122MHz PLL is locked
// note that PTT declared if PTT pressed, or CW key is pressed.
//
unsigned int GetP2PTTKeyInputs(void) {
  unsigned int Result = 0;

  // ReadStatusRegister();
  if (GStatusRegister & 1) {
    Result |= 1;  // set PTT output bit
  }

  if ((GStatusRegister >> VCWKEYDOWN) & 1) {
    Result |= 1;  // set PTT output bit if keyer PTT active
  }

  if ((GStatusRegister >> VKEYINA) & 1) {
    Result |= 2;  // set dot output bit
  }

  if ((GStatusRegister >> VKEYINB) & 1) {
    Result |= 4;  // set dash output bit
  }

  if (!((GStatusRegister >> VUSERIO8) & 1)) {
    Result |= 4;  // set dash output bit if IO8 active
  }

  if ((GStatusRegister >> VPLLLOCKED) & 1) {
    Result |= 16;  // set PLL output bit
  }

  if ((GStatusRegister >> VCWKEYDOWN) & 1) {
    Result |= 1;  // set PTT if keyer asserted TX
  }

  return Result;
}

//
// GetADCOverflow(void)
// return true if ADC amplitude overflow has occurred since last read.
// the overflow stored state is reset when this is read.
// returns bit0: 1 if ADC1 overflow; bit1: 1 if ARC2 overflow
//
unsigned int GetADCOverflow(void) {
  unsigned int Result = 0;
  Result = RegisterRead(VADDRADCOVERFLOWBASE);
  return (Result & 0x3);
}

//
// GetUserIOBits(void)
// return the user input bits
// returns IO4 in LSB, IO5 in bit 1, ATU bit in bit 2 & IO8 in bit 3
//
unsigned int GetUserIOBits(void) {
  unsigned int Result = 0;
  Result = ((GStatusRegister >> VUSERIO4) & 0b1011);                       // get user input 4/5/-/8
  Result = Result ^ 0x8;                                                   // invert IO8 (should be active low)
  Result |= ((GStatusRegister >> 7) & 0b0100);                             // get ATU bit into IO6 location
  return Result;
}

//
// unsigned int GetAnalogueIn(unsigned int AnalogueSelect)
// return one of 6 ADC values from the RF board analogue values
// the paramter selects which input is read.
// AnalogueSelect=0: AIN1 .... AnalogueSepect=5: AIN6
unsigned int GetAnalogueIn(unsigned int AnalogueSelect) {
  unsigned int Result = 0;
  AnalogueSelect &= 7;                                        // limit to 3 bits
  Result = RegisterRead(VADDRALEXADCBASE + 4 * AnalogueSelect);
  return Result;
}


//
// Initialise TLV320AIC3204 codec.
// separate function because there are many operations needed!
// High Performance Stereo Playback and record
// ---------------------------------------------
// PowerTune mode PTM_P3 is used for high
// performance 16-bit audio. For PTM_P4,
// an external audio interface that provides
// 20-bit audio is required.
//
// For normal USB Audio, no hardware change is required.
//
// If using an external interface, SW2.4 and
// SW2.5 of the USB-ModEVM must be set to
// HI and clocks can be connected to J14 of
// the USB-ModEVM.
//
// Audio is routed to both headphone and
// line outputs.
//
static void InitialiseTLV320AIC3204(void)
{
  pthread_mutex_lock(&CodecMutex);
  GCodecGain = 46;      // Mic Preamp set to 23 dB
  GCodecPath = 0x04;    // IN3 routed to PGA with 10k resistance
  // Software reset
  // pg0, r1: Initialize the device through software reset; takes 1ms
  // Select Page 0
  CodecRegisterWrite(0, 0x00);
  CodecRegisterWrite(1, 0x01);
  usleep (2000);                                      // 2ms wait for reset to complete

  //
  // Clock Settings
  // The codec receives: MCLK = 12.288 MHz, target sample rate is 48 kHz
  // The default (after reset) oversampling rate is 128, so we (only) need
  // and additional divider by 2 (NDAC = 1, MDAC = 2)
  // Need MDAC at least 2 since the resource class of the processing block is 8
  //

  //
  // pg0, r11&12: NDAC = 1, MDAC = 2
  CodecRegisterWrite(11, 0x81); // enable NDAC = 1
  CodecRegisterWrite(12, 0x82); // enable MDAC = 1

  //
  //pg0 r18, 19: set ADC clock = DAC clock
  CodecRegisterWrite(18, 0x01); // bit7 clear --> ADC_CLK = DAC_CLK
  CodecRegisterWrite(19, 0x02); // bit7 clear --> ADC_MOD_CLK = DAC_MOD_CLK

  // Signal Processing Settings


  //
  // pg0 r60: set the DAC Mode to PRB_P1 (LVB)
  // pg0 r61: set the ADC Mode to PRB_P1 (LVB)
  //
  // PRB_P1 is the default processing block (resource class = 8) with 3 biquads
  //
  CodecRegisterWrite(60, 0x01);
  CodecRegisterWrite(61, 0x01);

  //
  // Initialize Codec
  //
  // Select Page 1
  CodecRegisterWrite(0, 0x01);

  //
  // pg1 r1: Disable weak AVDD in presence of external AVDD supply
  CodecRegisterWrite(1, 0x08);

  //
  // pg1 r2: Enable Master Analog Power Control (LVB)
  CodecRegisterWrite(2, 0x09); // DVDD = AVDD = 1.75 Volt, analog blocks disabled

  //
  // pg1 r123: Set the REF charging time to slow (LVB)
  CodecRegisterWrite(123, 0x00);

  //
  // pg1 r1: disable weak AVDD in presence of external AVDD supply
  // DUPLICATE?
  CodecRegisterWrite(1, 0x08);

  //
  // pg1 r2: Enable Master Analog Power Control
  CodecRegisterWrite(2, 0x01); // DVDD = AVDD = 1.75 Volt, analog blocks enabled

  //
  // pg1 r61: Select ADC PTM_R4 (this is the default)
  CodecRegisterWrite(61, 0x00);

  // pg1 r71: Set the input powerup time
  CodecRegisterWrite(71, 0x32);  // 6.4 msec

  //
  // Recording Setup
  //
  // Select Page 1
  CodecRegisterWrite(0x00, 0x01);

  //
  // pg1 r58: enable analogue inputs (IN1 and IN3)
  //
  CodecRegisterWrite(58, 0x30);

  //
  // pg1 r52, r55: route IN1+ or IN3+ to PGA
  CodecRegisterWrite(52, GCodecPath);
  CodecRegisterWrite(55, GCodecPath);

  //
  // pg1 r54: Route Common Mode to PGA with 10k resistance
  // pg1 r57: Route Common Mode to PGA with 10k resistance
  CodecRegisterWrite(54, 0x40);
  CodecRegisterWrite(57, 0x40);

  //
  // pg1 r71: input powerup time
  CodecRegisterWrite(71, 0x32);  // 6.4 msec

  //
  // pg1 r59: Unmute Left MICPGA, Gain selection of 23dB
  // pg1 r60: Unmute Right MICPGA, Gain selection of 23dB
  CodecRegisterWrite(59, GCodecGain);
  CodecRegisterWrite(60, GCodecGain);

  //
  // pg1 r51: mic bias
  CodecRegisterWrite(51, 0x68);  // Bias powered up (from LDOIN), max. Voltage

  //
  // Select Page 0
  CodecRegisterWrite(0x00, 0x00);

  //
  // pg0 r81: Power up LADC/RADC
  CodecRegisterWrite(81, 0xC0);

  //
  // pg0 r82: Unmute LADC/RADC
  CodecRegisterWrite(82, 0x00);

  //
  // Playback Setup
  //

  //
  // Select Page 1
  CodecRegisterWrite(0, 0x01);

  //
  // Anti-thump step 1.
  // pg1 r20: De-pop. 6K, 5 time constants -> 300ms; add 100ms soft routing.
  CodecRegisterWrite(20, 0x65); // step time 50 msec, 6 kOhm resistance, 5 time constants, 50 msec soft routing

  //
  // anti-thump step 2.
  // pg1 r10: common mode
  CodecRegisterWrite(10, 0x3B); // HP common mode 1.65 V from LDOIN

  //
  // anti-thump step 3.
  // pg1 r12, 13: Route LDAC/RDAC to HPL/HPR
  CodecRegisterWrite(12, 0x08);
  CodecRegisterWrite(13, 0x08);

  //
  // pg1 r14, 15: Route LDAC/RDAC to LOL/LOR
  CodecRegisterWrite(14, 0x08);
  CodecRegisterWrite(15, 0x08);

  // before anti-thump step 4:
  // pg1 r22, 23: in1 to headphone bypass: MUTE
  CodecRegisterWrite(22, 0x72);
  CodecRegisterWrite(23, 0x72);

  //
  // anti-thump step 4:
  // pg0 r63: Power up LDAC/RDAC
  CodecRegisterWrite(0x00, 0x00);             // select page 0
  CodecRegisterWrite(0x3F, 0xD6);

  //
  // select Page 1
  CodecRegisterWrite(0x00, 0x01);

  //
  // anti-thump step 5:
  // pg1 r16, 17: Unmute HPL/HPR driver, 0dB Gain
  CodecRegisterWrite(16, 0x00);
  CodecRegisterWrite(17, 0x00);

  //
  // anti-thump step 6:
  // pg1 r9: Power up HPL/HPR and LOL/LOR drivers (LVB)
  CodecRegisterWrite(9, 0x3F);

  //
  // pg1 r18, 19: Unmute LOL/LOR driver, 0dB Gain
  CodecRegisterWrite(18, 0x00);
  CodecRegisterWrite(19, 0x00);

  //
  // Select Page 0
  CodecRegisterWrite(0x00, 0x00);

  //
  // pg0 r65, 66: DAC => 0dB
  CodecRegisterWrite(65, 0x00);
  CodecRegisterWrite(66, 0x00);

  //
  // anti-thump step 7: AFTER 300ms DELAY for ramp-up
  usleep(300000);

  // pg0 r64: Unmute LDAC/RDAC
  CodecRegisterWrite(64, 0x00);

  pthread_mutex_unlock(&CodecMutex);
}

static void InitialiseTLV320AIC23B(void) {
  pthread_mutex_lock(&CodecMutex);
  GCodecGain = 0;                                   // Codec left line in gain register
  GCodecPath = 0x14;                              // mic input, no boost
  CodecRegisterWrite(15, 0x0);                            // reset register: reset deveice
  usleep(100);
  CodecRegisterWrite(9, 0x1);                             // digital activation set to ACTIVE
  usleep(100);
  CodecRegisterWrite(4, GCodecPath);
  usleep(100);
  CodecRegisterWrite(6, 0x0);                             // all elements powered on
  usleep(100);
  CodecRegisterWrite(7, 0x2);                             // slave; no swap; right when LRC high; 16 bit, I2S
  usleep(100);
  CodecRegisterWrite(8, 0x0);                             // no clock divide; rate ctrl=0; normal mode, oversample 256Fs
  usleep(100);
  CodecRegisterWrite(5, 0x0);                             // no soft mute; no deemphasis; ADC high pass filter enabled
  usleep(100);
  CodecRegisterWrite(0, GCodecGain);                // line in gain=0
  usleep(100);
  pthread_mutex_unlock(&CodecMutex);
}

//
// CodecInitialise()
// initialise the CODEC, with the register values that don't normally change
// these are the values used by existing HPSDR FPGA firmware
//
void CodecInitialise() {
  if (Saturn_PCB_Version >= 3) {
      t_print("Initialising TLV320AIC3204 codec\n");
      InstalledCodec = e3204;
      InitialiseTLV320AIC3204();
  } else {
      t_print("Initialising TLV320AIC23B codec\n");
      InstalledCodec = e23b;
      InitialiseTLV320AIC23B();
  }
}

//
// SetTXAmplitudeScaling (unsigned int Amplitude)
// sets the overall TX amplitude. This must match the FPGA firmware
// and is set once on program start.
//
void SetTXAmplitudeScaling (unsigned int Amplitude) {
  uint32_t Register;
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                                // get current settings
  Register &= 0xFFC0000F;                                     // remove old bits
  Register |= ((Amplitude & 0x3FFFF) << VTXCONFIGSCALEBIT);   // add new bits

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                                // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);                  // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}

//
// SetTXProtocol2 (void)
// config TX for P2. This is called ONCE at startup
void SetTXProtocol2 () {
  uint32_t Register;
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings
  Register &= 0xFFFFFF7;                              // remove old bit
  Register |= 1 << VTXCONFIGPROTOCOLBIT;              // Set P2 (192 kHz)

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                    // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);  // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}

//
// void ResetDUCMux(void)
// resets to 64 to 48 bit multiplexer to initial state, expecting 1st 64 bit word
// also causes any input data to be discarded, so don't set it for long!
//
void ResetDUCMux(void) {
  uint32_t Register;
  uint32_t BitMask;
  BitMask = (1 << 29);
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings
  Register |= BitMask;                                // set reset bit
  RegisterWrite(VADDRTXCONFIGREG, Register);          // and write to it
  Register &= ~BitMask;                               // remove old bit
  RegisterWrite(VADDRTXCONFIGREG, Register);          // and write to it
  TXConfigRegValue = Register;
  pthread_mutex_unlock(&TXConfigMutex);
}

//
// void SetTXOutputGate(bool AlwaysOn)
// sets the sample output gater. If false, samples gated by TX strobe.
// if true, samples are alweays enabled.
//
void SetTXOutputGate(bool AlwaysOn) {
  uint32_t Register;
  uint32_t BitMask;
  BitMask = (1 << 2);
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings

  if (AlwaysOn) {
    Register |= BitMask;  // set bit if true
  } else {
    Register &= ~BitMask;  // clear bit if false
  }

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                    // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);  // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}

//
// void SetTXIQDeinterleave(bool Interleaved)
// if true, put DUC hardware in EER mode. Alternate IQ samples go:
// even samples to I/Q modulation; odd samples to EER.
// ensure FIFO empty & reset multiplexer when changing this bit!
// shgould be called by the TX I/Q data handler only to be sure
// of meeting that constraint
//
void SetTXIQDeinterleaved(bool Interleaved) {
  uint32_t Register;
  uint32_t BitMask;
  BitMask = (1 << 30);
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings

  if (Interleaved) {
    Register |= BitMask;  // set bit if true
  } else {
    Register &= ~BitMask;  // clear bit if false
  }

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                    // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);    // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}

//
// void EnableDUCMux(bool Enabled)
// enabled the multiplexer to take samples from FIFO and hand on to DUC
// // needs to be stoppable if there is an error condition
//
void EnableDUCMux(bool Enabled) {
  uint32_t Register;
  uint32_t BitMask;
  BitMask = 0x80000000;
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings

  if (Enabled) {
    Register |= BitMask;  // set bit if true
  } else {
    Register &= ~BitMask;  // clear bit if false
  }

  if (Register != TXConfigRegValue) {
    TXConfigRegValue = Register;                    // store it back
    RegisterWrite(VADDRTXCONFIGREG, Register);    // and write to it
  }

  pthread_mutex_unlock(&TXConfigMutex);
}
