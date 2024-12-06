/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#ifndef _VERSION_H
#define _VERSION_H

extern char build_version[];
extern char build_date[];
extern char build_commit[];
extern char build_options[];
extern char build_audio[];

void version_info_print(char * cmdlp);

//
// For "minor" versions up to 17, there is no "major" one.
// For "minor" version 18,  the "major" version is 1
// With each firmware update, the "minor" version is increased (it is not reset upon advancinc the major)
// The "major" version is increased if piHPSDR compatibility is broken
//
#define FIRMWARE_MIN_MINOR    8 // Minimum FPGA "minor" software version that this software requires
#define FIRMWARE_MAX_MINOR   18 // Maximum FPGA "minor" software version that this software is tested on
#define FIRMWARE_MIN_MAJOR    1 // Minimum FPGA "major" software version that this software requires
#define FIRMWARE_MAX_MAJOR    1 // Minimum FPGA "major" software version that this software requires

#endif
