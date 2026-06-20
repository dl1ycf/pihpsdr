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

#ifndef _AGC_H_
#define _AGC_H_

enum _agc_enum {
  AGC_OFF = 0,   // AGC off with fixed gain 0dB
  AGC_LONG,      // Attack=2ms, Hang=2000ms, Decay=2000ms, slope=0.35, hang_threshold from UI
  AGC_SLOW,      // Attack=2ms, Hang=1000ms, Decay=500ms,  slope=0.35, hang_threshold from UI
  AGC_MEDIUM,    // Attack=2ms, Hang=0ms,    Decay=250ms,  slope=0.35, hang_threshold 100ms
  AGC_FAST,      // Attack=2ms, Hang=0ms,    Decay=50ms,   slope=0.35, hang_threshold 100ms
  AGC_CUSTOM,    // Attack, Hang, Decay, Slope, hang_threshold from UI
  AGC_FIXED,     // AGC off with fixed gain from AGC slider
  AGC_LAST
};

#endif
