/*  nob.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2014 Warren Pratt, NR0V

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

#ifndef _anb_h
#define _anb_h

typedef struct _anb
{
	int run;
	int buffsize;					// size of input/output buffer
	double* in;						// input buffer
	double* out;					// output buffer
	int dline_size;					// length of delay line which is 'double dline[length][2]'
	double *dline;					// pointer to delay line
	double samplerate;				// samplerate, used to convert times into sample counts
	double tau;						// transition time, signal<->zero
	double hangtime;				// time to stay at zero after noise is no longer detected
	double advtime;					// deadtime (zero output) in advance of detected noise
	double backtau;					// time constant used in averaging the magnitude of the input signal
	double threshold;				// triggers if (noise > threshold * average_signal_magnitude)
    double *wave;                   // pointer to array holding transition waveform
    int state;                      // state of the state machine
    double avg;                     // average value of the signal magnitude
    int dtime;                      // count when decreasing the signal magnitude
    int htime;                      // count when hanging
    int itime;                      // count when increasing the signal magnitude
    int atime;                      // count at zero before the noise burst (advance count)
    double coef;					// parameter in calculating transition waveform
    int trans_count;                // number of samples to equal 'tau' time
    int hang_count;                 // number of samples to equal 'hangtime' time
    int adv_count;                  // number of samples to equal 'advtime' time
    int in_idx;                     // ring buffer position into which new samples are inserted
    int out_idx;                    // ring buffer position from which delayed samples are pulled
    double power;					// level at which signal was increasing when a new decrease is started
    int count;						// set each time a noise sample is detected, counts down
    double backmult;				// multiplier for waveform averaging
    double ombackmult;				// multiplier for waveform averaging
	CRITICAL_SECTION cs_update;
	double *legacy;																										////////////  legacy interface - remove
} anb, *ANB;

// [PORT is defined in wdsp_platform.h, which comm.h includes ahead of the module
// headers.  Some WDSP headers are also included on their own, ahead of comm.h
// (ChannelMaster includes resample.h this way), so a header that uses PORT
// includes wdsp_platform.h itself.  It also supplies the types, CRITICAL_SECTION
// and HANDLE, that some of these headers use.]
#include "wdsp_platform.h"

PORT ANB create_anb	(
	int run,
	int buffsize,
	double* in,
	double* out,
	double samplerate,
	double tau,
	double hangtime,
	double advtime,
	double backtau,
	double threshold
						);

PORT void destroy_anb (ANB a);

// [Exported (PORT) functions of this module that nothing else in WDSP, nor
// ChannelMaster, calls are declared only in wdsp.h, the public header.  Those
// declared here are called by other modules or by ChannelMaster.]

PORT void xanb (ANB a);

extern void setBuffers_anb (ANB a, double* in, double* out);

extern void setSamplerate_anb (ANB a, int rate);

extern void setSize_anb (ANB a, int size);


extern PORT void pSetRCVRANBRun (ANB a, int run);

extern PORT void pSetRCVRANBBuffsize (ANB a, int size);

extern PORT void pSetRCVRANBSamplerate (ANB a, int rate);

extern PORT void pSetRCVRANBTau (ANB a, double tau);

extern PORT void pSetRCVRANBHangtime (ANB a, double time);

extern PORT void pSetRCVRANBAdvtime (ANB a, double time);

extern PORT void pSetRCVRANBBacktau (ANB a, double tau);

extern PORT void pSetRCVRANBThreshold (ANB a, double thresh);

#endif