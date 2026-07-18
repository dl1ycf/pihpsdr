/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
* 2024,2025 - Heiko Amft, DL1BZ (from project deskHPSDR)
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

#include "receiver.h"

extern int tci_enable;
extern int tci_port;   // usually 40001
extern int tci_audio_rx_active;
extern int tci_audio_tx_active;

extern int launch_tci(void);
extern void shutdown_tci(void);
extern void tci_tx_chrono_loop(void);
