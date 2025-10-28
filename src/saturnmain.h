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
// saturnmain.h:
// Saturn interface to PiHPSDR
//
//////////////////////////////////////////////////////////////

#ifndef __saturnmain_h
#define __saturnmain_h

#include "saturnregisters.h"

void saturn_discovery(void);
void saturn_init(void);
void saturn_register_init(void);
void saturn_handle_speaker_audio(const unsigned char *UDPInBuffer);
void saturn_handle_high_priority(bool FromNetwork, const unsigned char *high_priority_buffer_to_radio);
void saturn_handle_general_packet(bool FromNetwork, const unsigned char *PacketBuffer);
void saturn_handle_ddc_specific(bool FromNetwork, const unsigned char *receive_specific_buffer);
void saturn_handle_duc_specific(bool FromNetwork, const unsigned char *transmit_specific_buffer);
void saturn_handle_duc_iq(bool FromNetwork, unsigned char *UDPInBuffer);
void saturn_free_buffers(void);
void saturn_exit(void);

int saturn_minor_version_min(void);
int saturn_minor_version_max(void);
int saturn_major_version_min(void);
int saturn_major_version_max(void);
#endif
