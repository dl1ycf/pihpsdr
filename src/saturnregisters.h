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
// saturnregisters.h:
// Hardware access to FPGA registers in the Saturn FPGA
//  at the level of "set TX frequency" or set DDC frequency"
//
//////////////////////////////////////////////////////////////

#ifndef __saturnregisters_h
#define __saturnregisters_h

#include <stdint.h>
#include <stdbool.h>

#define VNUMDDC 10                      // # DDCs (downconverters) available
#define VDDCPACKETSIZE 1444             // DDC I/Q packet length (in Bytes)

//
// protocol 2 handler for outgoing DDC I/Q data Packet from SDR
//
void *OutgoingDDCIQ(void *arg);

//
// interface calls to get commands from PC settings
//


//
// 8 bit Codec register read/write over the AXILite bus via SPI
//
void CodecRegisterWrite(uint32_t Address, uint32_t Data);
uint8_t CodecRegisterRead(uint32_t Address);

//
// open/close connection to the XDMA device driver for register and DMA access
//
int OpenXDMADriver(void);
void CloseXDMADriver(void);

//
// initiate a DMA to the FPGA with specified parameters
// returns 1 if success, else 0
// fd: file device (an open file)
// SrcData: pointer to memory block to transfer
// Length: number of bytes to copy
// AXIAddr: offset address in the FPGA window
//
int DMAWriteToFPGA(int fd, unsigned char*SrcData, uint32_t Length, uint32_t AXIAddr);

//
// initiate a DMA from the FPGA with specified parameters
// returns 1 if success, else 0
// fd: file device (an open file)
// DestData: pointer to memory block to transfer
// Length: number of bytes to copy
// AXIAddr: offset address in the FPGA window
//
int DMAReadFromFPGA(int fd, unsigned char*DestData, uint32_t Length, uint32_t AXIAddr);

//
// single 32 bit register read/write, from/to AXI-Lite bus
//
uint32_t RegisterRead(uint32_t Address);
void RegisterWrite(uint32_t Address, uint32_t Data);

//
// enum type for sample rate. only 48-384KHz allowed for protocol 1
//
typedef enum {
  eDisabled,
  e48KHz,
  e96KHz,
  e192KHz,
  e384KHz,
  e768KHz,
  e1536KHz,
  eInterleaveWithNext
} ESampleRate;

//
// enum type for ADC selection
//
typedef enum {
  eADC1,                        // selects ADC1
  eADC2,                        // selects ADC2
  eTestSource,                  // selects internal test source (not for operational use)
  eTXSamples                    // (for Puresignal)
} EADCSelect;

//
// enum for TX modulation source
//
typedef enum {
  eIQData,
  eFixed0Hz,
  eTXDDS,
  eCWKeyer
} ETXModulationSource;

//
// DMA channel allocations
//
#define VMICDMADEVICE "/dev/xdma0_c2h_1"
#define VDDCDMADEVICE "/dev/xdma0_c2h_0"
#define VSPKDMADEVICE "/dev/xdma0_h2c_1"
#define VDUCDMADEVICE "/dev/xdma0_h2c_0"

//
// FPGA register map
//
#define VADDRDDC0REG          0x00000
#define VADDRDDC1REG          0x00004
#define VADDRDDC2REG          0x00008
#define VADDRDDC3REG          0x0000C
#define VADDRDDC4REG          0x00010
#define VADDRDDC5REG          0x00014
#define VADDRDDC6REG          0x00018
#define VADDRDDC7REG          0x0001C
#define VADDRDDC8REG          0x01000
#define VADDRDDC9REG          0x01004
#define VADDRRXTESTDDSREG     0x01008
#define VADDRDDCRATES         0x0100C
#define VADDRDDCINSEL         0x01010
#define VADDRKEYERCONFIGREG   0x02000
#define VADDRCODECCONFIGREG   0x02004
#define VADDRTXCONFIGREG      0x02008
#define VADDRTXDUCREG         0x0200C
#define VADDRTXMODTESTREG     0x02010
#define VADDRRFGPIOREG        0x02014
#define VADDRADCCTRLREG       0x02018
#define VADDRDACCTRLREG       0x0201C
#define VADDRDEBUGLEDREG      0x03000
#define VADDRSTATUSREG        0x04000
#define VADDRDATECODE         0x04004
#define VADDRADCOVERFLOWBASE  0x05000
#define VADDRFIFOOVERFLOWBASE 0x06000
#define VADDRFIFORESET        0x07000
#define VADDRIAMBICCONFIG     0X07004
#define VADDRFIFOMONBASE      0x09000
#define VADDRALEXADCBASE      0x0A000
#define VADDRALEXSPIREG       0x0B000
#define VADDRBOARDID1         0x0C000
#define VADDRBOARDID2         0x0C004
#define VADDRCONFIGSPIREG     0x10000
#define VADDRCODECSPIWRITEREG 0x14000            // Write to Codec
#define VADDRCODECSPIREADREG  0x14004            // Read from Codec
#define VADDRXADCREG          0x18000            // on-chip XADC (temp, VCC...)
#define VADDRCWKEYERRAM       0x1C000            // keyer RAM mapped here

#define VNUMDMAFIFO           4                  // DMA streams available
#define VADDRDDCSTREAMREAD    0x0L               // stream reader/writer on AXI-4 bus
#define VADDRDUCSTREAMWRITE   0x0L               // stream reader/writer on AXI-4 bus
#define VADDRMICSTREAMREAD    0x40000L           // stream reader/writer on AXI-4 bus
#define VADDRSPKRSTREAMWRITE  0x40000L           // stream reader/writer on AXI-4 bus

#define VBITCODECMICFIFORESET 0         // reset bit in register
#define VBITCODECSPKFIFORESET 1         // reset bit in register
#define VBITDDCFIFORESET      2         // reset bit in register
#define VBITDUCFIFORESET      3         // reset bit in register

// InitialiseCWKeyerRamp(bool Protocol2, uint32_t Length_us)
// calculates an "S" shape ramp curve and loads into RAM
// needs to be called before keyer enabled!
// parameter is length in microseconds; typically 1000-5000
//
void InitialiseCWKeyerRamp(bool Protocol2, uint32_t Length_us);

//
// initialise the DAC Atten ROMs
// these set the step attenuator and DAC drive level
// for "attenuation intent" values from 0 to 255
//
void InitialiseDACAttenROMs(void);

//
// SetByteSwap(bool)
// set whether byte swapping is enabled. True if yes, to get data in network byte order.
//
void SetByteSwapping(bool IsSwapped);

//
// SetMOX(bool Mox)
// sets or clears TX state
//
void SetMOX(bool Mox);

//
// SetTXEnable(bool Enabled)
// sets or clears TX enable bit
// set or clear the relevant bit in GPIO
//
void SetTXEnable(bool Enabled);

//
// SetP2SampleRate(unsigned int DDC, bool Enabled, unsigned int SampleRate, bool InterleaveWithNext)
// sets the sample rate for a single DDC (used in protocol 2)
// allowed rates are 48KHz to 1536KHz.
// This sets the DDCRateReg variable and does NOT write to hardware
// The WriteP2DDCRateRegister() call must be made after setting values for all DDCs
//
void SetP2SampleRate(unsigned int DDC, bool Enabled, unsigned int SampleRate, bool InterleaveWithNext);

//
// bool WriteP2DDCRateRegister(void)
// writes the DDCRateRegister, once all settings have been made
// this is done so the number of changes to the DDC rates are minimised
// and the information all comes form one P2 message anyway.
// returns true if changes were made to the hardware register
//
bool WriteP2DDCRateRegister(void);

//
// uint32_t GetDDCEnables(void)
// get enable bits for each DDC; 1 bit per DDC
// this is needed to set timings and sizes for DMA transfers
//
uint32_t GetDDCEnables(void);

//
// SetOpenCollectorOutputs(unsigned int bits)
// sets the 7 open collector output bits
// data must be provided in bits 0-6
//
void SetOpenCollectorOutputs(unsigned int bits);

//
// SetADCCount(unsigned int ADCCount)
// sets the number of ADCs available in the hardware.
//
void SetADCCount(unsigned int ADCCount);

//
// SetADCOptions(EADCSelect ADC, bool Dither, bool Random);
// sets the ADC contol bits for one ADC
//
void SetADCOptions(EADCSelect ADC, bool PGA, bool Dither, bool Random);

//
// SetDDCFrequency(unsigned int DDC, unsigned int Value, bool IsDeltaPhase)
// sets a DDC frequency.
// DDC: DDC number (0-9)
// Value: 32 bit phase word or frequency word (1Hz resolution)
// IsDeltaPhase: true if a delta phase value, false if a frequency value (P1)
//
void SetDDCFrequency(uint32_t DDC, uint32_t Value, bool IsDeltaPhase);

//
// SetDUCFrequency(unsigned int Value, bool IsDeltaPhase)
// sets a DUC frequency. (Currently only 1 DUC, therefore DUC must be 0)
// Value: 32 bit phase word or frequency word (1Hz resolution)
// IsDeltaPhase: true if a delta phase value, false if a frequency value (P1)
//
void SetDUCFrequency(unsigned int Value, bool IsDeltaPhase);    // only accepts DUC=0

//
// AlexManualRXFilters(unsigned int Bits, int RX)
// P2: provides a 16 bit word with all of the Alex settings for a single RX
// must be formatted according to the Alex specification
// RX=0 or 1: RX1; RX=2: RX2
//
void AlexManualRXFilters(unsigned int Bits, int RX);

//
// AlexManualTXFilters(unsigned int Bits)
// P2: provides a 16 bit word with all of the Alex settings for TX
// must be formatted according to the Alex specification
// FPGA V12 onwards: uses an additional regoster with TX ant settings
// HasTXAntExplicitly true if data is for the new TXfilter, TX ant register
//
void AlexManualTXFilters(unsigned int Bits, bool HasTXAntExplicitly);

//
// SetTXDriveLevel(unsigned int Level)
// sets the TX DAC current via a PWM DAC output
// level: 0 to 255 drive level value (255 = max current)
//
void SetTXDriveLevel(unsigned int Level);

//
// SetCodecInputParams((bool MicLine, bool EnableBoost, int LineInGain)
// configures audio codec input characteristics:
// MicLine: if true, use LineIn, else use MicIn
// EnableBoost:  when using MicIn, enable extra 20dB amplification
// LineInGain:   when using LineIn, apply gain
//               LineInGain 0...31 maps to -12...34.5 dB in 1.5-db-steps
//
void SetCodecInputParams(bool MicLine, bool EnableBoost, int LineInGain);

//
// SetOrionMicOptions(bool MicRing, bool EnableBias, bool EnablePTT)
// sets the microphone control inputs
//
void SetOrionMicOptions(bool MicRing, bool EnableBias, bool EnablePTT);

//
// SetBalancedMicInput(bool Balanced)
// selects the balanced microphone input, not supported by current protocol code.
//
void SetBalancedMicInput(bool Balanced);

//
// SetADCAttenuator(EADCSelect ADC, unsigned int Atten, bool Enabled, bool RXAtten)
// sets the  stepped attenuator on the ADC input
// Atten provides a 5 bit atten value
// RXAtten: if true, sets atten to be used during RX
// TXAtten: if true, sets atten to be used during TX
// (it can be both!)
//
void SetADCAttenuator(EADCSelect ADC, unsigned int Atten, bool RXAtten, bool TXAtten);

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
                      bool StrictSpacing, bool IambicEnabled, bool Breakin);

//
// void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot)
// setup CWX (host generated dot and dash)
//
void SetCWXBits(bool CWXEnabled, bool CWXDash, bool CWXDot);

//
// SetDDCADC(int DDC, EADCSelect ADC)
// sets the ADC to be used for each DDC
// DDC = 0 to 9
//
void SetDDCADC(int DDC, EADCSelect ADC);

//
// void SetRXDDCEnabled(bool IsEnabled);
// sets enable bit so DDC operates normally. Resets input FIFO when starting.
//
void SetRXDDCEnabled(bool IsEnabled);

//
// EnableCW (bool Enabled, bool Breakin)
// enables or disables CW mode; selects CW as modulation source.
// If Breakin enabled, the key input engages TX automatically
// and generates sidetone.
//
void EnableCW (bool Enabled, bool Breakin);

//
// SetCWSidetoneVol(uint8_t Volume)
// sets the sidetone volume level (7 bits, unsigned)
//
void SetCWSidetoneVol(uint8_t Volume);

//
// SetCWPTTDelay(unsigned int Delay)
//  sets the delay (ms) before TX commences (8 bit delay value)
//
void SetCWPTTDelay(unsigned int Delay);

//
// SetCWHangTime(unsigned int HangTime)
// sets the delay (ms) after CW key released before TX removed
// (10 bit hang time value)
//
void SetCWHangTime(unsigned int HangTime);

//
// SetCWSidetoneFrequency(unsigned int Frequency)
// sets the CW audio sidetone frequency, in Hz
// (12 bit value)
//
void SetCWSidetoneFrequency(unsigned int Frequency);

//
// SetCWSidetoneEnabled(bool Enabled)
// enables or disables sidetone. If disabled, the volume is set to zero
//
void SetCWSidetoneEnabled(bool Enabled);

//
// SetMinPWMWidth(unsigned int Width)
// set class E min PWM width (not yet implemented)
//
void SetMinPWMWidth(unsigned int Width);

//
// SetMaxPWMWidth(unsigned int Width)
// set class E min PWM width (not yet implemented)
//
void SetMaxPWMWidth(unsigned int Width);

//
// SetXvtrEnable(bool Enabled)
// enables or disables transverter. If enabled, the PA is not keyed.
//
void SetXvtrEnable(bool Enabled);

//
// SetWidebandEnable(EADCSelect ADC, bool Enabled)
// enables wideband sample collection from an ADC.
//
void SetWidebandEnable(EADCSelect ADC, bool Enabled);

//
// SetWidebandSampleCount(unsigned int Samples)
// sets the wideband data collected count
//
void SetWidebandSampleCount(unsigned int Samples);

//
// SetWidebandSampleSize(unsigned int Bits)
// sets the sample size per packet used for wideband data transfers
//
void SetWidebandSampleSize(unsigned int Bits);

//
// SetWidebandUpdateRate(unsigned int Period_ms)
// sets the period (ms) between collections of wideband data
//
void SetWidebandUpdateRate(unsigned int Period_ms);

//
// SetWidebandPacketsPerFrame(unsigned int Count)
// sets the number of packets to be transferred per wideband data frame
//
void SetWidebandPacketsPerFrame(unsigned int Count);

//
// EnableTimeStamp(bool Enabled)
// enables a timestamp for RX packets
//
void EnableTimeStamp(bool Enabled);

//
// EnableVITA49(bool Enabled)
// enables VITA49 mode
//
void EnableVITA49(bool Enabled);

//
// SetAlexEnabled(unsigned int Alex)
// 8 bit parameter enables up to 8 Alex units.
// numbered 0 to 7
//
void SetAlexEnabled(unsigned int Alex);

//
// SetPAEnabled(bool Enabled)
// true if PA is enabled.
//
void SetPAEnabled(bool Enabled);

//
// SetTXDACCount(unsigned int Count)
// sets the number of TX DACs, Currently unused.
//
void SetTXDACCount(unsigned int Count);

//
// SetDUCSampleRate(ESampleRate Rate)
// sets the DUC sample rate.
// current Saturn h/w supports 48KHz for protocol 1 and 192KHz for protocol 2
//
void SetDUCSampleRate(ESampleRate Rate);

//
// SetDUCSampleSize(unsigned int Bits)
// sets the number of bits per sample.
// currently unimplemented, and protocol 2 always uses 24 bits per sample.
//
void SetDUCSampleSize(unsigned int Bits);

//
// SetDUCPhaseShift(unsigned int Value)
// sets a phase shift onto the TX output. Currently unimplemented.
//
void SetDUCPhaseShift(unsigned int Value);

//
// SetSpkrMute(bool IsMuted)
// enables or disables the Codec speaker output
//
void SetSpkrMute(bool IsMuted);

//
// SetUserOutputBits(unsigned int Bits)
// sets the user I/O bits
//
void SetUserOutputBits(unsigned int Bits);

/////////////////////////////////////////////////////////////////////////////////
// read settings from FPGA
//

//
// ReadStatusRegister(void)
// this is a precursor to getting any of the data itself; simply reads the register to a local variable
// probably call every time an outgoig packet is put together initially
// but possibly do this one a timed basis.
//
void ReadStatusRegister(void);

//
// GetPTTInput(void)
// return true if PTT input is pressed.
//
bool GetPTTInput(void);

//
// GetKeyerDashInput(void)
// return true if keyer dash input is pressed.
//
bool GetKeyerDashInput(void);

//
// GetKeyerDotInput(void)
// return true if keyer dot input is pressed.
//
bool GetKeyerDotInput(void);

//
// GetCWKeyDown(void)
// return true if keyer has initiated TX.
// depends on the status register having been read before this is called!
//
bool GetCWKeyDown(void);

//
// GetP2PTTKeyInputs(void)
// return several bits from Saturn status register:
// bit 0 - true if PTT active
// bit 1 - true if CW dot input active
// bit 2 - true if CW dash input active
// bit 4 - true if 10MHz to 122MHz PLL is locked
//
unsigned int GetP2PTTKeyInputs(void);

//
// GetADCOverflow(void)
// return true if ADC overflow has occurred since last read.
// the overflow stored state is reset when this is read.
// returns bit0: 1 if ADC1 overflow; bit1: 1 if ADC2 overflow
//
unsigned int GetADCOverflow(void);

//
// GetUserIOBits(void)
// return the user input bits
//
unsigned int GetUserIOBits(void);

//
// unsigned int GetAnalogueIn(unsigned int AnalogueSelect)
// return one of 6 ADC values from the RF board analogue values
// the paramter selects which input is read.
//
unsigned int GetAnalogueIn(unsigned int AnalogueSelect);

//////////////////////////////////////////////////////////////////////////////////
// internal App register settings
// these are things not accessible from external SDR applications, including debug
//

//
// CodecInitialise()
// initialise the CODEC, with the register values that don't normally change
//
void CodecInitialise();

//
// SetTXAmplitudeScaling (unsigned int Amplitude)
// sets the overall TX amplitude. This is normally set to a constant determined during development.
//
void SetTXAmplitudeScaling (unsigned int Amplitude);

//
// SetTXModulationTestSourceFrequency (unsigned int Freq)
// sets the TX modulation DDS source frequency. Only used for development.
//
void SetTXModulationTestSourceFrequency (unsigned int Freq);

//
// SetTXModulationSource(ETXModulationSource Source)
// selects the modulation source for the TX chain.
// this will need to be called operationally to change over between CW & I/Q
//
void SetTXModulationSource(ETXModulationSource Source);

//
// SetTXProtocol (bool Protocol)
// sets whether TX configured for P1 (48KHz) or P2 (192KHz)
//
void SetTXProtocol (bool Protocol);

//
// void ResetDUCMux(void)
//
void ResetDUCMux(void);

//
// void SetTXOutputGate(bool AlwaysOn)
// sets the sample output gater. If false, samples gated by TX strobe.
// if true, samples are alweays enabled.
//
void SetTXOutputGate(bool AlwaysOn);

//
// void SetTXIQDeinterleaved(bool Interleaved)
// if true, put DUC hardware in EER mode
// this must only be called by the TX datas handler
// as it needs ot reset the DUC mux at the same time
//
void SetTXIQDeinterleaved(bool Interleaved);

//
// void EnableDUCMux(bool Enabled)
// enabled the multiplexer to take samples from FIFO and hand on to DUC
// // needs to be stoppable if there is an error condition
//
void EnableDUCMux(bool Enabled);

#endif
