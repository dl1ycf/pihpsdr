//
// DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
//
// The constants the RADE V1 correlator's behaviour actually turns on,
// as a struct, so that replay_rade can sweep them in one process.
//
// This header is included at the top of the *generated* correlator
// (build/rade_correlator_tunable.c), where the matching #define lines
// have been removed. src/rade_correlator.c itself is untouched, and the
// live build never sees any of this.
//
#ifndef RADE_TUNING_H
#define RADE_TUNING_H

#define RADE_TUNING_CHECKS 3

typedef struct {
  //
  // Progressive acquisition: score the grid after acq_at[i] passes and
  // require acq_sigma[i]. Defaults 8/16/32 passes at 7.5/6.75/4.8 sigma.
  //
  int    acq_at[RADE_TUNING_CHECKS];
  double acq_sigma[RADE_TUNING_CHECKS];
  //
  // Frames a candidate is followed before it becomes a lock. 8 = ~1 s.
  //
  int    probation;
  //
  // Smoothing of the pilot-to-floor ratio: mag_alpha is the slow one
  // (~6 s, the reported health of a lock), use_alpha the fast one (~1 s,
  // the freeze decision).
  //
  double mag_alpha;
  double use_alpha;
  //
  // The freeze threshold. docs/diversity-rade.md names this as what
  // currently sets the weak-signal floor *and* holds the false-alarm
  // line, which makes it the first thing worth sweeping and the one to
  // sweep against a noise-only capture as well as a signal.
  //
  double use_ratio;
  //
  // Where the correlation floor is measured: +/-1 and +/-2 times this,
  // in Hz, away from the locked frequency.
  //
  double floor_df;
  //
  // Samples excluded either side of the peak when the acquisition
  // statistic's mean and spread are taken down a frequency column.
  //
  int    floor_guard;
  //
  // Frequency tracking loop.
  //
  double freq_alpha;
  double freq_limit;
} rade_tuning_t;

extern rade_tuning_t rade_tuning;

//
// Reset to the values src/rade_correlator.c ships with. Call this before
// applying each sweep point, so one point cannot inherit another's.
//
extern void rade_tuning_defaults(void);

//
// Set one field by name, as used by --set and --sweep. Names are the
// struct field names; the arrays are indexed, e.g. "acq_sigma2".
// Returns 0 if the name is not known.
//
extern int rade_tuning_set(const char *name, double value);

//
// Enumerate the settable names, for --help and for the CSV header.
// Returns NULL past the end.
//
extern const char *rade_tuning_name(int i);
extern double      rade_tuning_get(const char *name);

//
// The redirections. Every use site in the correlator is left alone; only
// the definitions were removed by mktunable.awk.
//
#define RADE_PROBATION    (rade_tuning.probation)
#define RADE_MAG_ALPHA    (rade_tuning.mag_alpha)
#define RADE_USE_ALPHA    (rade_tuning.use_alpha)
#define RADE_USE_RATIO    (rade_tuning.use_ratio)
#define RADE_FLOOR_DF     (rade_tuning.floor_df)
#define RADE_FLOOR_GUARD  (rade_tuning.floor_guard)
#define RADE_FREQ_ALPHA   (rade_tuning.freq_alpha)
#define RADE_FREQ_LIMIT   (rade_tuning.freq_limit)
#define rade_acq_at       (rade_tuning.acq_at)
#define rade_acq_sigma    (rade_tuning.acq_sigma)

#endif
