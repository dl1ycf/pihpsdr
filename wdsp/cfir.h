/*  cfir.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014, 2016, 2026 Warren Pratt, NR0V

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

#ifndef _cfir_h
#define _cfir_h

typedef struct _cfir *CFIR;

extern CFIR create_cfir (int run, int size, int nc, int mp, double* in, double* out, int runrate, int cicrate, 
	int DD, int R, int Pairs, double cutoff, int xtype, double xbw, int wintype);

extern void destroy_cfir (CFIR a);

extern void flush_cfir (CFIR a);

extern void xcfir (CFIR a);

extern void setBuffers_cfir (CFIR a, double* in, double* out);

extern void setSamplerate_cfir (CFIR a, int rate);

extern void setSize_cfir (CFIR a, int size);

extern void setOutRate_cfir (CFIR a, int rate);

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

extern PORT void SetTXACFIRNC(int channel, int nc);

#endif