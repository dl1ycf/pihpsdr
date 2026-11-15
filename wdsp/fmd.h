/*  fmd.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2026 Warren Pratt, NR0V

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

warren@pratt.one

*/

#ifndef _fmd_h
#define _fmd_h

typedef struct _fmd* FMD;

extern FMD create_fmd ( int run, int size, double* in, double* out, int rate, double deviation, 
	double f_low, double f_high, double fmin, double fmax, double zeta, double omegaN, double tau, 
	double afgain, int sntch_run, double ctcss_freq, int nc_de, int mp_de, int nc_aud, int mp_aud);

extern void destroy_fmd (FMD a);

extern void flush_fmd (FMD a);

extern void xfmd (FMD a);

extern void setBuffers_fmd (FMD a, double* in, double* out);

extern void setSamplerate_fmd (FMD a, int rate);

extern void setSize_fmd (FMD a, int size);

extern double* getFMDpAudio (int channel);

extern double* getFMDpPllpole (int channel);

extern void setFMDRun (int channel, int run);

// RXA Properties

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

extern PORT void SetRXAFMNCde (int channel, int nc);

extern PORT void SetRXAFMMPde (int channel, int mp);

extern PORT void SetRXAFMNCaud (int channel, int nc);

extern PORT void SetRXAFMMPaud (int channel, int mp);

#endif