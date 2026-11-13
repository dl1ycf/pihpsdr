/*  fmmod.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2016, 2023 Warren Pratt, NR0V

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

#ifndef _fmmod_h
#define _fmmod_h
#include "firmin.h"
typedef struct _fmmod
{
	int run;
	int size;
	double* in;
	double* out;
	double samplerate;
	double deviation;
	double f_low;
	double f_high;
	int ctcss_run;
	double ctcss_level;
	double ctcss_freq;
	// for ctcss gen
	double tscale;
	double tphase;
	double tdelta;
	// mod
	double sphase;
	double sdelta;
	// bandpass
	int bp_run;
	double bp_fc;
	int nc;
	int mp;
	FIRCORE p;
}fmmod, *FMMOD;

extern FMMOD create_fmmod (int run, int size, double* in, double* out, int rate, double dev, double f_low, double f_high, 
	int ctcss_run, double ctcss_level, double ctcss_freq, int bp_run, int nc, int mp);

extern void destroy_fmmod (FMMOD a);

extern void flush_fmmod (FMMOD a);

extern void xfmmod (FMMOD a);

extern void setBuffers_fmmod (FMMOD a, double* in, double* out);

extern void setSamplerate_fmmod (FMMOD a, int rate);

extern void setSize_fmmod (FMMOD a, int size);

// TXA Properties

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

extern PORT void SetTXAFMMP (int channel, int mp);

extern PORT void SetTXAFMNC (int channel, int nc);

extern PORT void SetTXAFMAFFreqs (int channel, double low, double high);

#endif