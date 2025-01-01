/* Copyright (C)
* 2021 - Laurence Barker G8NJJ
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
// saturnmain.h:
// Saturn interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#ifndef __saturnmain_h
#define __saturnmain_h

#include "saturnregisters.h"

#define SDRBOARDID 1                          // Hermes
#define SDRSWVERSION 1                        // version of this software
#define VDISCOVERYSIZE 60                     // discovery packet
#define VDISCOVERYREPLYSIZE 60                // reply packet
#define VWIDEBANDSIZE 1028                    // wideband scalar samples
#define VCONSTTXAMPLSCALEFACTOR 0x0001FFFF    // 18 bit scale value - set to 1/2 of full scale
#define VCONSTTXAMPLSCALEFACTOR_13 0x0002000  // 18 bit scale value - set to 1/32 of full scale FWV13+
#define VDMATRANSFERSIZE 4096
#define VDMABUFFERSIZE 131072                 // memory buffer to reserve (4x DDC FIFO so OK)
#define VALIGNMENT 4096                       // buffer alignment
#define VBASE 0x1000                          // offset into I/Q buffer for DMA to start
#define VIQSAMPLESPERFRAME 238
#define VIQBYTESPERFRAME 6*VIQSAMPLESPERFRAME // total bytes in one outgoing frame
#define VIQDUCSAMPLESPERFRAME 240

#define VSPKSAMPLESPERFRAME 64                // samples per UDP frame
#define VMEMWORDSPERFRAME 32                  // 8 byte writes per UDP msg
#define VSPKSAMPLESPERMEMWORD 2               // 2 samples (each 4 bytres) per 8 byte word
#define VDMASPKBUFFERSIZE 32768               // memory buffer to reserve
#define VDMASPKTRANSFERSIZE 256               // write 1 message at a time

#define VMICSAMPLESPERFRAME 64
#define VDMAMICBUFFERSIZE 32768           // memory buffer to reserve
#define VDMAMICTRANSFERSIZE 128                        // read 1 message at a time
#define VMICPACKETSIZE 132

void saturn_discovery(void);
void saturn_init(void);
void saturn_register_init(void);
void saturn_handle_speaker_audio(const uint8_t *UDPInBuffer);
void saturn_handle_high_priority(bool FromNetwork, unsigned char *high_priority_buffer_to_radio);
void saturn_handle_general_packet(bool FromNetwork, uint8_t *PacketBuffer);
void saturn_handle_ddc_specific(bool FromNetwork, unsigned char *receive_specific_buffer);
void saturn_handle_duc_specific(bool FromNetwork, unsigned char *transmit_specific_buffer);
void saturn_handle_duc_iq(bool FromNetwork, uint8_t *UDPInBuffer);
void saturn_free_buffers(void);
void saturn_exit(void);

#endif
