/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include <stdio.h>
#include <string.h>
#include "version.h"

char build_date[] = GIT_DATE;
char build_version[] = GIT_VERSION;
char build_commit[] = GIT_COMMIT;

char build_options[] =
#ifdef GPIO
  "GPIO "
#endif
#ifdef MIDI
  "MIDI "
#endif
#ifdef SATURN
  "SATURN "
#endif
#ifdef USBOZY
  "USBOZY "
#endif
#ifdef SOAPYSDR
  "SOAPYSDR "
#endif
#ifdef STEMLAB_DISCOVERY
  "STEMLAB "
#endif
#ifdef EXTNR
  "EXTNR "
#endif
#ifdef CLIENT_SERVER
  "SERVER "
#endif
  "";

char build_audio[] =
#ifdef ALSA
  "ALSA";
#endif
#ifdef PULSEAUDIO
  "PulseAudio";
#endif
#ifdef PORTAUDIO
  "PortAudio";
#endif
#if !defined(ALSA) && !defined(PORTAUDIO) && !defined(PULSEAUDIO)
  "(unkown)";
#endif

void version_info_print(char * cmdlp) {
  int copts = 0;
  if (!(strcmp(cmdlp,"-V")) || !(strcmp(cmdlp,"--version"))) {
    printf("Pihpsdr\n"); 
    printf("git_commit: %s\n", GIT_COMMIT);
    printf("git_date: %s\n", GIT_DATE);
    printf("fpga_min: %d.%d\n", FIRMWARE_MIN_MAJOR,FIRMWARE_MIN_MINOR);
    printf("fpga_max: %d.%d\n", FIRMWARE_MAX_MAJOR,FIRMWARE_MAX_MINOR);
#ifdef GPIO
    copts += 0x01;
#endif 
#ifdef MIDI
    copts += 0x02;
#endif 
#ifdef SATURN
    copts += 0x04;
#endif 
#ifdef EXTNR
    copts += 0x08;
#endif
#ifdef SERVER
    copts += 0x10;
#endif
#ifdef ALSA
    copts += 0x20;
#endif 
#ifdef PULSEAUDIO
    copts += 0x40;
#endif
#ifdef PORTAUDIO
    copts += 0x80;
#endif
  printf("options: 0x%x\n", copts);
  } else { // give help info
    printf("Regular start of Pihpsdr is without command line parameters!\n");
    printf("\'pihpsdr -V | --version\' returns version information.\n");
    printf("\'pihpsdr <something>' returns this help information.\n"); 
  }
}
