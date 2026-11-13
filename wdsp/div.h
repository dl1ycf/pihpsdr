/*  div.h

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

#ifndef _div_h
#define _div_h

typedef struct _div
{
	int run;
	int nr;							// number of receivers to mix
	int size;						// size of input/output buffers
	double **in;					// input buffers
	double *out;					// output buffer
	int output;						// which rcvr to output; ==nr for mix
	double *Irotate;
	double *Qrotate;
	CRITICAL_SECTION cs_update;
	double *legacy[4];																	///////////// legacy interface - remove
} mdiv, *MDIV;

extern MDIV create_div (int run, int nr, int size, double **in, double *out);

extern void destroy_div (MDIV pdiv);

extern void xdiv (MDIV pdiv);

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

extern PORT void xdivEXT (int id, int nsamples, double **in, double *out);

extern PORT void create_divEXT (int id, int run, int nr, int size);

extern PORT void destroy_divEXT (int id);

#endif