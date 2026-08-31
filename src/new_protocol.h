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

#ifndef _NEW_PROTOCOL_H_
#define _NEW_PROTOCOL_H_

#include "MacOS.h"   // for semaphores
#include "buffer.h"
#include "receiver.h"

// port definitions from host
#define GENERAL_REGISTERS_FROM_HOST_PORT              1024
#define PROGRAMMING_FROM_HOST_PORT                    1024
#define RECEIVER_SPECIFIC_REGISTERS_FROM_HOST_PORT    1025
#define TRANSMITTER_SPECIFIC_REGISTERS_FROM_HOST_PORT 1026
#define HIGH_PRIORITY_FROM_HOST_PORT                  1027
#define AUDIO_FROM_HOST_PORT                          1028
#define TX_IQ_FROM_HOST_PORT                          1029

// port definitions to host
#define COMMAND_RESPONSE_TO_HOST_PORT                 1024
#define HIGH_PRIORITY_TO_HOST_PORT                    1025
#define MIC_LINE_TO_HOST_PORT                         1026
#define WIDE_BAND_TO_HOST_PORT                        1027
#define RX_IQ_TO_HOST_PORT_0                          1035
#define RX_IQ_TO_HOST_PORT_1                          1036
#define RX_IQ_TO_HOST_PORT_2                          1037
#define RX_IQ_TO_HOST_PORT_3                          1038

extern void schedule_high_priority(void);
extern void schedule_general(void);
extern void schedule_receive_specific(void);
extern void schedule_transmit_specific(void);

extern void new_protocol_init(void);
extern void new_protocol_audio_samples(double left, double right);
extern void new_protocol_iq_samples(double isample, double qsample);
extern void new_protocol_tx_audio_samples(double sample);
extern void new_protocol_menu_start(void);
extern void new_protocol_menu_stop(void);

extern void saturn_post_iq_data(int ddc, p2buffer *buffer);
extern void saturn_post_micaudio(p2buffer *buffer);
extern void saturn_post_high_priority(p2buffer *buffer);

//
// if DUMP_TX_DATA is #defined, the first 1000000 samples
// after a RXTX transition are dumped to a file at the
// next TXRX transition. The value of DUMP_TX_DATA
// allows to dump either the TX IQ data sent to the radio,
// or the RX or TX feedback data. PURESIGNAL must be enabled
// to use TXFDBK or RXFDBK.
// (This is only implemented for P2).
//
#define DUMP_TXIQ   1
#define DUMP_TXFDBK 2
#define DUMP_RXFDBK 3

//#define DUMP_TX_DATA DUMP_TXIQ

#ifdef DUMP_TX_DATA
  extern int rxiq_count;
  extern double rxiqi[];
  extern double rxiqq[];
#endif

#endif
