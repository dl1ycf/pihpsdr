/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

//
// If PortAudio is used instead of ALSA (e.g. on MacOS),
// this file is not used (and replaced by portaudio.c).

#ifndef PORTAUDIO 

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>

#include <alsa/asoundlib.h>

#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "audio.h"
#include "mode.h"
#include "new_protocol.h"
#include "old_protocol.h"
#ifdef SOAPYSDR
#include "soapy_protocol.h"
#endif
#include "vfo.h"

int audio = 0;
int mic_buffer_size = 720; // samples (both left and right)
static GMutex audio_mutex;

static snd_pcm_t *record_handle=NULL;
static snd_pcm_format_t record_audio_format;

static void *mic_buffer=NULL;

static GThread *mic_read_thread_id=NULL;

static int running=FALSE;

#define FORMATS 3
static snd_pcm_format_t formats[3]={
  SND_PCM_FORMAT_FLOAT_LE,
  SND_PCM_FORMAT_S32_LE,
  SND_PCM_FORMAT_S16_LE};

static void *mic_read_thread(void *arg);


int n_input_devices;
AUDIO_DEVICE input_devices[MAX_AUDIO_DEVICES];
int n_output_devices;
AUDIO_DEVICE output_devices[MAX_AUDIO_DEVICES];

//
// Ring buffer for "local microphone" samples
// NOTE: lead large buffer for some "loopback" devices which produce
//       samples in large chunks if fed from digimode programs.
//
#define MICRINGLEN 6000
float  *mic_ring_buffer=NULL;
int     mic_ring_read_pt=0;
int     mic_ring_write_pt=0;

int audio_open_output(RECEIVER *rx) {
  int err;
  unsigned int rate=48000;
  unsigned int channels=2;
  int soft_resample=1;
  unsigned int latency=125000;

  if(rx->audio_name==NULL) {
    rx->local_audio=0;
    return -1;
  }

g_print("audio_open_output: rx=%d %s buffer_size=%d\n",rx->id,rx->audio_name,rx->local_audio_buffer_size);

  int i;
  char hw[128];
 

  i=0;
  while(i<127 && rx->audio_name[i]!=' ') {
    hw[i]=rx->audio_name[i];
    i++;
  }
  hw[i]='\0';
  
g_print("audio_open_output: hw=%s\n",hw);

  for(i=0;i<FORMATS;i++) {
    g_mutex_lock(&rx->local_audio_mutex);
    if ((err = snd_pcm_open (&rx->playback_handle, hw, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
      g_print("audio_open_output: cannot open audio device %s (%s)\n", 
              hw,
              snd_strerror (err));
      g_mutex_unlock(&rx->local_audio_mutex);
      return err;
    }
g_print("audio_open_output: handle=%p\n",rx->playback_handle);

g_print("audio_open_output: trying format %s (%s)\n",snd_pcm_format_name(formats[i]),snd_pcm_format_description(formats[i]));
    if ((err = snd_pcm_set_params (rx->playback_handle,formats[i],SND_PCM_ACCESS_RW_INTERLEAVED,channels,rate,soft_resample,latency)) < 0) {
      g_print("audio_open_output: snd_pcm_set_params failed: %s\n",snd_strerror(err));
      g_mutex_unlock(&rx->local_audio_mutex);
      audio_close_output(rx);
      continue;
    } else {
g_print("audio_open_output: using format %s (%s)\n",snd_pcm_format_name(formats[i]),snd_pcm_format_description(formats[i]));
      rx->local_audio_format=formats[i];
      break;
    }
  }

  if(i>=FORMATS) {
    g_print("audio_open_output: cannot find usable format\n");
    return err;
  }

  rx->local_audio_buffer_offset=0;
  rx->local_audio_cw=0;
  switch(rx->local_audio_format) {
    case SND_PCM_FORMAT_S16_LE:
g_print("audio_open_output: local_audio_buffer: size=%d sample=%ld\n",rx->local_audio_buffer_size,sizeof(gint16));
      rx->local_audio_buffer=g_new(gint16,2*rx->local_audio_buffer_size);
        break;
    case SND_PCM_FORMAT_S32_LE:
g_print("audio_open_output: local_audio_buffer: size=%d sample=%ld\n",rx->local_audio_buffer_size,sizeof(gint32));
      rx->local_audio_buffer=g_new(gint32,2*rx->local_audio_buffer_size);
      break;
    case SND_PCM_FORMAT_FLOAT_LE:
g_print("audio_open_output: local_audio_buffer: size=%d sample=%ld\n",rx->local_audio_buffer_size,sizeof(gfloat));
      rx->local_audio_buffer=g_new(gfloat,2*rx->local_audio_buffer_size);
      break;
  }
  
  g_print("audio_open_output: rx=%d audio_device=%d handle=%p buffer=%p size=%d\n",rx->id,rx->audio_device,rx->playback_handle,rx->local_audio_buffer,rx->local_audio_buffer_size);

  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}
	
int audio_open_input() {
  int err;
  unsigned int rate=48000;
  unsigned int channels=1;
  int soft_resample=1;
  unsigned int latency=125000;
  char hw[64];
  int i;

  if(transmitter->microphone_name==NULL) {
    transmitter->local_microphone=0;
    return -1;
  }

g_print("audio_open_input: %s\n",transmitter->microphone_name);
  
  mic_buffer_size = 256;

  g_print("audio_open_input: mic_buffer_size=%d\n",mic_buffer_size);
  i=0;
  while(i<63 && transmitter->microphone_name[i]!=' ') {
    hw[i]=transmitter->microphone_name[i];
    i++;
  }
  hw[i]='\0';


  g_print("audio_open_input: hw=%s\n",hw);

  for(i=0;i<FORMATS;i++) {
    g_mutex_lock(&audio_mutex);
    if ((err = snd_pcm_open (&record_handle, hw, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0) {
      g_print("audio_open_input: cannot open audio device %s (%s)\n",
              hw,
              snd_strerror (err));
      record_handle=NULL;
      g_mutex_unlock(&audio_mutex);
      return err;
    }
g_print("audio_open_input: handle=%p\n",record_handle);

g_print("audio_open_input: trying format %s (%s)\n",snd_pcm_format_name(formats[i]),snd_pcm_format_description(formats[i]));
    if ((err = snd_pcm_set_params (record_handle,formats[i],SND_PCM_ACCESS_RW_INTERLEAVED,channels,rate,soft_resample,latency)) < 0) {
      g_print("audio_open_input: snd_pcm_set_params failed: %s\n",snd_strerror(err));
      g_mutex_unlock(&audio_mutex);
      audio_close_input();
      continue;
    } else {
g_print("audio_open_input: using format %s (%s)\n",snd_pcm_format_name(formats[i]),snd_pcm_format_description(formats[i]));
      record_audio_format=formats[i];
      break;
    }
  }

  if(i>=FORMATS) {
    g_print("audio_open_input: cannot find usable format\n");
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return err;
  }

g_print("audio_open_input: format=%d\n",record_audio_format);

  switch(record_audio_format) {
    case SND_PCM_FORMAT_S16_LE:
g_print("audio_open_input: mic_buffer: size=%d channels=%d sample=%ld bytes\n",mic_buffer_size,channels,sizeof(gint16));
      mic_buffer=g_new(gint16,mic_buffer_size);
      break;
    case SND_PCM_FORMAT_S32_LE:
g_print("audio_open_input: mic_buffer: size=%d channels=%d sample=%ld bytes\n",mic_buffer_size,channels,sizeof(gint32));
      mic_buffer=g_new(gint32,mic_buffer_size);
      break;
    case SND_PCM_FORMAT_FLOAT_LE:
g_print("audio_open_input: mic_buffer: size=%d channels=%d sample=%ld bytes\n",mic_buffer_size,channels,sizeof(gfloat));
      mic_buffer=g_new(gfloat,mic_buffer_size);
      break;
  }

g_print("audio_open_input: allocating ring buffer\n");
  mic_ring_buffer=(float *) g_new(float,MICRINGLEN);
  mic_ring_read_pt = mic_ring_write_pt=0;
  if (mic_ring_buffer == NULL) {
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return -1;
  }

g_print("audio_open_input: creating mic_read_thread\n");
  GError *error;
  mic_read_thread_id = g_thread_try_new("microphone",mic_read_thread,NULL,&error);
  if(!mic_read_thread_id ) {
    g_print("g_thread_new failed on mic_read_thread: %s\n",error->message);
    g_mutex_unlock(&audio_mutex);
    audio_close_input();
    return -1;
  }

  g_mutex_unlock(&audio_mutex);
  return 0;
}

void audio_close_output(RECEIVER *rx) {
g_print("audio_close_output: rx=%d handle=%p buffer=%p\n",rx->id,rx->playback_handle,rx->local_audio_buffer);
  g_mutex_lock(&rx->local_audio_mutex);
  if(rx->playback_handle!=NULL) {
    snd_pcm_close (rx->playback_handle);
    rx->playback_handle=NULL;
  }
  if(rx->local_audio_buffer!=NULL) {
    g_free(rx->local_audio_buffer);
    rx->local_audio_buffer=NULL;
  }
  g_mutex_unlock(&rx->local_audio_mutex);
}

void audio_close_input() {
g_print("audio_close_input\n");
  running=FALSE;
  g_mutex_lock(&audio_mutex);
  if(mic_read_thread_id!=NULL) {
g_print("audio_close_input: wait for thread to complete\n");
    g_thread_join(mic_read_thread_id);
    mic_read_thread_id=NULL;
  }
  if(record_handle!=NULL) {
g_print("audio_close_input: snd_pcm_close\n");
    snd_pcm_close (record_handle);
    record_handle=NULL;
  }
  if(mic_buffer!=NULL) {
g_print("audio_close_input: free mic buffer\n");
    g_free(mic_buffer);
    mic_buffer=NULL;
  }
  if (mic_ring_buffer != NULL) {
    g_free(mic_ring_buffer);
  }
  g_mutex_unlock(&audio_mutex);
}

//
// This is for writing a CW side tone.
// To keep sidetone latencies low, only use 256 samples of the
// RX audio buffer, and slightly shorten periods of silence
// as long as the delay is too large.
//
// There are three parameters to control the latency:
//
// short_audio_buffer_size  length of piHPSDR's audio buffer (default: 256)
// delay low water mark     stretch pauses if delay falls below  (default: 896)
// delay high water mark    shorten pauses if delay exceeds (default: 1152)
//
// By default, it is tried to keep the delay in the range 1024 +/- 128
// Note that when sending the buffer, delay "jumps" by the buffer size
//

int cw_audio_write(RECEIVER *rx, float sample){
  snd_pcm_sframes_t delay;
  long rc;
  float *float_buffer;
  gint32 *long_buffer;
  gint16 *short_buffer;
  static int count=0;
  int    short_audio_buffer_size;
	
  g_mutex_lock(&rx->local_audio_mutex);
  if(rx->playback_handle!=NULL && rx->local_audio_buffer!=NULL) {

    if (rx->local_audio_cw == 0 && cw_keyer_sidetone_volume > 0) {
      //
      // first invocation of cw_audio_write e.g. after a RX/TX transition:
      // clear audio buffer local to pihpsdr, rewind ALSA buffer
      // if contains too many samples
      //
      // if the keyer side tone volume is zero, then we need not care
      // about side tone latency at all.
      //
      rx->local_audio_cw=1;
      rx->local_audio_buffer_offset=0;
      if (snd_pcm_delay(rx->playback_handle, &delay) == 0) {
         if (delay > 1024) snd_pcm_rewind(rx->playback_handle, delay-1024);
      }
      count=0;
    }

    short_audio_buffer_size=rx->local_audio_buffer_size;
    if (rx->local_audio_cw) {
      //
      // For CW side tone, use short audio buffers
      //
      if (short_audio_buffer_size > 256) short_audio_buffer_size = 256;
      if (sample != 0.0) count=0;  // count upwards during silence
    }

    //
    // Put sample into buffer
    //
    switch(rx->local_audio_format) {
      case SND_PCM_FORMAT_S16_LE:
        short_buffer=(gint16 *)rx->local_audio_buffer;
        short_buffer[rx->local_audio_buffer_offset*2]=(gint16)(sample*32767.0F);
        short_buffer[(rx->local_audio_buffer_offset*2)+1]=(gint16)(sample*32767.0F);
        break;
      case SND_PCM_FORMAT_S32_LE:
        long_buffer=(gint32 *)rx->local_audio_buffer;
        long_buffer[rx->local_audio_buffer_offset*2]=(gint32)(sample*4294967295.0F);
        long_buffer[(rx->local_audio_buffer_offset*2)+1]=(gint32)(sample*4294967295.0F);
        break;
      case SND_PCM_FORMAT_FLOAT_LE:
        float_buffer=(float *)rx->local_audio_buffer;
        float_buffer[rx->local_audio_buffer_offset*2]=sample;
        float_buffer[(rx->local_audio_buffer_offset*2)+1]=sample;
        break;
    }
    rx->local_audio_buffer_offset++;

    if (++count >= 16 && rx->local_audio_cw) {
      //
      // We have just seen 16 zero samples, so this is the right place
      // to adjust the buffer filling.
      // If buffer gets too full   ==> skip the sample
      // If buffer gets too lempty ==> insert zero sample
      //
      if (snd_pcm_delay(rx->playback_handle,&delay) == 0) {
        if (delay > 1152) {
          // delete the last sample
          rx->local_audio_buffer_offset--;
        }
        if ((delay < 896) && (rx->local_audio_buffer_offset < short_audio_buffer_size)) {
          // insert another zero sample
          switch(rx->local_audio_format) {
            case SND_PCM_FORMAT_S16_LE:
              short_buffer=(gint16 *)rx->local_audio_buffer;
              short_buffer[rx->local_audio_buffer_offset*2]=0;
              short_buffer[(rx->local_audio_buffer_offset*2)+1]=0;
              break;
            case SND_PCM_FORMAT_S32_LE:
              long_buffer=(gint32 *)rx->local_audio_buffer;
              long_buffer[rx->local_audio_buffer_offset*2]=0;
              long_buffer[(rx->local_audio_buffer_offset*2)+1]=0;
              break;
            case SND_PCM_FORMAT_FLOAT_LE:
              float_buffer=(float *)rx->local_audio_buffer;
              float_buffer[rx->local_audio_buffer_offset*2]=0.0;
              float_buffer[(rx->local_audio_buffer_offset*2)+1]=0.0;
              break;
          }
          rx->local_audio_buffer_offset++;
        }
      }
      count=0;
    }

    if(rx->local_audio_buffer_offset>=short_audio_buffer_size) {

      if ((rc = snd_pcm_writei (rx->playback_handle, rx->local_audio_buffer, rx->local_audio_buffer_offset)) != rx->local_audio_buffer_offset) {
        if(rc<0) {
          if(rc==-EPIPE) {
            if ((rc = snd_pcm_prepare (rx->playback_handle)) < 0) {
              g_print("audio_write: cannot prepare audio interface for use %ld (%s)\n", rc, snd_strerror (rc));
              g_mutex_unlock(&rx->local_audio_mutex);
              return rc;
            } else {
              // ignore short write
            }
          }
        }
      }
      rx->local_audio_buffer_offset=0;
    }
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

//
// if rx == active_receiver and while transmitting, DO NOTHING
// since cw_audio_write may be active
//

int audio_write(RECEIVER *rx,float left_sample,float right_sample) {
  snd_pcm_sframes_t delay;
  long rc;
  long trim;
  int txmode=get_tx_mode();
  float *float_buffer;
  gint32 *long_buffer;
  gint16 *short_buffer;

  //
  // We have to stop the stream here if a CW side tone may occur.
  // This might cause underflows, but we cannot use audio_write
  // and cw_audio_write simultaneously on the same device.
  // Instead, the side tone version will take over.
  // If *not* doing CW, the stream continues because we might wish
  // to listen to this rx while transmitting.
  //

  if (rx == active_receiver && isTransmitting() && (txmode==modeCWU || txmode==modeCWL)) {
    return 0;
  }

  // lock AFTER checking the "quick return" condition but BEFORE checking the pointers
  g_mutex_lock(&rx->local_audio_mutex);

  if(rx->playback_handle!=NULL && rx->local_audio_buffer!=NULL) {

    if (rx->local_audio_cw == 1) {
      //
      // we come from a CWTX-RX transition where we kept the ALSA audio buffer
      // at the low-water mark. So with this call, send one bunch of silence
      // to do a partial re-fill.
      //
      rx->local_audio_cw=0;
      switch(rx->local_audio_format) {
        case SND_PCM_FORMAT_S16_LE:
          bzero(rx->local_audio_buffer,2*sizeof(gint16)*rx->local_audio_buffer_size);
          break;
        case SND_PCM_FORMAT_S32_LE:
          bzero(rx->local_audio_buffer,2*sizeof(gint32)*rx->local_audio_buffer_size);
          break;
        case SND_PCM_FORMAT_FLOAT_LE:
          bzero(rx->local_audio_buffer,2*sizeof(gfloat)*rx->local_audio_buffer_size);
          break;
      }
      rx->local_audio_buffer_offset=0;
      //
      // In principle, this should be done after *all* TX/RX transition
      // unless in duplex mode, since the ALSA buffers are drained in this case.
      //
      if(snd_pcm_delay(rx->playback_handle,&delay)==0) {
        if (delay < 1024) {
          snd_pcm_writei (rx->playback_handle, rx->local_audio_buffer, rx->local_audio_buffer_size);
          //g_print("Audio output buffer partially refilled, delay was %ld\n", delay);
        }
      }
    }
    
    switch(rx->local_audio_format) {
      case SND_PCM_FORMAT_S16_LE:
        short_buffer=(gint16 *)rx->local_audio_buffer;
        short_buffer[rx->local_audio_buffer_offset*2]=(gint16)(left_sample*32767.0F);
        short_buffer[(rx->local_audio_buffer_offset*2)+1]=(gint16)(right_sample*32767.0F);
        break;
      case SND_PCM_FORMAT_S32_LE:
        long_buffer=(gint32 *)rx->local_audio_buffer;
        long_buffer[rx->local_audio_buffer_offset*2]=(gint32)(left_sample*4294967295.0F);
        long_buffer[(rx->local_audio_buffer_offset*2)+1]=(gint32)(right_sample*4294967295.0F);
        break;
      case SND_PCM_FORMAT_FLOAT_LE:
        float_buffer=(float *)rx->local_audio_buffer;
        float_buffer[rx->local_audio_buffer_offset*2]=left_sample;
        float_buffer[(rx->local_audio_buffer_offset*2)+1]=right_sample;
        break;
    }
    rx->local_audio_buffer_offset++;

    if(rx->local_audio_buffer_offset>=rx->local_audio_buffer_size) {

      trim=0;

      int max_delay=rx->local_audio_buffer_size*4;
      if(snd_pcm_delay(rx->playback_handle,&delay)==0) {
        if(delay>max_delay) {
          trim=delay-max_delay;
g_print("audio delay=%ld trim=%ld audio_buffer_size=%d\n",delay,trim,rx->local_audio_buffer_size);
        }
      }

      if(trim<rx->local_audio_buffer_size) {
        if ((rc = snd_pcm_writei (rx->playback_handle, rx->local_audio_buffer, rx->local_audio_buffer_size-trim)) != rx->local_audio_buffer_size-trim) {
          if(rc<0) {
            if(rc==-EPIPE) {
              if ((rc = snd_pcm_prepare (rx->playback_handle)) < 0) {
                g_print("audio_write: cannot prepare audio interface for use %ld (%s)\n", rc, snd_strerror (rc));
                rx->local_audio_buffer_offset=0;
                g_mutex_unlock(&rx->local_audio_mutex);
                return rc;
              }
            } else {
              // ignore short write
            }
          }
        }
      }
      rx->local_audio_buffer_offset=0;
    }
  }
  g_mutex_unlock(&rx->local_audio_mutex);
  return 0;
}

static void *mic_read_thread(gpointer arg) {
  int rc;
  gfloat *float_buffer;
  gint32 *long_buffer;
  gint16 *short_buffer;
  gfloat sample;
  int i;

g_print("mic_read_thread: mic_buffer_size=%d\n",mic_buffer_size);
g_print("mic_read_thread: snd_pcm_start\n");
  if ((rc = snd_pcm_start (record_handle)) < 0) {
    g_print("mic_read_thread: cannot start audio interface for use (%s)\n",
            snd_strerror (rc));
    return NULL;
  }

  running=TRUE;
  while(running) {
    if ((rc = snd_pcm_readi (record_handle, mic_buffer, mic_buffer_size)) != mic_buffer_size) {
      if(running) {
        if(rc<0) {
          g_print("mic_read_thread: read from audio interface failed (%s)\n",
                  snd_strerror (rc));
          //running=FALSE;
        } else {
          g_print("mic_read_thread: read %d\n",rc);
        }
      }
    } else {
      int newpt;
      // process the mic input
      for(i=0;i<mic_buffer_size;i++) {
        switch(record_audio_format) {
          case SND_PCM_FORMAT_S16_LE:
            short_buffer=(gint16 *)mic_buffer;
            sample=(gfloat)short_buffer[i]/32767.0f;
            break;
          case SND_PCM_FORMAT_S32_LE:
            long_buffer=(gint32 *)mic_buffer;
            sample=(gfloat)long_buffer[i]/4294967295.0f;
            break;
          case SND_PCM_FORMAT_FLOAT_LE:
            float_buffer=(gfloat *)mic_buffer;
            sample=float_buffer[i];
            break;
        }
        switch(protocol) {
          case ORIGINAL_PROTOCOL:
          case NEW_PROTOCOL:
#ifdef SOAPYSDR
          case SOAPYSDR_PROTOCOL:
#endif
	    //
	    // put sample into ring buffer
	    //
	    if (mic_ring_buffer != NULL) {
              // the "existence" of the ring buffer is now guaranteed for 1 msec,
	      // see audio_close_input().
	      newpt=mic_ring_write_pt +1;
	      if (newpt == MICRINGLEN) newpt=0;
	      if (newpt != mic_ring_read_pt) {
	        // buffer space available, do the write
	        mic_ring_buffer[mic_ring_write_pt]=sample;
	        // atomic update of mic_ring_write_pt
	        mic_ring_write_pt=newpt;
	      }
            }
            break;
          default:
            break;
        }
      }
    }
  }
g_print("mic_read_thread: exiting\n");
  return NULL;
}

//
// Utility function for retrieving mic samples
// from ring buffer
//
float audio_get_next_mic_sample() {
  int newpt;
  float sample;
  g_mutex_lock(&audio_mutex);
  if ((mic_ring_buffer == NULL) || (mic_ring_read_pt == mic_ring_write_pt)) {
    // no buffer, or nothing in buffer: insert silence
    sample=0.0;
  } else {
    newpt = mic_ring_read_pt+1;
    if (newpt == MICRINGLEN) newpt=0;
    sample=mic_ring_buffer[mic_ring_read_pt];
    // atomic update of read pointer
    mic_ring_read_pt=newpt;
  }
  g_mutex_unlock(&audio_mutex);
  return sample;
}

void audio_get_cards() {
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);
  int i;
  char *device_id;
  int card = -1;

g_print("audio_get_cards\n");

  g_mutex_init(&audio_mutex);
  n_input_devices=0;
  n_output_devices=0;

  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);
  while (snd_card_next(&card) >= 0 && card >= 0) {
    int err = 0;
    snd_ctl_t *handle;
    char name[20];
    snprintf(name, sizeof(name), "hw:%d", card);
    if ((err = snd_ctl_open(&handle, name, 0)) < 0) {
      continue;
    }

    if ((err = snd_ctl_card_info(handle, info)) < 0) {
      snd_ctl_close(handle);
      continue;
    }

    int dev = -1;

    while (snd_ctl_pcm_next_device(handle, &dev) >= 0 && dev >= 0) {
      snd_pcm_info_set_device(pcminfo, dev);
      snd_pcm_info_set_subdevice(pcminfo, 0);

      // input devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);
      if ((err = snd_ctl_pcm_info(handle, pcminfo)) == 0) {
        device_id=g_new(char,128);
        snprintf(device_id, 128, "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));
        if(n_input_devices<MAX_AUDIO_DEVICES) {
          input_devices[n_input_devices].name=g_new0(char,strlen(device_id)+1);
          strcpy(input_devices[n_input_devices].name,device_id);
          input_devices[n_input_devices].description=g_new0(char,strlen(device_id)+1);
          strcpy(input_devices[n_input_devices].description,device_id);
          input_devices[n_input_devices].index=0;  // not used
          n_input_devices++;
g_print("input_device: %s\n",device_id);
        }
	g_free(device_id);
      }

      // ouput devices
      snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);
      if ((err = snd_ctl_pcm_info(handle, pcminfo)) == 0) {
        device_id=g_new(char,128);
        snprintf(device_id, 128, "plughw:%d,%d %s", card, dev, snd_ctl_card_info_get_name(info));
        if(n_output_devices<MAX_AUDIO_DEVICES) {
          output_devices[n_output_devices].name=g_new0(char,strlen(device_id)+1);
          strcpy(output_devices[n_output_devices].name,device_id);
          output_devices[n_output_devices].description=g_new0(char,strlen(device_id)+1);
          strcpy(output_devices[n_output_devices].description,device_id);
          input_devices[n_output_devices].index=0; // not used
          n_output_devices++;
g_print("output_device: %s\n",device_id);
        }
	g_free(device_id);
      }
    }
    snd_ctl_close(handle);
  }

  // look for dmix and dsnoop
  void **hints, **n;
  char *name, *descr, *io;

  if (snd_device_name_hint(-1, "pcm", &hints) < 0)
    return;
  n = hints;
  while (*n != NULL) {
    name = snd_device_name_get_hint(*n, "NAME");
    descr = snd_device_name_get_hint(*n, "DESC");
    io = snd_device_name_get_hint(*n, "IOID");

    if(strncmp("dmix:", name, 5)==0) {
      if(n_output_devices<MAX_AUDIO_DEVICES) {
        //if(strncmp("dmix:CARD=ALSA",name,14)!=0) {
          output_devices[n_output_devices].name=g_new0(char,strlen(name)+1);
          strcpy(output_devices[n_output_devices].name,name);
          output_devices[n_output_devices].description=g_new0(char,strlen(descr)+1);
          i=0;
          while(i<strlen(descr) && descr[i]!='\n') {
            output_devices[n_output_devices].description[i]=descr[i];
            i++;
          }
          output_devices[n_output_devices].description[i]='\0';
          input_devices[n_output_devices].index=0;  // not used
          n_output_devices++;
g_print("output_device: name=%s descr=%s\n",name,descr);
        //}
      }
#ifdef INCLUDE_SNOOP
    } else if(strncmp("dsnoop:", name, 6)==0) {
      if(n_input_devices<MAX_AUDIO_DEVICES) {
        //if(strncmp("dmix:CARD=ALSA",name,14)!=0) {
          input_devices[n_input_devices].name=g_new0(char,strlen(name)+1);
          strcpy(input_devices[n_input_devices].name,name);
          input_devices[n_input_devices].description=g_new0(char,strlen(descr)+1);
          i=0;
          while(i<strlen(descr) && descr[i]!='\n') {
            input_devices[n_input_devices].description[i]=descr[i];
            i++;
          }
          input_devices[n_input_devices].description[i]='\0';
          input_devices[n_input_devices].index=0;  // not used
          n_input_devices++;
g_print("input_device: name=%s descr=%s\n",name,descr);
        //}
      }
#endif
    }

//
//  For these three items, use free() instead of g_free(),
//  since these have been allocated by ALSA via
//  snd_device_name_get_hint()
//
    if (name != NULL)
      free(name);
    if (descr != NULL)
      free(descr);
    if (io != NULL)
      free(io);
    n++;
  }
  snd_device_name_free_hint(hints);
}

char * audio_get_error_string(int err) {
  return (char *)snd_strerror(err);
}
#endif
