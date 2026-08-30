//
// DEVELOPMENT TOOL. Not part of piHPSDR - see README.md.
//
#include <stddef.h>
#include <string.h>

#include "rade_tuning.h"

//
// The defaults are the values src/rade_correlator.c ships with, written
// out here because the generator removes exactly those lines from its
// copy. If the two ever disagree, replay_rade --verify against a real
// capture is what catches it: a replay that does not reproduce the live
// run means this is not the correlator that took the recording.
//
// rade_tuning carries them from the start rather than waiting to be
// initialised. A zeroed tuning struct is a correlator with every
// threshold at zero, which does not fail - it quietly produces nonsense,
// and a caller that forgot to call rade_tuning_defaults() would get it.
//
#define RADE_TUNING_INIT {           \
    .acq_at      = { 8, 16, 32 },    \
    .acq_sigma   = { 7.5, 6.75, 4.8 },  /* [2] is RADE_LOCK_SIGMA */ \
    .probation   = 8,                \
    .mag_alpha   = 0.02,             \
    .use_alpha   = 0.12,             \
    .use_ratio   = 2.50,             \
    .floor_df    = 300.0,            \
    .floor_guard = 12,               \
    .freq_alpha  = 0.05,             \
    .freq_limit  = 0.5 * 100.0 + 10.0   /* 0.5*RADE_ACQ_FRANGE + 10 */ \
  }

static const rade_tuning_t rade_tuning_shipped = RADE_TUNING_INIT;

rade_tuning_t rade_tuning = RADE_TUNING_INIT;

void rade_tuning_defaults(void) {
  rade_tuning = rade_tuning_shipped;
}

enum { T_INT, T_DBL };

static const struct {
  const char *name;
  int         type;
  size_t      off;
} fields[] = {
  { "acq_at0",     T_INT, offsetof(rade_tuning_t, acq_at[0])    },
  { "acq_at1",     T_INT, offsetof(rade_tuning_t, acq_at[1])    },
  { "acq_at2",     T_INT, offsetof(rade_tuning_t, acq_at[2])    },
  { "acq_sigma0",  T_DBL, offsetof(rade_tuning_t, acq_sigma[0]) },
  { "acq_sigma1",  T_DBL, offsetof(rade_tuning_t, acq_sigma[1]) },
  { "acq_sigma2",  T_DBL, offsetof(rade_tuning_t, acq_sigma[2]) },
  { "probation",   T_INT, offsetof(rade_tuning_t, probation)    },
  { "mag_alpha",   T_DBL, offsetof(rade_tuning_t, mag_alpha)    },
  { "use_alpha",   T_DBL, offsetof(rade_tuning_t, use_alpha)    },
  { "use_ratio",   T_DBL, offsetof(rade_tuning_t, use_ratio)    },
  { "floor_df",    T_DBL, offsetof(rade_tuning_t, floor_df)     },
  { "floor_guard", T_INT, offsetof(rade_tuning_t, floor_guard)  },
  { "freq_alpha",  T_DBL, offsetof(rade_tuning_t, freq_alpha)   },
  { "freq_limit",  T_DBL, offsetof(rade_tuning_t, freq_limit)   },
  { NULL, 0, 0 }
};

int rade_tuning_set(const char *name, double value) {
  for (int i = 0; fields[i].name != NULL; i++) {
    if (strcmp(fields[i].name, name) != 0) { continue; }

    char *p = (char *)&rade_tuning + fields[i].off;

    if (fields[i].type == T_INT) {
      *(int *)p = (int)(value + (value < 0.0 ? -0.5 : 0.5));
    } else {
      *(double *)p = value;
    }

    return 1;
  }

  return 0;
}

double rade_tuning_get(const char *name) {
  for (int i = 0; fields[i].name != NULL; i++) {
    if (strcmp(fields[i].name, name) != 0) { continue; }

    const char *p = (const char *)&rade_tuning + fields[i].off;
    return (fields[i].type == T_INT) ? (double) * (const int *)p : *(const double *)p;
  }

  return 0.0;
}

const char *rade_tuning_name(int i) {
  int n = 0;

  while (fields[n].name != NULL) { n++; }

  return (i >= 0 && i < n) ? fields[i].name : NULL;
}
