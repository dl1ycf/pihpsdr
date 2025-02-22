/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

#ifndef _CLIENT_SERVER_H_
#define _CLIENT_SERVER_H_

#include <gtk/gtk.h>
#include <stdint.h>
#include <netinet/in.h>

#include "mode.h"
#include "receiver.h"

#define mydouble uint64_t

typedef enum {
  RECEIVER_DETACHED,
  RECEIVER_ATTACHED
} CLIENT_STATE;

enum _header_type_enum {
  INFO_RADIO,
  INFO_ADC,
  INFO_DAC,
  INFO_RECEIVER,
  INFO_TRANSMITTER,
  INFO_VFO,
  INFO_BAND,
  INFO_BANDSTACK,
  INFO_MEMORY,
  INFO_SPECTRUM,
  INFO_AUDIO,
  CMD_START_RADIO,
  CMD_SPECTRUM,
  CMD_AUDIO,
  CMD_SAMPLE_RATE,
  CMD_LOCK,
  CMD_CTUN,
  CMD_SPLIT,
  CMD_SAT,
  CMD_DUP,
  CMD_STEP,
  CMD_RECEIVERS,
  CMD_RX_FREQ,
  CMD_RX_STEP,
  CMD_RX_MOVE,
  CMD_RX_MOVETO,
  CMD_RX_BAND,               // short command: vfo=b1, band=b2
  CMD_RX_BANDSTACK,          // short command: stack=b1
  CMD_RX_MODE,               // short command: vfo=b1, mode=b2
  CMD_RX_FILTER_SEL,         // short command: vfo=b1, filter=b2
  CMD_RX_FILTER_VAR,         // short command: mode=b1, filter=b2, low=s1, high=s2
  CMD_RX_FILTER_CUT,         // short command: rx=b1, low=s1, high=s2
  CMD_RX_AGC,
  CMD_RX_NOISE,
  CMD_RX_ZOOM,
  CMD_RX_PAN,
  CMD_RX_VOLUME,
  CMD_RX_AGC_GAIN,
  CMD_RX_ATTENUATION,
  CMD_RX_GAIN,
  CMD_RX_SQUELCH,
  CMD_FPS,                   // short command: id=b1, fps=b2
  CMD_RX_SELECT,             // short command: rx=b1
  CMD_VFO_A_TO_B,            // short command: no parameters
  CMD_VFO_B_TO_A,            // short command: no parameters
  CMD_VFO_SWAP,              // short command: no parameters
  CMD_RIT_TOGGLE,            // short command: vfo=b1
  CMD_RIT_VALUE,             // short command: vfo=b1, rit=s1
  CMD_RIT_INCR,              // short command: vfo=b1, incr=s1
  CMD_XIT_TOGGLE,
  CMD_XIT_CLEAR,
  CMD_XIT,
  CMD_RIT_STEP,              // short command: vfo=b1, step=s1
  CMD_FILTER_BOARD,
  CMD_SWAP_IQ,
  CMD_REGION,
  CMD_MUTE_RX,
  CMD_ANAN10E,
  CMD_RX_EQ,
  CMD_TX_EQ,
  CMD_RX_DISPLAY,
  CMD_TX_DISPLAY,
  CMD_PTT,
  CMD_TUNE,
  CMD_TWOTONE,
  CMD_MICGAIN,
  CMD_DRIVE,
  CMD_SCREEN,
  CMD_METER,
  CMD_XVTR,
  CMD_RCL,
  CMD_STORE,
  CMD_VFO_STEPSIZE,
  CMD_ADC,
  CMD_RADIOMENU,
  CMD_RXMENU,
  CMD_CWPEAK,
  CMD_DIVERSITY,
  CLIENT_SERVER_COMMANDS,
};

#define CLIENT_SERVER_VERSION 0xFFFF     // This indicates a test version
#define SPECTRUM_DATA_SIZE 4096          // Maximum width of a panadapter
#define AUDIO_DATA_SIZE 1024             // 1024 stereo samples

#define REMOTE_SYNC (uint16_t)0xFAFA

typedef struct _remote_client {
  gboolean running;
  int socket;
  socklen_t address_length;
  struct sockaddr_in address;
  GThread *thread_id;
  CLIENT_STATE state;
  int receivers;
  guint spectrum_update_timer_id;
  int send_spectrum[10];  // slot #8 is for the transmitter
  void *next;
} REMOTE_CLIENT;

typedef struct __attribute__((__packed__)) _header {
  uint16_t sync;
  uint16_t data_type;
  uint16_t version;
  //
  // two bytes and two shorts that can be used
  // to store data for commands that need that little,
  // so these commands only need the header
  //
  uint8_t b1;
  uint8_t b2;
  uint16_t s1;
  uint16_t s2;
  union {
    // The payload can be used by some variable-length
    // commands such as INFO_SPECTRUM
    uint64_t payload;
    REMOTE_CLIENT *client;
  } context;
} HEADER;

//
// This is the data that is changed in the radio menu
// and needs no special processing. Note state variables
// like "anan10e" (which, if changed,  requires re-starting
// the protocol) are sent in dedicated commands.
//
typedef struct __attribute__((__packed__)) _radiomenu_data {
  HEADER header;
  uint8_t  mic_ptt_tip_bias_ring;
  uint8_t  sat_mode;
  uint8_t  mic_input_xlr;
  uint8_t  atlas_clock_source_10mhz;
  uint8_t  atlas_clock_source_128mhz;
  uint8_t  atlas_mic_source;
  uint8_t  atlas_penelope;
  uint8_t  atlas_janus;
  uint8_t  mic_ptt_enabled;
  uint8_t  mic_bias_enabled;
  uint8_t  pa_enabled;
  uint8_t  mute_spkr_amp;
  uint8_t  hl2_audio_codec;
  uint8_t  soapy_iqswap;
  uint8_t  enable_tx_inhibit;
  uint8_t  enable_auto_tune;
//
  uint16_t rx_gain_calibration;
//
  uint64_t frequency_calibration;
} RADIOMENU_DATA;

//
// This is the data changed in the RX  menu
// which requires no special processing. Some variables
// (like dither) are associated with the receiver #id,
// but we also find global data such as the ADC bypass
// options.
//
typedef struct __attribute__((__packed__)) _rxmenu_data {
  HEADER header;
  uint8_t id;
  uint8_t dither;
  uint8_t random;
  uint8_t preamp;
  uint8_t adc0_filter_bypass;
  uint8_t adc1_filter_bypass;
} RXMENU_DATA;

//
// All the data associated with a BAND (note that "current"
// rather refers to the associated band stack). Exchanging
// this data between client and server is mostly only necessary
// for transverter bands (the 60m band is also special).
//
typedef struct __attribute__((__packed__)) _band_data {
  HEADER header;
  char title[16];
  uint8_t  band;
  uint8_t  OCrx;
  uint8_t  OCtx;
  uint8_t  alexRxAntenna;
  uint8_t  alexTxAntenna;
  uint8_t  alexAttenuation;
  uint8_t  disablePA;
  uint8_t  current;
  uint16_t gain;
  mydouble pa_calibration;
  uint64_t frequencyMin;
  uint64_t frequencyMax;
  uint64_t frequencyLO;
  uint64_t errorLO;
} BAND_DATA;

//
// Bandstack data
//
typedef struct __attribute__((__packed__)) _bandstack_data {
  HEADER header;
  uint8_t band;
  uint8_t stack;
  uint8_t mode;
  uint8_t filter;
  uint8_t ctun;
  uint8_t ctcss_enabled;
  uint8_t ctcss;
  uint16_t deviation;
  uint64_t frequency;
  uint64_t ctun_frequency;
} BANDSTACK_DATA;

//
// This is the date of the memory (Store/Recall) slots
//
typedef struct __attribute__((__packed__)) _memory_data {
  HEADER header;
  uint8_t index;
  uint8_t ctun;
  uint8_t mode;
  uint8_t filter;
  uint8_t ctcss_enabled;
  uint8_t ctcss;
  uint8_t bd;
//
  uint64_t frequency;
  uint64_t ctun_frequency;
} MEMORY_DATA;

//
// This is the global data sent *once* by the server
// to the client when the connection has been established.
//
typedef struct __attribute__((__packed__)) _radio_data {
  HEADER header;
  char name[32];
//
  uint8_t  locked;
  uint8_t  protocol;
  uint8_t  supported_receivers;
  uint8_t  receivers;
  uint8_t  filter_board;
  uint8_t  enable_auto_tune;
  uint8_t  new_pa_board;
  uint8_t  region;
  uint8_t  atlas_penelope;
  uint8_t  atlas_clock_source_10mhz;
  uint8_t  atlas_clock_source_128mhz;
  uint8_t  atlas_mic_source;
  uint8_t  atlas_janus;
  uint8_t  hl2_audio_codec;
  uint8_t  anan10E;
  uint8_t  tx_out_of_band_allowed;
  uint8_t  pa_enabled;
  uint8_t  mic_boost;
  uint8_t  mic_linein;
  uint8_t  mic_ptt_enabled;
  uint8_t  mic_bias_enabled;
  uint8_t  mic_ptt_tip_bias_ring;
  uint8_t  mic_input_xlr;
  uint8_t  cw_keyer_sidetone_volume;
  uint8_t  OCtune;
  uint8_t  vox_enabled;
  uint8_t  mute_rx_while_transmitting;
  uint8_t  mute_spkr_amp;
  uint8_t  adc0_filter_bypass;
  uint8_t  adc1_filter_bypass;
  uint8_t  split;
  uint8_t  sat_mode;
  uint8_t  duplex;
  uint8_t  have_rx_gain;
  uint8_t  have_rx_att;
  uint8_t  have_alex_att;
  uint8_t  have_preamp;
  uint8_t  have_dither;
  uint8_t  have_saturn_xdma;
  uint8_t  rx_stack_horizontal;
  uint8_t  n_adc;
//
  uint16_t pa_power;
  uint16_t OCfull_tune_time;
  uint16_t OCmemory_tune_time;
  uint16_t cw_keyer_sidetone_frequency;
  uint16_t rx_gain_calibration;
  uint16_t device;
  uint16_t tx_filter_low;
  uint16_t tx_filter_high;
  uint16_t display_width;
//
  mydouble vox_threshold;
  mydouble vox_hang;
  mydouble drive_digi_max;
  mydouble pa_trim[11];
//
  uint64_t frequency_calibration;
  uint64_t soapy_radio_sample_rate;
  uint64_t radio_frequency_min;
  uint64_t radio_frequency_max;
} RADIO_DATA;

typedef struct __attribute__((__packed__)) _dac_data {
  HEADER header;
  uint8_t antenna;
  mydouble gain;
} DAC_DATA;

typedef struct __attribute__((__packed__)) _adc_data {
  HEADER header;
  uint8_t adc;
  uint8_t dither;
  uint8_t random;
  uint8_t preamp;
//
  uint16_t filters;
  uint16_t hpf;
  uint16_t lpf;
  uint16_t antenna;
  uint16_t attenuation;
//
  mydouble gain;
  mydouble min_gain;
  mydouble max_gain;
} ADC_DATA;

//
// This is the transmitter data sent by the server to the client.
// It is sent when the connection has been established, but can
// be re-sent any time by the server if necessary.
//
typedef struct __attribute__((__packed__)) _transmitter_data {
  HEADER header;
  uint8_t  id;
  uint8_t  dac;
  uint8_t  display_detector_mode;
  uint8_t  display_average_mode;
  uint8_t  use_rx_filter;
  uint8_t  alex_antenna;
  uint8_t  puresignal;
  uint8_t  feedback;
  uint8_t  auto_on;
  uint8_t  ps_oneshot;
  uint8_t  ctcss_enabled;
  uint8_t  ctcss;
  uint8_t  pre_emphasize;
  uint8_t  drive;
  uint8_t  tune_use_drive;
  uint8_t  tune_drive;
  uint8_t  compressor;
  uint8_t  cfc;
  uint8_t  cfc_eq;
  uint8_t  dexp;
  uint8_t  dexp_filter;
  uint8_t  eq_enable;
  uint8_t  alcmode;
//
  uint16_t dexp_filter_low;
  uint16_t dexp_filter_high;
  uint16_t dexp_trigger;
  uint16_t dexp_exp;
  uint16_t filter_low;
  uint16_t filter_high;
  uint16_t deviation;
  uint16_t width;
  uint16_t height;
  uint16_t attenuation;
//
  mydouble eq_freq[11];
  mydouble eq_gain[11];
  mydouble dexp_tau;
  mydouble dexp_attack;
  mydouble dexp_release;
  mydouble dexp_hold;
  mydouble dexp_hyst;
  mydouble cfc_freq[11];
  mydouble cfc_lvl[11];
  mydouble cfc_post[11];
  mydouble mic_gain;
  mydouble compressor_level;
  mydouble display_average_time;
  mydouble am_carrier_level;
  mydouble ps_ampdelay;
  mydouble ps_moxdelay;
  mydouble ps_loopdelay;
} TRANSMITTER_DATA;

//
// This is the receiver data sent by the server to the client.
// It is sent when the connection has been established, but can
// be re-sent any time by the server if necessary.
//
typedef struct __attribute__((__packed__)) _receiver_data {
  HEADER header;
  uint8_t id;
  uint8_t adc;
  uint8_t agc;
  uint8_t nb;
  uint8_t nb2_mode;
  uint8_t nr;
  uint8_t nr_agc;
  uint8_t nr2_ae;
  uint8_t nr2_gain_method;
  uint8_t nr2_npe_method;
  uint8_t anf;
  uint8_t snb;
  uint8_t display_detector_mode;
  uint8_t display_average_mode;
  uint8_t zoom;
  uint8_t dither;
  uint8_t random;
  uint8_t preamp;
  uint8_t alex_antenna;
  uint8_t alex_attenuation;
  uint8_t squelch_enable;
  uint8_t binaural;
  uint8_t eq_enable;
  uint8_t smetermode;
//
  uint16_t fps;
  uint16_t filter_low;
  uint16_t filter_high;
  uint16_t deviation;
  uint16_t pan;
  uint16_t width;
//
  mydouble hz_per_pixel;
  mydouble squelch;
  mydouble display_average_time;
  mydouble volume;
  mydouble agc_gain;
  mydouble agc_hang;
  mydouble agc_thresh;
  mydouble agc_hang_threshold;
  mydouble nr2_trained_threshold;
  mydouble nr2_trained_t2;
  mydouble nb_tau;
  mydouble nb_hang;
  mydouble nb_advtime;
  mydouble nb_thresh;
  mydouble nr4_reduction_amount;
  mydouble nr4_smoothing_factor;
  mydouble nr4_whitening_factor;
  mydouble nr4_noise_rescale;
  mydouble nr4_post_threshold;
  mydouble eq_freq[11];
  mydouble eq_gain[11];
//
  uint64_t fft_size;
  uint64_t sample_rate;
} RECEIVER_DATA;

//
// This is the VFO data sent by the server to the client.
// It is sent when the connection has been established, but can
// be re-sent any time by the server if necessary.
//
typedef struct __attribute__((__packed__)) _vfo_data {
  HEADER header;
  uint8_t  vfo;
  uint8_t  band;
  uint8_t  bandstack;
  uint8_t  mode;
  uint8_t  filter;
  uint8_t  ctun;
  uint8_t  rit_enabled;
  uint16_t rit_step;
  uint64_t frequency;
  uint64_t ctun_frequency;
  uint64_t rit;
  uint64_t lo;
  uint64_t offset;
  uint64_t step;
} VFO_DATA;

//
// Data for a panadapter. Can be used for receivers (id = 0,1)
// or for the transmitter (id = 8). Sent periodically as long as
// is is enabled for this panadapter via CMD_SPECTRUM.
// Note that this also contains high-frequency data such as
// RX S-meter, TX power/ALC/swr and PURESIGNAL status data, toghether
// with VFO frequencies for "quick" VFO update.
//
typedef struct __attribute__((__packed__)) _spectrum_data {
  HEADER header;
  uint8_t id;
  uint8_t pscorr;
  uint16_t width;
//
  uint64_t vfo_a_freq;
  uint64_t vfo_b_freq;
  uint64_t vfo_a_ctun_freq;
  uint64_t vfo_b_ctun_freq;
  uint64_t vfo_a_offset;
  uint64_t vfo_b_offset;
//
  mydouble meter;
  mydouble alc;
  mydouble fwd;
  mydouble swr;
  uint16_t sample[SPECTRUM_DATA_SIZE];
} SPECTRUM_DATA;

typedef struct __attribute__((__packed__)) _audio_data {
  HEADER header;
  uint8_t rx;
  uint16_t samples;
  uint16_t sample[AUDIO_DATA_SIZE * 2];
} AUDIO_DATA;


//
// Universal data structure for commands that need
// (possibly besides data stored in the header) only
// a single "long long".
//
typedef struct __attribute__((__packed__)) _u64_command {
  HEADER header;
  uint64_t u64;
} U64_COMMAND;

//
// Universal data structure for commands that need
// (possibly besides data stored in the header) only
// a single "double".
//
typedef struct __attribute__((__packed__)) _double_command {
  HEADER header;
  mydouble dbl;
} DOUBLE_COMMAND;

typedef struct __attribute__((__packed__)) _diversity_command {
  HEADER header;
  uint8_t diversity_enabled;
  mydouble div_gain;
  mydouble div_phase;
} DIVERSITY_COMMAND;

typedef struct __attribute__((__packed__)) _agc_gain_command {
  HEADER header;
  uint8_t id;
  mydouble gain;
  mydouble hang;
  mydouble thresh;
  mydouble hang_thresh;
} AGC_GAIN_COMMAND;

//
// Data to be sent by the client if an equalizer (RX1, RX2 or TX)
// has been changed
//
typedef struct __attribute__((__packed__)) _equalizer_command {
  HEADER header;
  uint8_t  id;
  uint8_t  enable;
  mydouble freq[11];
  mydouble gain[11];
} EQUALIZER_COMMAND;

//
// Data to be sent by the client if a noise reduction parameter
// (noise menu or command) has been changed
//
typedef struct __attribute__((__packed__)) _noise_command {
  HEADER header;
  uint8_t  id;
  uint8_t  nb;
  uint8_t  nr;
  uint8_t  anf;
  uint8_t  snb;
  uint8_t  nb2_mode;
  uint8_t  nr_agc;
  uint8_t  nr2_gain_method;
  uint8_t  nr2_npe_method;
  uint8_t  nr2_ae;
  mydouble nb_tau;
  mydouble nb_hang;
  mydouble nb_advtime;
  mydouble nb_thresh;
  mydouble nr2_trained_threshold;
  mydouble nr2_trained_t2;
  mydouble nr4_reduction_amount;
  mydouble nr4_smoothing_factor;
  mydouble nr4_whitening_factor;
  mydouble nr4_noise_rescale;
  mydouble nr4_post_threshold;
} NOISE_COMMAND;

extern gboolean hpsdr_server;
extern gboolean hpsdr_server;
extern int client_socket;
extern int start_spectrum(void *data);
extern void start_vfo_timer(void);
extern gboolean remote_started;

extern REMOTE_CLIENT *remoteclients;

extern int listen_port;

extern int create_hpsdr_server(void);
extern int destroy_hpsdr_server(void);

extern int radio_connect_remote(char *host, int port);

extern void send_radio_data(int sock);
extern void send_adc_data(int sock, int i);
extern void send_dac_data(int sock);
extern void send_receiver_data(int sock, int rx);
extern void send_vfo_data(int sock, int v);
extern void send_memory_data(int sock, int index);

extern void send_startstop_spectrum(int s, int rx, int state);
extern void send_vfo_frequency(int s, int rx, long long hz);
extern void send_vfo_move_to(int s, int rx, long long hz);
extern void update_vfo_move(int rx, long long hz, int round);
extern void send_vfo_stepsize(int s, int v, int stepsize);
extern void send_vfo_step(int s, int rx, int steps);
extern void update_vfo_step(int rx, int steps);
extern void send_zoom(int s, int rx, int zoom);
extern void send_pan(int s, int rx, int pan);
extern void send_meter(int s, int metermode, int alcmode);
extern void send_screen(int s, int hstack, int width);
extern void send_volume(int s, int rx, double volume);
extern void send_agc(int s, int rx, int agc);
extern void send_diversity(int s, int enabled, double gain, double phase);
extern void send_agc_gain(int s, int rx, double gain, double hang, double thresh, double hang_thresh);
extern void send_attenuation(int s, int rx, int attenuation);
extern void send_rfgain(int s, int rx, double gain);
extern void send_squelch(int s, int rx, int enable, double squelch);
extern void send_eq(int s, int id);
extern void send_recall(int s, int index);
extern void send_store(int s, int index);
extern void send_micgain(int s, double gain);
extern void send_drive(int s, double value);
extern void send_noise(int s, const RECEIVER *rx);
extern void send_xvtr_changed(int s);
extern void send_band(int s, int rx, int band);
extern void send_bandstack(int s, int old, int new);
extern void send_band_data(int s, int band);
extern void send_bandstack_data(int s, int band, int stack);
extern void send_mode(int s, int rx, int mode);
extern void send_ptt(int s, int state);
extern void send_tune(int s, int state);
extern void send_twotone(int s, int state);
extern void send_filter_sel(int s, int vfo, int filter);
extern void send_filter_var(int s, int mode, int filter);
extern void send_filter_cut(int s, int rx);
extern void send_cwpeak(int s, int id, int state);
extern void send_split(int s, int state);
extern void send_sat(int s, int sat);
extern void send_duplex(int s, int state);
extern void send_ctun(int s, int vfo, int ctun);
extern void send_display(int s, int id);
extern void send_fps(int s, int rx, int fps);
extern void send_rx_select(int s, int rx);
extern void send_lock(int s, int lock);
extern void send_rit_toggle(int s, int rx);
extern void send_rit_value(int s, int rx, int ritval);
extern void send_rit_incr(int s, int rx, int incr);
extern void send_xit_toggle(int s);
extern void send_xit_clear(int s);
extern void send_xit(int s, int xit);
extern void send_sample_rate(int s, int rx, int sample_rate);
extern void send_receivers(int s, int receivers);
extern void send_rit_step(int s, int v, int step);
extern void send_filter_board(int s, int filter_board);
extern void send_swap_iq(int s, int swap_iq);
extern void send_region(int s, int region);
extern void send_mute_rx(int s, int id, int mute);
extern void send_varfilter_data(int s);
extern void send_anan10E(int s, int new);
extern void send_adc(int s, int id, int adc);
extern void send_radiomenu(int s);
extern void send_rxmenu(int s, int id);

extern void remote_audio(const RECEIVER *rx, short left_sample, short right_sample);

#endif
