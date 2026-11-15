/*  eer.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014 Warren Pratt, NR0V

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

#ifndef _eer_h
#define _eer_h

typedef struct _eer
{
	int run;
	int amiq;
	int size;
	double* in;
	double* out;
	double* outM;
	int rate;
	double mgain;
	double pgain;
	int rundelays;
	double mdelay;
	double pdelay;
	DELAY mdel;
	DELAY pdel;
	CRITICAL_SECTION cs_update;
	double *legacy;																										////////////  legacy interface - remove
	double *legacyM;																									////////////  legacy interface - remove
} eer, *EER;

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

PORT EER create_eer (int run, int size, double* in, double* out, double* outM, int rate, double mgain, double pgain, int rundelays, double mdelay, double pdelay, int amiq);

PORT void destroy_eer (EER a);

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

PORT void xeer (EER a);

PORT void pSetEERRun (EER a, int run);

PORT void pSetEERAMIQ (EER a, int amiq);

PORT void pSetEERMgain (EER a, double gain);

PORT void pSetEERPgain (EER a, double gain);

PORT void pSetEERRunDelays (EER a, int run);

PORT void pSetEERMdelay (EER a, double delay);

PORT void pSetEERPdelay (EER a, double delay);

PORT void pSetEERSize (EER a, int size);

PORT void pSetEERSamplerate (EER a, int rate);

#endif