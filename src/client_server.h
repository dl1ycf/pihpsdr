/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
* 2025 - Christoph van Wüllen, DL1YCF
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
#include "transmitter.h"

#define mydouble uint64_t

enum _header_type_enum {
  CMD_ADC,
  CMD_AGC,
  CMD_AGC_GAIN,
  CMD_AMCARRIER,
  CMD_ANAN10E,
  CMD_ATTENUATION,
  CMD_BAND_SEL,
  CMD_BANDSTACK,
  CMD_BINAURAL,
  CMD_CAPTURE,
  CMD_COMPRESSOR,
  CMD_CTCSS,
  CMD_CTUN,
  CMD_CW,
  CMD_CWPEAK,
  CMD_DEVIATION,
  CMD_DEXP,
  CMD_DIGIMAX,
  CMD_DIVERSITY,
  CMD_DRIVE,
  CMD_DUP,
  CMD_FILTER_BOARD,
  CMD_FILTER_SEL,
  CMD_FILTER_VAR,
  CMD_FREQ,
  CMD_HEARTBEAT,
  CMD_LOCK,
  CMD_METER,
  CMD_MICGAIN,
  CMD_MODE,
  CMD_MOVE,
  CMD_MOVETO,
  CMD_MOX,
  CMD_MUTE_RX,
  CMD_NOISE,
  CMD_PAN,
  CMD_PATRIM,
  CMD_PREEMP,
  CMD_PSATT,
  CMD_PSONOFF,
  CMD_PSPARAMS,
  CMD_PSRESET,
  CMD_PSRESUME,
  CMD_RADIOMENU,
  CMD_RCL,
  CMD_RECEIVERS,
  CMD_REGION,
  CMD_RESTART,
  CMD_RFGAIN,
  CMD_RIT,
  CMD_RIT_STEP,
  CMD_RXFFT,
  CMD_RXMENU,
  CMD_RX_DISPLAY,
  CMD_RX_EQ,
  CMD_RX_FILTER_CUT,
  CMD_RX_FPS,
  CMD_RX_SELECT,
  CMD_RX_SPECTRUM,
  CMD_SAMPLE_RATE,
  CMD_SAT,
  CMD_SCREEN,
  CMD_SIDETONEFREQ,
  CMD_SOAPY_AGC,
  CMD_SOAPY_RXANT,
  CMD_SOAPY_TXANT,
  CMD_SPLIT,
  CMD_SQUELCH,
  CMD_START_RADIO,
  CMD_STEP,
  CMD_STORE,
  CMD_TOGGLE_MOX,
  CMD_TOGGLE_TUNE,
  CMD_TUNE,
  CMD_TWOTONE,
  CMD_TXFFT,
  CMD_TXFILTER,
  CMD_TXMENU,
  CMD_TX_DISPLAY,
  CMD_TX_EQ,
  CMD_TX_FILTER_CUT,
  CMD_TX_FPS,
  CMD_TX_SPECTRUM,
  CMD_VFO_A_TO_B,
  CMD_VFO_B_TO_A,
  CMD_VFO_STEPSIZE,
  CMD_VFO_SWAP,
  CMD_VOLUME,
  CMD_VOX,
  CMD_XIT,
  CMD_XVTR,
  CMD_ZOOM,
  INFO_ADC,
  INFO_BAND,
  INFO_BANDSTACK,
  INFO_DISPLAY,
  INFO_MEMORY,
  INFO_PS,
  INFO_RADIO,
  INFO_RECEIVER,
  INFO_RXAUDIO,
  INFO_RX_SPECTRUM,
  INFO_TX_SPECTRUM,
  INFO_TRANSMITTER,
  INFO_TXAUDIO,
  INFO_VFO,
  CLIENT_SERVER_COMMANDS,
};

#define CLIENT_SERVER_VERSION 0x01250001 // 32-bit version number
#define SPECTRUM_DATA_SIZE 4096          // Maximum width of a panadapter
#define AUDIO_DATA_SIZE 512              // 512 stereo samples

typedef struct _remote_client {
  int running;
  int authorised;
  int socket;
  socklen_t address_length;
  struct sockaddr_in address;
  guint timer_id;
  int send_rx_spectrum[8];
  int send_tx_spectrum;
} REMOTE_CLIENT;

typedef struct __attribute__((__packed__)) _header {
  uint8_t sync[4];
  uint16_t data_type;
  //
  // two bytes and two shorts that can be used
  // to store data for commands that need that little,
  // so these commands only need the header
  //
  uint8_t b1;
  uint8_t b2;
  uint16_t s1;
  uint16_t s2;
} HEADER;

//
// This is the data that is changed in the radio menu
// and needs no special processing. Note state variables
// like "anan10e" (which, if changed,  requires re-starting
// the protocol) are sent in dedicated commands. We also
// include some other radio data (new_pa_board)
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
  uint8_t  mute_spkr_xmit;
  uint8_t  hl2_audio_codec;
  uint8_t  soapy_iqswap;
  uint8_t  enable_tx_inhibit;
  uint8_t  enable_auto_tune;
  uint8_t  new_pa_board;
  uint8_t  tx_out_of_band_allowed;
  uint8_t  OCtune;
  //
  uint16_t rx_gain_calibration;
  uint16_t OCfull_tune_time;
  uint16_t OCmemory_tune_time;
  //
  uint64_t frequency_calibration;
} RADIOMENU_DATA;

//
// This is the data changed in the RX  menu
// which requires no special processing in P1
// and not more than "packet schedules" in P2.
// Some variables
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
  uint8_t alex_attenuation;
  uint8_t adc0_filter_bypass;
  uint8_t adc1_filter_bypass;
} RXMENU_DATA;

//
// This is data from the TX menu that requires no special
// processing in P1 (but possibly packet schedules in P2).
//
typedef struct __attribute__((__packed__)) _txmenu_data {
  HEADER header;
  uint8_t  tune_drive;
  uint8_t  tune_use_drive;
  uint8_t  swr_protection;
  uint8_t  mic_boost;
  uint8_t  mic_linein;
  mydouble linein_gain;
  mydouble swr_alarm;
} TXMENU_DATA;

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
  uint8_t  RxAntenna;
  uint8_t  TxAntenna;
  uint8_t  disablePA;
  uint8_t  current;
  uint16_t gaincalib;
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
  uint8_t sat_mode;
  uint8_t ctun;
  uint8_t mode;
  uint8_t filter;
  uint8_t bd;
  uint8_t alt_ctun;
  uint8_t alt_mode;
  uint8_t alt_filter;
  uint8_t alt_bd;
  uint8_t ctcss_enabled;
  uint8_t ctcss;
  //
  uint16_t deviation;
  uint16_t alt_deviation;
  //
  uint64_t frequency;
  uint64_t ctun_frequency;
  uint64_t alt_frequency;
  uint64_t alt_ctun_frequency;
} MEMORY_DATA;

//
// This is the global data sent *once* by the server
// to the client when the connection has been established.
//
typedef struct __attribute__((__packed__)) _radio_data {
  HEADER header;
  char name[64];
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
  uint8_t  mute_rx_while_transmitting;
  uint8_t  mute_spkr_amp;
  uint8_t  mute_spkr_xmit;
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
  uint8_t  diversity_enabled;
  uint8_t  soapy_iqswap;
  uint8_t  soapy_rx1_antennas;
  uint8_t  soapy_rx2_antennas;
  uint8_t  soapy_tx_antennas;
  uint8_t  soapy_rx1_gains;
  uint8_t  soapy_rx2_gains;
  uint8_t  soapy_tx_gains;
  uint8_t  soapy_tx_channels;
  uint8_t  soapy_rx1_has_automatic_gain;
  uint8_t  soapy_rx2_has_automatic_gain;
  //
  char     soapy_hardware_key[64];
  char     soapy_driver_key[64];
  char     soapy_rx1_antenna[8][64];
  char     soapy_rx2_antenna[8][64];
  char     soapy_tx_antenna[8][64];
  char     soapy_rx1_gain_elem_name[8][64];
  char     soapy_rx2_gain_elem_name[8][64];
  char     soapy_tx_gain_elem_name[8][64];
  //
  uint16_t pa_power;
  uint16_t OCfull_tune_time;
  uint16_t OCmemory_tune_time;
  uint16_t cw_keyer_sidetone_frequency;
  uint16_t rx_gain_calibration;
  uint16_t device;
  //
  mydouble drive_min;
  mydouble drive_max;
  mydouble drive_digi_max;
  mydouble pa_trim[11];
  mydouble div_gain;
  mydouble div_phase;
  mydouble soapy_rx1_gain_step;
  mydouble soapy_rx1_gain_min;
  mydouble soapy_rx1_gain_max;
  mydouble soapy_rx2_gain_step;
  mydouble soapy_rx2_gain_min;
  mydouble soapy_rx2_gain_max;
  mydouble soapy_tx_gain_step;
  mydouble soapy_tx_gain_min;
  mydouble soapy_tx_gain_max;
  mydouble soapy_rx1_gain_elem_step[8];
  mydouble soapy_rx1_gain_elem_min[8];
  mydouble soapy_rx1_gain_elem_max[8];
  mydouble soapy_rx2_gain_elem_step[8];
  mydouble soapy_rx2_gain_elem_min[8];
  mydouble soapy_rx2_gain_elem_max[8];
  mydouble soapy_tx_gain_elem_step[8];
  mydouble soapy_tx_gain_elem_min[8];
  mydouble soapy_tx_gain_elem_max[8];
  //
  uint64_t frequency_calibration;
  uint64_t soapy_radio_sample_rate;
  uint64_t radio_frequency_min;
  uint64_t radio_frequency_max;
} RADIO_DATA;

//
// This is all the data that will be used by
// a tx_set_compressor() call on the server side
//
typedef struct __attribute__((__packed__)) _compressor_data {
  HEADER header;
  uint8_t  compressor;
  uint8_t  cfc;
  uint8_t  cfc_eq;
  mydouble compressor_level;
  mydouble cfc_freq[11];
  mydouble cfc_lvl[11];
  mydouble cfc_post[11];
} COMPRESSOR_DATA;

//
// This is all the data that will be used by a
// tx_set_dexp() call on the server side
//
typedef struct __attribute__((__packed__)) _dexp_data {
  HEADER header;
  uint8_t  dexp;
  uint8_t  dexp_filter;
  uint16_t dexp_trigger;
  uint16_t dexp_exp;
  uint16_t dexp_filter_low;
  uint16_t dexp_filter_high;
  mydouble dexp_tau;
  mydouble dexp_attack;
  mydouble dexp_release;
  mydouble dexp_hold;
  mydouble dexp_hyst;
} DEXP_DATA;

typedef struct __attribute__((__packed__)) _adc_data {
  HEADER header;
  uint8_t adc;
  uint8_t random;
  uint8_t dither;
  uint8_t preamp;
  uint8_t antenna;
  uint8_t alex_attenuation;
  uint8_t overload;
  uint8_t filter_bypass;
  //
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
  uint8_t  antenna;
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
  uint8_t  swr_protection;
  uint8_t  usr_rx_filter;
  //
  uint16_t fps;
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
  uint16_t tx_default_filter_low;
  uint16_t tx_default_filter_high;
  //
  uint64_t fft_size;
  //
  mydouble swr_alarm;
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
  uint8_t squelch_enable;
  uint8_t binaural;
  uint8_t eq_enable;
  uint8_t smetermode;
  uint8_t low_latency;
  uint8_t pan;
  //
  uint16_t fps;
  uint16_t filter_low;
  uint16_t filter_high;
  uint16_t deviation;
  uint16_t width;
  //
  mydouble cA;
  mydouble cB;
  mydouble cAp;
  mydouble cBp;
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
  uint8_t  xit_enabled;
  uint8_t  cwAudioPeakFilter;
  //
  uint16_t rit_step;
  uint16_t deviation;
  //
  uint64_t frequency;
  uint64_t ctun_frequency;
  uint64_t rit;
  uint64_t xit;
  uint64_t lo;
  uint64_t offset;
  uint64_t step;
} VFO_DATA;

//
// Data for a panadapter. Can be used for receivers (id = 0,1)
// or for the transmitter (id = 8). Sent periodically as long as
// is is enabled for this panadapter via CMD_??_SPECTRUM.
// Note that this also contains high-frequency data such as
// RX S-meter, TX power/ALC/swr and PURESIGNAL status data, toghether
// with VFO frequencies for "quick" VFO update.
//
typedef struct __attribute__((__packed__)) _spectrum_data {
  HEADER header;
  uint8_t id;
  uint8_t avail;
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
  mydouble cA;
  mydouble cB;
  mydouble cAp;
  mydouble cBp;
  //
  uint8_t sample[SPECTRUM_DATA_SIZE];
} SPECTRUM_DATA;

//
// The difference between RX and TX audio is that the latter is mono
// (this saves Client==>Server bandwidth)
//
typedef struct __attribute__((__packed__)) _txaudio_data {
  HEADER header;
  uint8_t rx;
  uint16_t numsamples;
  uint16_t samples[AUDIO_DATA_SIZE];
} TXAUDIO_DATA;

typedef struct __attribute__((__packed__)) _rxaudio_data {
  HEADER header;
  uint8_t rx;
  uint16_t numsamples;
  uint16_t samples[AUDIO_DATA_SIZE * 2];
} RXAUDIO_DATA;


//
// There are many more such parameters, but they currently
// cannot be changed.
//
typedef struct __attribute__((__packed__)) _ps_params {
  HEADER header;
  uint8_t  ps_ptol;
  uint8_t  ps_oneshot;
  uint8_t  ps_map;
  mydouble ps_setpk;
} PS_PARAMS;

typedef struct __attribute__((__packed__)) _display_data {
  HEADER header;
  uint8_t adc0_overload;
  uint8_t adc1_overload;
  uint8_t high_swr_seen;
  uint8_t tx_fifo_overrun;
  uint8_t tx_fifo_underrun;
  uint8_t TxInhibit;
  uint8_t txzero;
  uint8_t capture_state;
  uint16_t exciter_power;
  uint16_t ADC0;
  uint16_t ADC1;
  uint16_t sequence_errors;
  uint32_t capture_record_pointer;
  uint32_t capture_replay_pointer;
} DISPLAY_DATA;

typedef struct __attribute__((__packed__)) _ps_data {
  HEADER header;
  uint16_t psinfo[16];
  uint16_t attenuation;
  mydouble ps_getmx;
} PS_DATA;

typedef struct __attribute__((__packed__)) _patrim_data {
  HEADER header;
  int pa_power;
  mydouble pa_trim[11];
} PATRIM_DATA;

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

#define HPSDR_PWD_LEN 64
extern int hpsdr_server;
extern int server_stops_protocol;
extern char hpsdr_pwd[HPSDR_PWD_LEN];

extern int client_socket;
extern int start_spectrum(void *data);
extern void start_vfo_timer(void);
extern gboolean remote_started;

extern REMOTE_CLIENT remoteclient;

extern int listen_port;

extern int create_hpsdr_server(void);
extern int destroy_hpsdr_server(void);

extern int radio_connect_remote(char *host, int port, const char *pwd);
extern void remote_rxaudio(const RECEIVER *rx, short left_sample, short right_sample);
extern void server_tx_audio(short sample);
extern short remote_get_mic_sample();
extern void  send_rxspectrum(int id);
extern void  send_txspectrum(void);

extern void send_adc(int s, int id, int adc);
extern void send_adc_data(int sock, int i);
extern void send_afbinaural(int s, const RECEIVER *rx);
extern void send_agc(int s, int rx, int agc);
extern void send_agc_gain(int s, const RECEIVER *rx);
extern void send_am_carrier(int s);
extern void send_anan10E(int s, int new);
extern void send_attenuation(int s, int rx, int attenuation);
extern void send_band(int s, int rx, int band);
extern void send_band_data(int s, int band);
extern void send_bandstack(int s, int old, int new);
extern void send_bandstack_data(int s, int band, int stack);
extern void send_capture(int s);
extern void send_ctcss(int s);
extern void send_ctun(int s, int vfo, int ctun);
extern void send_cw(int s, int state,  int wait);
extern void send_cwpeak(int s, int id, int state);
extern void send_dac_data(int sock);
extern void send_deviation(int s, int id, int state);
extern void send_dexp(int s);
extern void send_digidrivemax(int s);
extern void send_display(int s, int id);
extern void send_diversity(int s, int enabled, double gain, double phase);
extern void send_drive(int s, double value);
extern void send_duplex(int s, int state);
extern void send_eq(int s, int id);
extern void send_filter_board(int s, int filter_board);
extern void send_rx_filter_cut(int s, int rx);
extern void send_tx_filter_cut(int s);
extern void send_filter_sel(int s, int vfo, int filter);
extern void send_filter_var(int s, int mode, int filter);
extern void send_rxfps(int s, int rx, int fps);
extern void send_txfps(int s, int fps);
extern void send_heartbeat(int s);
extern void send_lock(int s, int lock);
extern void send_memory_data(int sock, int index);
extern void send_meter(int s, int metermode, int alcmode);
extern void send_micgain(int s, double gain);
extern void send_mode(int s, int rx, int mode);
extern void send_mute_rx(int s, int id, int mute);
extern void send_noise(int s, const RECEIVER *rx);
extern void send_pan(int s, const RECEIVER *rx);
extern void send_patrim(int s);
extern void send_preemp(int s);
extern void send_psatt(int s);
extern void send_psonoff(int s, int state);
extern void send_psparams(int s, const TRANSMITTER *tx);
extern void send_psreset(int s);
extern void send_psresume(int s);
extern void send_mox(int s, int state);
extern void send_radio_data(int sock);
extern void send_radiomenu(int s);
extern void send_recall(int s, int index);
extern void send_receiver_data(int sock, int rx);
extern void send_receivers(int s, int receivers);
extern void send_region(int s, int region);
extern void send_restart(int s);
extern void send_rfgain(int s, int rx, double gain);
extern void send_rit(int s, int id);
extern void send_rit_step(int s, int v, int step);
extern void send_rx_data(int s, int id);
extern void send_rx_fft(int s, const RECEIVER *rx);
extern void send_rx_select(int s, int rx);
extern void send_rxmenu(int s, int id);
extern void send_sample_rate(int s, int rx, int sample_rate);
extern void send_sat(int s, int sat);
extern void send_screen(int s, int hstack, int width);
extern void send_sidetone_freq(int s, int freq);
extern void send_soapy_rxant(int s, int id);
extern void send_soapy_txant(int s);
extern void send_soapy_agc(int s, int id);
extern void send_split(int s, int state);
extern void send_squelch(int s, int rx, int enable, double squelch);
extern void send_start_radio(int s);
extern void send_startstop_rxspectrum(int s, int id, int state);
extern void send_startstop_txspectrum(int s, int state);
extern void send_store(int s, int index);
extern void send_swap_iq(int s, int swap_iq);
extern void send_toggle_mox(int s);
extern void send_toggle_tune(int s);
extern void send_tune(int s, int state);
extern void send_twotone(int s, int state);
extern void send_tx_compressor(int s);
extern void send_tx_data(int s);
extern void send_tx_fft(int s, const TRANSMITTER *tx);
extern void send_txfilter(int s);
extern void send_txmenu(int s);
extern void send_varfilter_data(int s);
extern void send_vfo_atob(int sock);
extern void send_vfo_btoa(int sock);
extern void send_vfo_data(int sock, int v);
extern void send_vfo_frequency(int s, int v, long long hz);
extern void send_vfo_move_to(int s, int v, long long hz, int round);
extern void send_vfo_step(int s, int v, int steps);
extern void send_vfo_stepsize(int s, int v, int stepsize);
extern void send_vfo_swap(int sock);
extern void send_volume(int s, int rx, double volume);
extern void send_vox(int s, int state);
extern void send_xit(int s, int id);
extern void send_xvtr_changed(int s);
extern void send_zoom(int s, const RECEIVER *rx);
extern void update_vfo_move(int v, long long hz, int round);
extern void update_vfo_step(int v, int steps);

extern int recv_bytes(int s, char *buffer, int bytes);
extern int send_bytes(int s, char *buffer, int bytes);
extern void generate_pwd_hash(unsigned char *s, unsigned char *hash, const char *pwd);
//
// htonll and friends are macros, and this may have
// side effects. Better use functions that operate
// on simple variables (not expressions).
// Futhermore, explicit casting is done here once for all
//

static inline uint64_t to_double(double x) {
  uint64_t u64 = (x + 9.0E8) * 1.0E10;
#ifdef __APPLE__
  uint64_t ret = htonll(u64);
#else
  uint64_t ret = htobe64(u64);
#endif
  return ret;
}

static inline uint64_t to_ll(long long x) {
#ifdef __APPLE__
  uint64_t ret = htonll(x);
#else
  uint64_t ret = htobe64(x);
#endif
  return ret;
}

static inline uint32_t to_int(int x) {
  int32_t s32 = x;
  uint32_t ret = htonl(s32);
  return ret;
}

static inline uint16_t to_short(int x) {
  int16_t s16 = x;
  uint16_t ret = htons(s16);
  return ret;
}

static inline double from_double(uint64_t y) {
#ifdef __APPLE__
  uint64_t u64 = ntohll(y);
#else
  uint64_t u64 = be64toh(y);
#endif
  return (1.0E-10 * u64 - 9.0E8);
}

static inline long long from_ll(uint64_t y) {
#ifdef __APPLE__
  uint64_t u64 = ntohll(y);
#else
  uint64_t u64 = be64toh(y);
#endif
  return (long long) u64;
}

static inline int from_int(uint32_t y) {
  int32_t s32 = ntohl(y);
  return (int) s32;
}

static inline int from_short(uint16_t y) {
  int16_t s16 = ntohs(y);
  return (int) s16;
}

static const uint8_t syncbytes[4] = { 0xFA, 0xFA, 0xAF, 0xAF };

static inline void SYNC(uint8_t *sync) {
  *sync++ = 0xFA;
  *sync++ = 0xFA;
  *sync++ = 0xAF;
  *sync++ = 0xAF;
}
#endif
