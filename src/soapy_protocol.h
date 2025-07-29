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
#include <SoapySDR/Device.h>

#include "receiver.h"
#include "transmitter.h"

SoapySDRDevice *get_soapy_device(void);

void soapy_protocol_create_single_receiver(RECEIVER *rx);
void soapy_protocol_create_dual_receiver(RECEIVER *rx1, RECEIVER *rx2);
void soapy_protocol_start_single_receiver(RECEIVER *rx);
void soapy_protocol_start_dual_receiver(RECEIVER *rx1, RECEIVER *rx2);

void soapy_protocol_init(gboolean hf);
void soapy_protocol_stop(void);
void soapy_protocol_stop_receivers(void);
void soapy_protocol_set_rx_frequency(int id);
void soapy_protocol_set_rx_antenna(int channel, int ant);
void soapy_protocol_set_lna_gain(RECEIVER *rx, int gain);
void soapy_protocol_set_rx_gain(int id);
void soapy_protocol_rx_attenuate(int id);
void soapy_protocol_rx_unattenuate(int id);
void soapy_protocol_set_rx_gain_element(int id, char *name, double gain);
double soapy_protocol_get_rx_gain_element(int id, char *name);
void soapy_protocol_change_rx_sample_rate(RECEIVER *rx);
gboolean soapy_protocol_get_automatic_gain(int id);
void soapy_protocol_set_automatic_gain(int id, gboolean mode);
void soapy_protocol_create_transmitter(const TRANSMITTER *tx);
void soapy_protocol_start_transmitter(void);
void soapy_protocol_stop_transmitter(void);
void soapy_protocol_set_tx_frequency(void);
void soapy_protocol_set_tx_antenna(int ant);
void soapy_protocol_set_tx_gain(double gain);
void soapy_protocol_set_tx_gain_element(char *name, double gain);
double soapy_protocol_get_tx_gain_element(char *name);
void soapy_protocol_iq_samples(float isample, float qsample);
