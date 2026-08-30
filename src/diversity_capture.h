/* Copyright (C)
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

#ifndef _DIVERSITY_CAPTURE_H_
#define _DIVERSITY_CAPTURE_H_

//
// ===================================================================
//  DEVELOPMENT TOOL - NOT PART OF THE DIVERSITY FEATURE.
//
//  Everything here is compiled only under -DDIVERSITY_CAPTURE, which
//  the top-level Makefile adds for "make DIVCAP=1" and never otherwise.
//  It is to be deleted, along with the guarded blocks in
//  diversity_auto.c and diversity_menu.c, before the diversity work is
//  submitted upstream. See test/diversity/devtools/README.md.
// ===================================================================
//
// Records the two antenna streams as the auto-phasing analysis thread
// sees them - block aligned, at the DDC rate, ahead of any combining -
// so that a real signal can be replayed into the correlator offline as
// many times as a parameter sweep needs.
//
// The tap is in div_process_block(), which is the only place where the
// block, the context that produced it and the correlator's answer for it
// all exist together.
//

#include <stdint.h>
#include <stdio.h>

//
// On-disk format. Little-endian, which is every machine piHPSDR runs on;
// the replay tool checks the magic and version and refuses anything else
// rather than pretending to be portable.
//
#define DIVCAP_MAGIC        "PIHPDIVC"
#define DIVCAP_VERSION      1u
#define DIVCAP_REC_MAGIC    0x214B4C42u   /* "BLK!" */
#define DIVCAP_END_MAGIC    0x21444E45u   /* "END!" */

#define DIVCAP_NOTE_LEN     192
#define DIVCAP_RADIO_LEN    48

//
// Written once at the head of the file.
//
struct divcap_header {
  char     magic[8];
  uint32_t version;
  uint32_t sample_rate;       // DDC rate: 48000 .. 384000
  uint32_t nfft;              // sample pairs per block
  uint32_t block_bytes;       // float payload per block = 16 * nfft
  uint64_t t_start;           // unix seconds
  uint32_t flags;             // reserved, 0
  uint32_t pad;
  char     radio[DIVCAP_RADIO_LEN];
  char     note[DIVCAP_NOTE_LEN];
};

//
// One per analysis block. Everything the correlator was given, plus what
// it produced, so a replay can be checked against the live run before it
// is trusted to answer anything.
//
struct divcap_block {
  uint32_t rec_magic;
  uint32_t seq;
  uint32_t dropped;           // analysis blocks lost immediately before this one
  uint32_t rec_flags;         // bit 0: context differs from the previous block

  //
  // Fixed-width mirror of struct div_context. The context is what decides
  // where the analysis window sits and which pilot bank is searched, so a
  // capture without it cannot be replayed faithfully - and vfo_t's own
  // field widths are not something to depend on in a file format.
  //
  int64_t  frequency;
  int64_t  ctun_frequency;
  int64_t  offset;
  int32_t  sidetone;
  int32_t  ctx_sample_rate;
  int32_t  mode;
  int32_t  filter_low;
  int32_t  filter_high;
  int32_t  ref;
  int32_t  follow;
  int32_t  weighting;
  int32_t  pad0;
  double   centre;
  double   width;

  //
  // What rade_corr_process() was actually handed. Recorded rather than
  // recomputed because div_frame_off() and div_rade_side_expected() are
  // static in diversity_auto.c, and because a replay that re-derived them
  // would be testing that derivation rather than the correlator.
  //
  int32_t  expect_bank;
  int32_t  auto_mode;      // DIV_AUTO_NULL / DIV_AUTO_SUM, the objective
  double   frame_off;
  double   tau;
  double   hang;

  //
  // The loop's state *entering* this block, i.e. everything the previous
  // block left behind.
  //
  // Recorded at the tap rather than after processing because
  // div_process_block() has half a dozen exit points and threading a
  // result out of all of them would leave marks all over a file that has
  // to go back to exactly what it was. A (state, input) pair is also the
  // natural thing to check a deterministic state machine against: the
  // replay compares its own state entering block N with this, so like is
  // compared with like and the whole timeline still has to agree.
  //
  int32_t  live_locked;
  int32_t  live_confirming;
  int32_t  live_mirrored;
  int32_t  live_holding;
  double   live_quality;
  double   live_freq_off;
  double   live_snr;
  double   live_coherence;
  double   live_track_gain;    // dB,  the loop's answer before slewing
  double   live_track_phase;   // deg,  "
  double   live_cos;           // the weight actually applied to the samples
  double   live_sin;
  //
  // followed by nfft float pairs for arm 0, then nfft float pairs for arm 1
  //
};

//
// Written when the file is closed. A capture whose trailer is missing was
// truncated; one whose skipped count is non-zero perturbed the very thing
// it was recording and should be thrown away.
//
struct divcap_trailer {
  uint32_t end_magic;
  uint32_t blocks;
  uint32_t skipped;
  uint32_t pad;
  double   duration;
};

//
// Read once per block by the analysis thread, with no lock, exactly as
// div_auto_running is read by the sample path.
//
extern int div_capture_active;

//
// Arm/disarm. Safe to call from the GTK thread; both are idempotent.
// Returns 1 if a file was opened.
//
extern int  diversity_capture_start(int sample_rate, int nfft);
extern void diversity_capture_stop(void);

//
// Hand one analysis block to the writer thread. Copies and returns; all
// file I/O happens on the writer thread.
//
extern void diversity_capture_block(const float *arm0, const float *arm1,
                                    const struct divcap_block *meta);

//
// "rec 12.3s 9.4M" / "rec (7 lost)" / "" when idle. For the menu.
//
extern void diversity_capture_status(char *buf, size_t len);

#endif
