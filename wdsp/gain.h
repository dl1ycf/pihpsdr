/*  gain.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013 Warren Pratt, NR0V

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

#ifndef _gain_h
#define _gain_h

typedef struct _gain
{
	int run;
	int* prun;
	int size;
	double* in;
	double* out;
	double Igain;
	double Qgain;
	CRITICAL_SECTION cs_update;
}gain, *GAIN;

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.]

extern void setBuffers_gain (GAIN a, double* in, double* out);

extern void setSamplerate_gain (GAIN a, int rate);

extern void setSize_gain (GAIN a, int size);

// TXA Properties

#endif