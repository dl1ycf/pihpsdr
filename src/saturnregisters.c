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

#include <stdlib.h>                     // for function min()
#include <math.h>
#include <unistd.h>
#include <semaphore.h>

#include "saturndrivers.h"
#include "saturnregisters.h"
#include "message.h"

//
// Mutexes to protect registers that are accessed from several threads
//
static pthread_mutex_t CodecMutex     = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t GPIOMutex      = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t DDCInSelMutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t TXConfigMutex  = PTHREAD_MUTEX_INITIALIZER;

//
// 8 bit Codec register write over the AXILite bus via SPI
// using simple SPI writer IP
// given 7 bit register address and 9 bit data
//
void CodecRegisterWrite(unsigned int Address, unsigned int Data) {
  uint32_t WriteData;
  WriteData = (Address << 9) | (Data & 0x01FF);
  RegisterWrite(VADDRCODECSPIWRITEREG, WriteData);       // and write to it
  usleep(5);
}

//
// 8 bit Codec register read over the AXILite bus via SPI
// using simple SPI writer IP
// given 7 bit register address
// note this function will work with the IP we've had for a while;
// but only transfers data using the new TLV320AIC3204 codec)
//
uint8_t CodecRegisterRead(unsigned int Address)
{
  uint32_t WriteData;
  uint32_t ReadData;
  WriteData = (Address << 9) | (1<<8);             // shift out address and 1 bit
  RegisterWrite(VADDRCODECSPIWRITEREG, WriteData); // and write to it
  usleep(10);                                      // small wait for that shift to complete
  ReadData = RegisterRead(VADDRCODECSPIREADREG);
  usleep(5);

  return (uint8_t) (ReadData & 0xFF);
}

//
// ROMs for DAC Current Setting and 0.5dB step digital attenuator
//
static unsigned int DACCurrentROM[256];           // used for residual attenuation
static unsigned int DACStepAttenROM[256];         // provides most atten setting

//
// local copies of values written to registers
//
bool MOXAsserted;                                 // true if MOX as asserted

static uint32_t DDCDeltaPhase[VNUMDDC];           // DDC frequency settings
static uint32_t DUCDeltaPhase;                    // DUC frequency setting
static uint32_t GStatusRegister;                  // most recent status register setting
static uint32_t GPIORegValue;                     // value stored into GPIO
static uint32_t TXConfigRegValue;                 // value written into TX config register
static uint32_t DDCInSelReg;                      // value written into DDC config register
static uint32_t DDCRateReg;                       // value written into DDC rate register
static bool GADCOverride;                         // true if ADCs are to be overridden & use test source instead
static uint32_t P2SampleRates[VNUMDDC];           // numerical sample rates for each DDC
static uint32_t GDDCEnabled;                      // 1 bit per DDC
static uint32_t GTXDACCtrl;                       // TX DAC current setting & atten
static uint32_t GRXADCCtrl;                       // RX1 & 2 attenuations
static uint32_t GAlexTXFiltRegister;              // 16 bit used of 32
static uint32_t GAlexTXAntRegister;               // 16 bit used of 32
static uint32_t GAlexRXRegister;                  // 32 bit RX register
static bool GCWKeysReversed;                      // true if keys reversed. Not yet used but will be
static unsigned int GCWKeyerSpeed;                // Keyer speed in WPM. Not yet used
static unsigned int GCWKeyerMode;                 // Keyer Mode. True if mode B. Not yet used
static unsigned int GCWKeyerWeight;               // Keyer Weight. Not yet used
static bool GCWKeyerSpacing;                      // Keyer spacing
static bool GCWIambicKeyerEnabled;                // true if iambic keyer is enabled
static uint32_t GIambicConfigReg;                 // copy of iambic comfig register
static uint32_t GCWKeyerSetup;                    // keyer control register
static uint32_t GClassEPWMMin;                    // min class E PWM. NOT USED at present.
static uint32_t GClassEPWMMax;                    // max class E PWM. NOT USED at present.
static uint32_t GCodecConfigReg;                  // codec configuration
static bool GSidetoneEnabled;                     // true if sidetone is enabled
static unsigned int GSidetoneVolume;              // assigned sidetone volume (8 bit signed)
static bool GWidebandADC1;                        // true if wideband on ADC1. For P2 - not used yet.
static bool GWidebandADC2;                        // true if wideband on ADC2. For P2 - not used yet.
static unsigned int GWidebandSampleCount;         // P2 - not used yet
static unsigned int GWidebandSamplesPerPacket;    // P2 - not used yet
static unsigned int GWidebandUpdateRate;          // update rate in ms. P2 - not used yet.
static unsigned int GWidebandPacketsPerFrame;     // P2 - not used yet
static unsigned int GAlexEnabledBits;             // P2. True if Alex1-8 enabled. NOT USED YET.
static bool GPAEnabled;                           // P2. True if PA enabled. NOT USED YET.
static unsigned int GTXDACCount;                  // P2. #TX DACs. NOT USED YET.
static ESampleRate GDUCSampleRate;                // P2. TX sample rate. NOT USED YET.
static unsigned int GDUCSampleSize;               // P2. DUC # sample bits. NOT USED YET
static unsigned int GDUCPhaseShift;               // P2. DUC phase shift. NOT USED YET.
static bool GSpeakerMuted;                        // P2. True if speaker muted.
static bool GCWXMode;                             // True if in computer generated CWX mode
static bool GCWXDot;                              // True if computer generated CW Dot.
static bool GCWXDash;                             // True if computer generated CW Dash.
static bool GCWEnabled;                           // true if CW mode
static bool GBreakinEnabled;                      // true if break-in is enabled
static unsigned int GUserOutputBits;              // P2. Not yet implermented.
static uint32_t TXModulationTestReg;              // modulation test DDS
static bool GEnableTimeStamping;                  // true if timestamps to be added to data. NOT IMPLEMENTED YET
static bool GEnableVITA49;                        // true if tyo enable VITA49 formatting. NOT SUPPORTED YET
static unsigned int GCWKeyerRampms = 0;           // ramp length for keyer, in ms
static bool GCWKeyerRamp_IsP2 = false;            // true if ramp initialised for protocol 2

static unsigned int GNumADCs;                     // count of ADCs available

//
// local copies of Codec registers
//
static unsigned int GCodecLineInGain;             // Contents of Register 0 of old codec
static unsigned int GCodecAnaloguePath;           // Contents of Register 4 of old codec
static unsigned int GCodec2PGA;                   // Contents of R
static unsigned int GCodec2MicPGARouting;         // Contents of Registers 52/55 of new codec


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
#define VOFFSETALEXTXFILTREG 0                      // offset addr in IP core: TX filt, RX ant
#define VOFFSETALEXRXREG 4                          // offset addr in IP core
#define VOFFSETALEXTXANTREG 8                       // offset addr in IP core: TX filt, TX ant

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
#define VOPENCOLLECTORBITS   16                      // bits 16-22
#define VMOXBIT              24
#define VTXENABLEBIT         25
#define VDATAENDIAN          26
#define VTXRELAYDISABLEBIT   27
#define VPURESIGNALENABLE    28                      // not used by this hardware
#define VATUTUNEBIT          29
#define VXVTRENABLEBIT       30

//
// GPIO input buts
//
#define VKEYINA              2                       // dot key
#define VKEYINB              3                       // dash key
#define VUSERIO4             4
#define VUSERIO5             5
#define VUSERIO6             6
#define VUSERIO8             7
#define V13_8VDETECTBIT      8
#define VATUTUNECOMPLETEBIT  9
#define VPLLLOCKED           10
#define VCWKEYDOWN           11                     // keyer output
#define VCWKEYPRESSED        12                     // keyer request for TX active

//
// Keyer setup register defines
//
#define VCWKEYERENABLE 31                           // enable bit
#define VCWKEYERDELAY 0                             // delay bits 7:0
#define VCWKEYERHANG 8                              // hang time is 17:8
#define VCWKEYERRAMP 18                             // ramp time
#define VRAMPSIZE 4096                              // max ramp length in words

//
// Iambic config register defines
//
#define VIAMBICSPEED 0                              // speed bits 7:0
#define VIAMBICWEIGHT 8                             // weight bits 15:8
#define VIAMBICREVERSED 16                          // keys reversed bit 16
#define VIAMBICENABLE 17                            // keyer enabled bit 17
#define VIAMBICMODE 18                              // mode bit 18
#define VIAMBICSTRICT 19                            // strict spacing bit 19
#define VIAMBICCWX 20                               // CWX enable bit 20
#define VIAMBICCWXDOT 21                            // CWX dox bit 21
#define VIAMBICCWXDASH 22                           // CWX dash bit 22
#define VCWBREAKIN 23                               // breakin bit (CW not Iambic strictly!)
#define VIAMBICCWXBITS 0x00700000                   // all CWX bits
#define VIAMBICBITS 0x000FFFFF                      // all non CWX bits

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
  DACCurrentROM[0] = 0;                             // min level
  DACStepAttenROM[0] = 63;                          // max atten

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
  Register = GPIORegValue;                        // get current settings

  if (IsSwapped) {
    Register |= (1 << VDATAENDIAN);  // set bit for swapped to network order
  } else {
    Register &= ~(1 << VDATAENDIAN);  // clear bit for raspberry pi local order
  }

  GPIORegValue = Register;                        // store it back
  RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it
  pthread_mutex_unlock(&GPIOMutex);
}

//
// internal function to set the keyer on or off
// needed because keyer setting can change by message, of by TX operation
//
static void ActivateCWKeyer(bool Keyer) {
  uint32_t Register = GCWKeyerSetup;                           // get current settings

  if (Keyer) {
    Register |= (1<<VCWKEYERENABLE);
  } else {
    Register &= ~(1<<VCWKEYERENABLE);
  }

  if (Register != GCWKeyerSetup) {                    // write back if different
    GCWKeyerSetup = Register;                       // store it back
    RegisterWrite(VADDRKEYERCONFIGREG, Register);   // and write to it
  }
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
  Register = GPIORegValue;                        // get current settings
  MOXAsserted = Mox;                              // set variable

  if (Mox) {
    Register |= (1 << VMOXBIT);
  } else {
    Register &= ~(1 << VMOXBIT);
  }

  GPIORegValue = Register;                        // store it back
  RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it

  //
  // now set CW keyer if required
  //
  if (Mox) {
    ActivateCWKeyer(GCWEnabled);
  } else {        // disable keyer unless CW & breakin
    ActivateCWKeyer(GCWEnabled && GBreakinEnabled);
  }

  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetTXEnable(bool Enabled)
// sets or clears TX enable bit
// set or clear the relevant bit in GPIO
//
void SetTXEnable(bool Enabled) {
  uint32_t Register;
  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings

  if (Enabled) {
    Register |= (1 << VTXENABLEBIT);
  } else {
    Register &= ~(1 << VTXENABLEBIT);
  }

  GPIORegValue = Register;                        // store it back
  RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it
  pthread_mutex_unlock(&GPIOMutex);
}

void SetP2SampleRate(unsigned int DDC, bool Enabled, unsigned int SampleRate, bool InterleaveWithNext) {
  uint32_t RegisterValue;
  uint32_t Mask;
  ESampleRate Rate;
  Mask = 7 << (DDC * 3);                      // 3 bits in correct position

  if (!Enabled) {                                 // if not enabled, clear sample rate value & enabled flag
    P2SampleRates[DDC] = 0;
    GDDCEnabled &= ~(1 << DDC);                 // clear enable bit
    Rate = eDisabled;
  } else {
    P2SampleRates[DDC] = SampleRate;
    GDDCEnabled |= (1 << DDC);                  // set enable bit

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

  RegisterValue = DDCRateReg;                     // get current register setting
  RegisterValue &= ~Mask;                         // strip current bits
  Mask = (uint32_t)Rate;                          // new bits
  Mask = Mask << (DDC * 3);                       // get new bits to right bit position
  RegisterValue |= Mask;
  DDCRateReg = RegisterValue;                     // don't save to hardware
}

//
// bool WriteP2DDCRateRegister(void)
// writes the DDCRateRegister, once all settings have been made
// this is done so the number of changes to the DDC rates are minimised
// and the information all comes form one P2 message anyway.
// returns true if changes were made to the hardware register
//
bool WriteP2DDCRateRegister(void) {
  uint32_t CurrentValue;                          // current register setting
  bool Result = false;                            // return value
  CurrentValue = RegisterRead(VADDRDDCRATES);

  if (CurrentValue != DDCRateReg) {
    Result = true;
  }

  RegisterWrite(VADDRDDCRATES, DDCRateReg);        // and write to hardware register
  return Result;
}

//
// uint32_t GetDDCEnables(void)
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
  GPIORegValue = Register;                        // store it back
  RegisterWrite(VADDRRFGPIOREG, Register);        // and write to it
  pthread_mutex_unlock(&GPIOMutex);
}

//
// SetADCCount(unsigned int ADCCount)
// sets the number of ADCs available in the hardware.
//
void SetADCCount(unsigned int ADCCount) {
  GNumADCs = ADCCount;                            // just save the value
}

//
// SetADCOptions(EADCSelect ADC, bool Dither, bool Random);
// sets the ADC contol bits for one ADC
//
void SetADCOptions(EADCSelect ADC, bool PGA, bool Dither, bool Random) {
  uint32_t Register;                              // FPGA register content
  uint32_t RandBit = VADC1RANDBIT;                // bit number for Rand
  uint32_t PGABit = VADC1PGABIT;                  // bit number for Dither
  uint32_t DitherBit = VADC1DITHERBIT;            // bit number for Dither

  if (ADC != eADC1) {                             // for ADC2, these are all 3 bits higher
    RandBit += 3;
    PGABit += 3;
    DitherBit += 3;
  }

  pthread_mutex_lock(&GPIOMutex);
  Register = GPIORegValue;                        // get current settings
  Register &= ~(1 << RandBit);                    // strip old bits
  Register &= ~(1 << PGABit);
  Register &= ~(1 << DitherBit);

  if (PGA) {                                      // add new bits where set
    Register |= (1 << PGABit);
  }

  if (Dither) {
    Register |= (1 << DitherBit);
  }

  if (Random) {
    Register |= (1 << RandBit);
  }

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
  if (DDCDeltaPhase[DDC] != DeltaPhase) {
    DDCDeltaPhase[DDC] = DeltaPhase;
    uint32_t RegAddress = DDCRegisters[DDC];
    RegisterWrite(RegAddress, DeltaPhase);
  }
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

  DUCDeltaPhase = DeltaPhase;             // store this delta phase
  RegisterWrite(VADDRTXDUCREG, DeltaPhase);  // and write to it

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

  if (HasTXAntExplicitly && (Register != GAlexTXAntRegister)) {
    GAlexTXAntRegister = Register;
    RegisterWrite(VADDRALEXSPIREG + VOFFSETALEXTXANTREG, Register);  // and write to it
  } else if (!HasTXAntExplicitly && (Register != GAlexTXFiltRegister)) {
    GAlexTXFiltRegister = Register;
    RegisterWrite(VADDRALEXSPIREG + VOFFSETALEXTXFILTREG, Register); // and write to it
  }
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
  GTXDACCtrl = RegisterValue;
  RegisterWrite(VADDRDACCTRLREG, RegisterValue);  // and write to it
}


//
// MicLine:    true if using LineIn, false if using Microphone
// MicBoost:   true if using 20dB mic boost
// LineInGain: LineIn gain vaule
//
// MicBoost has no effect if MicLine is false
// LineInGain has no effect if MicLine is true
//
void SetCodecInputParams(bool MicLine, bool EnableBoost, int LineInGain) {
  unsigned int Reg1, Reg2;
  pthread_mutex_lock(&CodecMutex);
  switch (InstalledCodec) {
  case e23b:
    Reg1 = GCodecAnaloguePath & 0xFFFC;

    if (MicLine) { Reg1 |= 0x0004; }

    if (EnableBoost) { Reg1 |= 0x0001; }

    Reg2 = GCodecLineInGain & 0xFFE0;
    Reg2 |= (LineInGain & 0x001F);   // 5-bit value

    if(Reg1 != GCodecAnaloguePath) {
      GCodecAnaloguePath = Reg1;
      CodecRegisterWrite(4, Reg1);
    }

    if(Reg2 != GCodecLineInGain) {
      GCodecLineInGain = Reg2;
      CodecRegisterWrite(0, Reg2);
    }

    break;
  case e3204:
    if (MicLine) {
      // Route LineIn, and set gain
      Reg1 = 0xC0;
      Reg2 = 3*(LineInGain & 0x001F);  // in 0.5 dB steps, from 0 to 46.5 dB
    } else {
      // Route MicIn, set gain to 23 or 3 dB
      Reg1 = 0x04;
      Reg2 = EnableBoost? 46 : 6;
    }

    if (Reg1 != GCodec2MicPGARouting) {
      // Select Page 1, update registers 52 and 55
      GCodec2MicPGARouting = Reg1;
      CodecRegisterWrite(0x00, 0x01);
      CodecRegisterWrite(52, Reg1);
      CodecRegisterWrite(55, Reg1);
    }

    if (Reg2 != GCodec2PGA) {
      // Select Page 1, update registers 59 and 60
      GCodec2PGA = Reg2;
      CodecRegisterWrite(0x00, 0x01);
      CodecRegisterWrite(59, Reg2);
      CodecRegisterWrite(60, Reg2);
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
// SetADCAttenuator(EADCSelect ADC, unsigned int Atten, bool Enabled, bool RXAtten)
// sets the  stepped attenuator on the ADC input
// Atten provides a 5 bit atten value
// RXAtten: if true, sets atten to be used during RX
// TXAtten: if true, sets atten to be used during TX
// (it can be both!)
//
void SetADCAttenuator(EADCSelect ADC, unsigned int Atten, bool RXAtten, bool TXAtten) {
  uint32_t Register;                              // local copy
  uint32_t TXMask;
  uint32_t RXMask;
  Register = GRXADCCtrl;                          // get existing settings
  TXMask = 0b0000001111100000;                    // mask bits for TX, ADC1
  RXMask = 0b0000000000011111;                    // mask bits for RX, ADC1

  if (ADC == eADC1) {
    if (RXAtten) {
      Register &= ~RXMask;
      Register |= (Atten & 0X1F);             // add in new bits for ADC1, RX
    }

    if (TXAtten) {
      Register &= ~TXMask;
      Register |= (Atten & 0X1F) << 5;        // add in new bits for ADC1, TX
    }
  } else {
    TXMask = TXMask << 10;                      // move to ADC2 bit positions
    RXMask = RXMask << 10;                      // move to ADC2 bit positions

    if (RXAtten) {
      Register &= ~RXMask;
      Register |= (Atten & 0X1F) << 10;       // add in new bits for ADC2, RX
    }

    if (TXAtten) {
      Register &= ~TXMask;
      Register |= (Atten & 0X1F) << 15;       // add in new bits for ADC2, TX
    }
  }

  GRXADCCtrl = Register;
  RegisterWrite(VADDRADCCTRLREG, Register);      // and write to it
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
  Register = GIambicConfigReg;                    // copy of H/W register
  Register &= ~VIAMBICBITS;                       // strip off old iambic bits
  GCWKeyerSpeed = Speed;                          // just save it for now
  GCWKeyerWeight = Weight;                        // just save it for now
  GCWKeysReversed = ReverseKeys;                  // just save it for now
  GCWKeyerMode = Mode;                            // just save it for now
  GCWKeyerSpacing = StrictSpacing;
  GCWIambicKeyerEnabled = IambicEnabled;
  // set new data
  Register |= Speed;
  Register |= (Weight << VIAMBICWEIGHT);

  if (ReverseKeys) {
    Register |= (1 << VIAMBICREVERSED);  // set bit if enabled
  }

  if (Mode) {
    Register |= (1 << VIAMBICMODE);  // set bit if enabled
  }

  if (StrictSpacing) {
    Register |= (1 << VIAMBICSTRICT);  // set bit if enabled
  }

  if (IambicEnabled) {
    Register |= (1 << VIAMBICENABLE);  // set bit if enabled
  }

  if (Breakin) {
    Register |= (1 << VCWBREAKIN);  // set bit if enabled
  }

  if (Register != GIambicConfigReg) {             // save if changed
    GIambicConfigReg = Register;
    RegisterWrite(VADDRIAMBICCONFIG, Register);
  }
}

//
// void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot)
// setup CWX (host generated dot and dash)
//
void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot) {
  uint32_t Register;
  Register = GIambicConfigReg;                    // copy of H/W register
  Register &= ~VIAMBICCWXBITS;                    // strip off old CWX bits
  GCWXMode = CWXEnabled;                          // computer generated CWX mode
  GCWXDot = CWXDot;                               // computer generated CW Dot.
  GCWXDash = CWXDash;                             // computer generated CW Dash.

  if (GCWXMode) {
    Register |= (1 << VIAMBICCWX);  // set bit if enabled
  }

  if (GCWXDot) {
    Register |= (1 << VIAMBICCWXDOT);  // set bit if enabled
  }

  if (GCWXDash) {
    Register |= (1 << VIAMBICCWXDASH);  // set bit if enabled
  }

  if (Register != GIambicConfigReg) {             // save if changed
    GIambicConfigReg = Register;
    RegisterWrite(VADDRIAMBICCONFIG, Register);
  }
}

//
// SetDDCADC(int DDC, EADCSelect ADC)
// sets the ADC to be used for each DDC
// DDC = 0 to 9
// if GADCOverride is set, set to test source instead
//
void SetDDCADC(int DDC, EADCSelect ADC) {
  uint32_t Register;
  uint32_t ADCSetting;
  uint32_t Mask;

  if (GADCOverride) {
    ADC = eTestSource;  // override setting
  }

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


#define VMINCWRAMPDURATION         5000             // 5ms min
#define VMAXCWRAMPDURATION        10000             // 10ms max
#define VMAXCWRAMPDURATIONV14PLUS 20000             // 20ms max, for firmware V1.4 and later

//
// InitialiseCWKeyerRamp(bool Protocol2, uint32_t Length_us)
// calculates an "S" shape ramp curve and loads into RAM
// needs to be called before keyer enabled!
// parameter is length in microseconds; typically 1000-5000
// setup ramp memory and rampl length fields
// only calculate if parameters have changed!
//
void InitialiseCWKeyerRamp(bool Protocol2, uint32_t Length_us) {
  unsigned int FPGAVersion = 0;
  unsigned int MaxDuration;               // max ramp duration in microseconds

  if (FPGA_MinorVersion >= 14) {
    MaxDuration = VMAXCWRAMPDURATIONV14PLUS;        // get version dependent max length
  } else {
    MaxDuration = VMAXCWRAMPDURATION;
  }

  // first find out if the length is OK and clip if not
  if (Length_us < VMINCWRAMPDURATION) {
    Length_us = VMINCWRAMPDURATION;
  }

  if (Length_us > MaxDuration) {
    Length_us = MaxDuration;
  }

  // now apply that ramp length
  if ((Length_us != GCWKeyerRampms) || (Protocol2 != GCWKeyerRamp_IsP2)) {
    double SamplePeriod;                     // sample period in us
    uint32_t RampLength;                     // integer length in WORDS not bytes!
    unsigned int Cntr;
    uint32_t Register;
    GCWKeyerRampms = Length_us;
    GCWKeyerRamp_IsP2 = Protocol2;
    t_print("calculating new CW ramp, length = %ud usec\n", Length_us);

    // work out required length in samples
    if (Protocol2) {
      SamplePeriod = 1000.0 / 192.0;
    } else {
      SamplePeriod = 1000.0 / 48.0;
    }

    RampLength = (uint32_t)(((double)Length_us / SamplePeriod) + 1);

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

    for (Cntr = 0; Cntr < RampLength; Cntr++) {
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

    for (Cntr = RampLength; Cntr < VRAMPSIZE; Cntr++) {                     // fill remainder of RAM
      RegisterWrite(VADDRCWKEYERRAM + 4 * Cntr, 8388607);
    }

    //
    // finally write the ramp length
    // V14 onwards: this is in WORDS
    //
    Register = GCWKeyerSetup;                            // get current settings
    Register &= 0x8003FFFF;                              // strip out ramp bits

    if (FPGAVersion >= 14) {
      Register |= (RampLength << VCWKEYERRAMP);          // word end address
    } else {
      Register |= ((RampLength << 2) << VCWKEYERRAMP);   // byte end address
    }

    GCWKeyerSetup = Register;                            // store it back
    RegisterWrite(VADDRKEYERCONFIGREG, Register);        // and write to it
  }
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

//
// SetCWSidetoneEnabled(bool Enabled)
// enables or disables sidetone. If disabled, the volume is set to zero in codec config reg
// only do something if the bit changes; note the volume setting function is relevant too
//
void SetCWSidetoneEnabled(bool Enabled) {
  if (GSidetoneEnabled != Enabled) {                  // only act if bit changed
    GSidetoneEnabled = Enabled;
    uint32_t Register = GCodecConfigReg;                     // get current settings
    Register &= 0x0000FFFF;                         // remove old volume bits

    if (Enabled) {
      Register |= (GSidetoneVolume & 0xFF) << 24;  // add back new bits; resize to 16 bits
    }

    GCodecConfigReg = Register;                     // store it back
    RegisterWrite(VADDRCODECCONFIGREG, Register);   // and write to it
  }
}

//
// SetCWSidetoneVol(uint8_t Volume)
// sets the sidetone volume level (7 bits, unsigned)
//
void SetCWSidetoneVol(uint8_t Volume) {
  if (GSidetoneVolume != Volume) {                    // only act if value changed
    GSidetoneVolume = Volume;                       // set new value
    uint32_t Register = GCodecConfigReg;                     // get current settings
    Register &= 0x0000FFFF;                         // remove old volume bits

    if (GSidetoneEnabled) {
      Register |= (GSidetoneVolume & 0xFF) << 24;  // add back new bits; resize to 16 bits
    }

    GCodecConfigReg = Register;                     // store it back
    RegisterWrite(VADDRCODECCONFIGREG, Register);   // and write to it
  }
}

//
// SetCWPTTDelay(unsigned int Delay)
//  sets the delay (ms) before TX commences (8 bit delay value)
//
void SetCWPTTDelay(unsigned int Delay) {
  uint32_t Register;
  Register = GCWKeyerSetup;                           // get current settings
  Register &= 0xFFFFFF00;                             // remove old bits
  Register |= (Delay & 0xFF);                         // add back new bits

  if (Register != GCWKeyerSetup) {                    // write back if different
    GCWKeyerSetup = Register;                       // store it back
    RegisterWrite(VADDRKEYERCONFIGREG, Register);   // and write to it
  }
}

//
// SetCWHangTime(unsigned int HangTime)
// sets the delay (ms) after CW key released before TX removed
// (10 bit hang time value)
//
void SetCWHangTime(unsigned int HangTime) {
  uint32_t Register;
  Register = GCWKeyerSetup;                        // get current settings
  Register &= 0xFFFC00FF;                          // remove old bits
  Register |= (HangTime & 0x3FF) << VCWKEYERHANG;  // add back new bits

  if (Register != GCWKeyerSetup) {                 // write back if different
    GCWKeyerSetup = Register;                    // store it back
    RegisterWrite(VADDRKEYERCONFIGREG, Register);   // and write to it
  }
}

#define VCODECSAMPLERATE 48000                      // I2S rate
//
// SetCWSidetoneFrequency(unsigned int Frequency)
// sets the CW audio sidetone frequency, in Hz
// (12 bit value)
// DDS needs a 16 bit phase word; sample rate = 48KHz so convert accordingly
//
void SetCWSidetoneFrequency(unsigned int Frequency) {
  uint32_t Register;
  uint32_t DeltaPhase;                            // DDS delta phase value
  double fDeltaPhase;                             // delta phase as a float
  fDeltaPhase = 65536.0 * (double)Frequency / (double) VCODECSAMPLERATE;
  DeltaPhase = ((uint32_t)fDeltaPhase) & 0xFFFF;
  Register = GCodecConfigReg;                     // get current settings
  Register &= 0xFFFF0000;                             // remove old bits
  Register |= DeltaPhase;                             // add back new bits

  if (Register != GCodecConfigReg) {              // write back if different
    GCodecConfigReg = Register;                 // store it back
    RegisterWrite(VADDRCODECCONFIGREG, Register);   // and write to it
  }
}

//
// SetMinPWMWidth(unsigned int Width)
// set class E min PWM width (not yet implemented)
//
void SetMinPWMWidth(unsigned int Width) {
  GClassEPWMMin = Width;                                      // just store for now
}

//
// SetMaxPWMWidth(unsigned int Width)
// set class E min PWM width (not yet implemented)
//
void SetMaxPWMWidth(unsigned int Width) {
  GClassEPWMMax = Width;                                      // just store for now
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
// SetWidebandEnable(EADCSelect ADC, bool Enabled)
// enables wideband sample collection from an ADC.
// P2 - not yet implemented
//
void SetWidebandEnable(EADCSelect ADC, bool Enabled) {
  if (ADC == eADC1) {                     // if ADC1 save its state
    GWidebandADC1 = Enabled;
  } else if (ADC == eADC2) {              // similarly for ADC2
    GWidebandADC2 = Enabled;
  }
}

//
// SetWidebandSampleCount(unsigned int Samples)
// sets the wideband data collected count
// P2 - not yet implemented
//
void SetWidebandSampleCount(unsigned int Samples) {
  GWidebandSampleCount = Samples;
}

//
// SetWidebandSampleSize(unsigned int Bits)
// sets the sample size per packet used for wideband data transfers
// P2 - not yet implemented
//
void SetWidebandSampleSize(unsigned int Bits) {
  GWidebandSamplesPerPacket = Bits;
}

//
// SetWidebandUpdateRate(unsigned int Period_ms)
// sets the period (ms) between collections of wideband data
// P2 - not yet implemented
//
void SetWidebandUpdateRate(unsigned int Period_ms) {
  GWidebandUpdateRate = Period_ms;
}

//
// SetWidebandPacketsPerFrame(unsigned int Count)
// sets the number of packets to be transferred per wideband data frame
// P2 - not yet implemented
//
void SetWidebandPacketsPerFrame(unsigned int Count) {
  GWidebandPacketsPerFrame = Count;
}

//
// EnableTimeStamp(bool Enabled)
// enables a timestamp for RX packets
//
void EnableTimeStamp(bool Enabled) {
  GEnableTimeStamping = Enabled;                          // P2. true if enabled. NOT SUPPORTED YET
}

//
// EnableVITA49(bool Enabled)
// enables VITA49 mode
//
void EnableVITA49(bool Enabled) {
  GEnableVITA49 = Enabled;                                // P2. true if enabled. NOT SUPPORTED YET
}

//
// SetAlexEnabled(unsigned int Alex)
// 8 bit parameter enables up to 8 Alex units.
//
void SetAlexEnabled(unsigned int Alex) {
  GAlexEnabledBits = Alex;                                // just save for now.
}

//
// SetPAEnabled(bool Enabled)
// true if PA is enabled.
//
void SetPAEnabled(bool Enabled) {
  uint32_t Register;
  GPAEnabled = Enabled;                           // just save for now
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
// SetTXDACCount(unsigned int Count)
// sets the number of TX DACs, Currently unused.
//
void SetTXDACCount(unsigned int Count) {
  GTXDACCount = Count;                                    // just save for now.
}

//
// SetDUCSampleRate(ESampleRate Rate)
// sets the DUC sample rate.
// current Saturn h/w supports 48KHz for protocol 1 and 192KHz for protocol 2
//
void SetDUCSampleRate(ESampleRate Rate) {
  GDUCSampleRate = Rate;                                  // just save for now.
}

//
// SetDUCSampleSize(unsigned int Bits)
// sets the number of bits per sample.
// currently unimplemented, and protocol 2 always uses 24 bits per sample.
//
void SetDUCSampleSize(unsigned int Bits) {
  GDUCSampleSize = Bits;                                  // just save for now
}

//
// SetDUCPhaseShift(unsigned int Value)
// sets a phase shift onto the TX output. Currently unimplemented.
//
void SetDUCPhaseShift(unsigned int Value) {
  GDUCPhaseShift = Value;                                 // just save for now.
}

//
// SetSpkrMute(bool IsMuted)
// enables or disables the Codec speaker output
//
void SetSpkrMute(bool IsMuted) {
  uint32_t Register;
  GSpeakerMuted = IsMuted;                        // just save for now.
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
// SetUserOutputBits(unsigned int Bits)
// sets the user I/O bits
//
void SetUserOutputBits(unsigned int Bits) {
  GUserOutputBits = Bits;                         // just save for now
}

/////////////////////////////////////////////////////////////////////////////////
// read settings from FPGA
//

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
  GCodec2PGA = 46;                // Mic Preamp set to 23 dB
  GCodec2MicPGARouting = 0x04;    // Use Mic Input
  // Software reset
  // pg0, r1: Initialize the device through software reset; takes 1ms
  // Select Page 0
  CodecRegisterWrite(0x00, 0x00);
  CodecRegisterWrite(0x01, 0x01);
  usleep (2000);                                      // 2ms wait for reset to complete

  //
  // Clock Settings
  // The codec receives: MCLK = 12.288 MHz,
  // WCLK = 48 kHz

  //
  // pg0, r11&12: NDAC = 1, MDAC = 2
  CodecRegisterWrite(0x0B, 0x81);
  CodecRegisterWrite(0x0C, 0x82);

  //
  //pg0 r18, 19: set ADC clock = DAC clock
  CodecRegisterWrite(0x12, 0x01);
  CodecRegisterWrite(0x13, 0x02);

  // Signal Processing Settings


  //
  // pg0 r60: set the DAC Mode to PRB_P1 (LVB)
  // pg0 r61: set the ADC Mode to PRB_P1 (LVB)
  CodecRegisterWrite(0x3C, 0x01);
  CodecRegisterWrite(0x3D, 0x01);

  //
  // Initialize Codec
  //
  // Select Page 1
  CodecRegisterWrite(0x00, 0x01);

  //
  // pg1 r1: Disable weak AVDD in presence of external AVDD supply
  CodecRegisterWrite(0x01, 0x08);

  //
  // pg1 r2: Enable Master Analog Power Control (LVB)
  CodecRegisterWrite(0x02, 0x09);

  //
  // pg1 r123: Set the REF charging time to slow (LVB)
  CodecRegisterWrite(0x7B, 0x00);

  //
  // pg1 r1: disable weak AVDD in presence of external AVDD supply
  // DUPLICATE?
  CodecRegisterWrite(0x01, 0x08);

  //
  // pg1 r2: Enable Master Analog Power Control
  CodecRegisterWrite(0x02, 0x01);

  //
  // pg1 r61: Select ADC PTM_R4
  CodecRegisterWrite(0x3D, 0x00);

  // pg1 r71: Set the input powerup time to 3.1ms (for ADC)
  CodecRegisterWrite(0x47, 0x32);

  //
  // Recording Setup
  //
  // Select Page 1
  CodecRegisterWrite(0x00, 0x01);

  //
  // pg1 r58: enable analogue inputs
  CodecRegisterWrite(0x3A, 0x30);

  //
  // pg1 r52: Route IN1L, R and IN3 L,R
  // pg1 r55: Route IN1R to RIGHT_P
  CodecRegisterWrite(52, GCodec2MicPGARouting);
  CodecRegisterWrite(55, GCodec2MicPGARouting);

  //
  // pg1 r54: Route Common Mode to LEFT_M
  // pg1 r57: Route Common Mode to RIGHT_M
  CodecRegisterWrite(0x36, 0x40);
  CodecRegisterWrite(0x39, 0x40);

  //
  // pg1 r71: input powerup time
  CodecRegisterWrite(0x47, 0x32);

  //
  // pg1 r59: Unmute Left MICPGA, Gain selection of 23dB
  // pg1 r60: Unmute Right MICPGA, Gain selection of 23dB
  CodecRegisterWrite(59, GCodec2PGA);
  CodecRegisterWrite(60, GCodec2PGA);

  //
  // pg1 r51: mic bias
  CodecRegisterWrite(0x33, 0x68);

  //
  // Select Page 0
  CodecRegisterWrite(0x00, 0x00);

  //
  // pg0 r81: Power up LADC/RADC
  CodecRegisterWrite(0x51, 0xC0);

  //
  // pg0 r82: Unmute LADC/RADC
  CodecRegisterWrite(0x52, 0x00);

  //
  // Playback Setup
  //

  //
  // Select Page 1
  CodecRegisterWrite(0x00, 0x01);

  //
  // Anti-thump step 1.
  // pg1 r20: De-pop. 6K, 5 time constants -> 300ms; add 100ms soft routing.
  CodecRegisterWrite(0x14, 0x65);

  //
  // anti-thump step 2.
  // pg1 r10: common mode
  CodecRegisterWrite(0x0A, 0x3B);

  //
  // anti-thump step 3.
  // pg1 r12, 13: Route LDAC/RDAC to HPL/HPR
  CodecRegisterWrite(0x0C, 0x08);
  CodecRegisterWrite(0x0D, 0x08);

  //
  // pg1 r14, 15: Route LDAC/RDAC to LOL/LOR
  CodecRegisterWrite(0x0E, 0x08);
  CodecRegisterWrite(0x0F, 0x08);

  // before anti-thump step 4:
  // pg1 r22, 23: in1 to headphone bypass: MUTE
  CodecRegisterWrite(0x16, 0x72);
  CodecRegisterWrite(0x17, 0x72);

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
  CodecRegisterWrite(0x10, 0x00);
  CodecRegisterWrite(0x11, 0x00);

  //
  // anti-thump step 6:
  // pg1 r9: Power up HPL/HPR and LOL/LOR drivers (LVB)
  CodecRegisterWrite(0x09, 0x3F);

  //
  // pg1 r18, 19: Unmute LOL/LOR driver, 0dB Gain
  CodecRegisterWrite(0x12, 0x00);
  CodecRegisterWrite(0x13, 0x00);

  //
  // Select Page 0
  CodecRegisterWrite(0x00, 0x00);

  //
  // pg0 r65, 66: DAC => 0dB
  CodecRegisterWrite(0x41, 0x00);
  CodecRegisterWrite(0x42, 0x00);

  //
  // anti-thump step 7: AFTER 300ms DELAY for ramp-up
  usleep(300000);
  // pg0 r64: Unmute LDAC/RDAC
  CodecRegisterWrite(0x40, 0x00);

  pthread_mutex_unlock(&CodecMutex);
}

static void InitialiseTLV320AIC23B(void) {
  pthread_mutex_lock(&CodecMutex);
  GCodecLineInGain = 0;                                   // Codec left line in gain register
  GCodecAnaloguePath = 0x14;                              // mic input, no boost
  CodecRegisterWrite(15, 0x0);                            // reset register: reset deveice
  usleep(100);
  CodecRegisterWrite(9, 0x1);                             // digital activation set to ACTIVE
  usleep(100);
  CodecRegisterWrite(4, GCodecAnaloguePath);
  usleep(100);
  CodecRegisterWrite(6, 0x0);                             // all elements powered on
  usleep(100);
  CodecRegisterWrite(7, 0x2);                             // slave; no swap; right when LRC high; 16 bit, I2S
  usleep(100);
  CodecRegisterWrite(8, 0x0);                             // no clock divide; rate ctrl=0; normal mode, oversample 256Fs
  usleep(100);
  CodecRegisterWrite(5, 0x0);                             // no soft mute; no deemphasis; ADC high pass filter enabled
  usleep(100);
  CodecRegisterWrite(0, GCodecLineInGain);                // line in gain=0
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
// SetTXProtocol (bool Protocol)
// sets whether TX configured for P1 (48KHz) or P2 (192KHz)
// true for P2
void SetTXProtocol (bool Protocol) {
  uint32_t Register;
  pthread_mutex_lock(&TXConfigMutex);
  Register = TXConfigRegValue;                        // get current settings
  Register &= 0xFFFFFF7;                              // remove old bit
  Register |= ((((unsigned int)Protocol) & 1) << VTXCONFIGPROTOCOLBIT);          // add new bit

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

  pthread_mutex_lock(&TXConfigMutex);
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

//
// SetTXModulationTestSourceFrequency (unsigned int Freq)
// sets the TX modulation DDS source frequency. Only used for development.
//
void SetTXModulationTestSourceFrequency (unsigned int Freq) {
  uint32_t Register;
  Register = Freq;                        // get current settings

  if (Register != TXModulationTestReg) {                 // write back if different
    TXModulationTestReg = Register;                    // store it back
    RegisterWrite(VADDRTXMODTESTREG, Register);  // and write to it
  }
}

//
// SetTXModulationSource(ETXModulationSource Source)
// selects the modulation source for the TX chain.
// this will need to be called operationally to change over between CW & I/Q
//
void SetTXModulationSource(ETXModulationSource Source) {
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
