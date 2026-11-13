/*  utilities.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2019, 2024 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

warren@wpratt.com

*/

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

PORT void *malloc0 (int size);

extern void print_impulse (const char* filename, int N, double* impulse, int rtype, int pr_mode);

void print_peak_val(const char* filename, int N, double* buff, double thresh);

void print_peak_env (const char* filename, int N, double* buff, double thresh);

extern void print_peak_env_f2 (const char* filename, int N, float* Ibuff, float* Qbuff);

extern void print_iqc_values(const char* file, int state, double env_in, double I, double Q, double ym, double yc, double ys, double thresh);

extern void print_meter (const char* filename, double* meter, int enum_av, int enum_pk, int enum_gain);

extern void print_message (const char* filename, const char* message, int p0, int p1, int p2);

extern void print_window_gain (const char* filename, int wintype, double inv_coherent_gain, double inherent_gain);

extern void print_deviation (const char* filename, double dpmax, double rate);

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

extern void WriteAudioWDSP(double seconds, int rate, int size, double* indata, int mode, double gain);

extern void WriteScaledAudio (
	double seconds,			// number of seconds of audio to record
	int rate,				// sample rate
	int size,				// incoming buffer size
	double* indata );		// pointer to incoming data buffer


#ifndef _bfcu_h
#define _bfcu_h

typedef struct _bfcu
{
	int id;
	int min_size;
	int max_size;
	double rate;
	double corner;
	int points;
	double* dataset[16];
	int i_lower_corner;
	int i_upper_corner;
}bfcu, * BFCU;

extern void test_bfcu();

extern void dprintf(const char* format, ...);

extern char* uint32_to_bitstr(uint32_t n, char* buf);

#endif
