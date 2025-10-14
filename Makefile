#######################################################################################
# IMPORTANT NOTE:
# This file is no longer meant to be changed by the end user.
# To use non-default compile-time options please create a file4
# make.config.pihpsdr (see below)
#
#######################################################################################

#######################################################################################
#
# Default Compile-time options
#
#######################################################################################

GPIO=ON
MIDI=ON
SATURN=ON
USBOZY=OFF
SOAPYSDR=OFF
STEMLAB=OFF
AUDIO=PULSE
EXTENDED_NR=OFF
TTS=ON

#######################################################################################
#
# Explanation of compile time options
#
# GPIO         | If ON, compile with GPIO support (RaspPi only, needs libgpiod)
# MIDI         | If ON, compile with MIDI support
# SATURN       | If ON, compile with native SATURN/G2 XDMA support
# USBOZY       | If ON, piHPSDR can talk to legacy USB OZY radios (needs  libusb-1.0)
# SOAPYSDR     | If ON, piHPSDR can talk to radios via SoapySDR library
# STEMLAB      | If ON, piHPSDR can start SDR app on RedPitay via Web interface (needs libcurl)
# AUDIO        | If AUDIO=ALSA, use ALSA rather than PulseAudio on Linux
#
# If you want to use a non-default compile time option, write them
# into a file "make.config.pihpsdr". So, for example, if you want to
# disable GPIO and have AUDIO=ALSA, create a file make.config.pihpsdr in
# the pihpsdr directory with two lines that read
#
# GPIO=OFF
# AUDIO=ALSA
#
#######################################################################################

-include make.config.pihpsdr

# get the OS Name
UNAME_S := $(shell uname -s)

# Get git commit version and date
GIT_DATE := $(firstword $(shell git --no-pager show --date=short --format="%ai" --name-only))
GIT_VERSION := $(shell git describe --abbrev=0 --tags --always --dirty)
GIT_COMMIT := $(shell git log --pretty=format:"%h"  -1)

#
# Compile with warning level set to maximum. Note the check against "unintendend" fallthroughs
# in switch statements must be requested explicitly.
# Turn off complaints about deprecated functions (new GTK functions are marked deprecated in each
# release) and against unused parameters (those regularly occur in GTK callbacks).
#
CFLAGS?= -O3 -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter -Wno-deprecated-declarations
LINK?=   $(CC)

#
# The "official" way to compile+link with pthreads is now to use the -pthread option
# *both* for the compile and the link step.
#
CFLAGS+=-pthread -I./src
LINK+=-pthread

PKG_CONFIG = pkg-config

##############################################################################
# CPP_DEFINES and CPP_SOURCES are "filled" with all  possible options,
# so that everything is processed when running "cppcheck".
##############################################################################

CPP_DEFINES=
CPP_SOURCES=
CPP_INCLUDE=

WDSP_INCLUDE=-I./wdsp
WDSP_LIBS=wdsp/libwdsp.a `$(PKG_CONFIG) --libs fftw3`

##############################################################################
#
# Add support for extended noise reduction, if requested
# This implies that one compiles against a wdsp.h e.g. in /usr/local/include,
# and links with a WDSP shared lib e.g. in /usr/local/lib
#
# THIS IS GOING TO BE REMOVED! Reason: vu3rdd wdsp library still version 1.23
#
##############################################################################

ifeq ($(EXTENDED_NR), ON)
EXTNR_OPTIONS=-DEXTNR
WDSP_INCLUDE=
WDSP_LIBS=-lwdsp `$(PKG_CONFIG) --libs fftw3`
endif
CPP_DEFINES += -DEXTNR
CPP_INCLUDE +=$(WDSP_INCLUDE)

##############################################################################
#
# Settings for optional features, to be requested by un-commenting lines above
#
##############################################################################

##############################################################################
#
# disable GPIO and SATURN for MacOS, simply because it is not there
#
##############################################################################

ifeq ($(UNAME_S), Darwin)
GPIO=
SATURN=
endif

##############################################################################
#
# Add modules for MIDI if requested.
# Note these are different for Linux/MacOS
#
##############################################################################

ifeq ($(MIDI),ON)
MIDI_OPTIONS=-D MIDI
MIDI_HEADERS= src/midi.h src/midi_menu.h
ifeq ($(UNAME_S), Darwin)
MIDI_SOURCES= src/mac_midi.c src/midi2.c src/midi3.c src/midi_menu.c
MIDI_OBJS= src/mac_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -framework CoreMIDI -framework Foundation
endif
ifeq ($(UNAME_S), Linux)
MIDI_SOURCES= src/alsa_midi.c src/midi2.c src/midi3.c src/midi_menu.c
MIDI_OBJS= src/alsa_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -lasound
endif
endif
CPP_DEFINES += -DMIDI
CPP_SOURCES += src/mac_midi.c src/midi2.c src/midi3.c src/midi_menu.c
CPP_SOURCES += src/alsa_midi.c src/midi2.c src/midi3.c src/midi_menu.c

##############################################################################
#
# Stuff for text-to-speech, if requested
#
##############################################################################

ifeq ($(TTS),ON)
TTS_OPTIONS=-D TTS
TTS_HEADERS= src/tts.h src/MacTTS.h
ifeq ($(UNAME_S), Darwin)
TTS_SOURCES= src/tts.c src/MacTTS.m
TTS_OBJS= src/tts.o src/MacTTS.o
TTS_LIBS= -framework Foundation -framework AVFoundation
endif
ifeq ($(UNAME_S), Linux)
TTS_OPTIONS=-D TTS
TTS_HEADERS= src/tts.h src/MacTTS.h
TTS_SOURCES= src/tts.c
TTS_OBJS= src/tts.o
endif
endif
CPP_DEFINES += -DTTS
CPP_SOURCES += src/tts.c

##############################################################################
#
# Add libraries for Saturn support, if requested
#
##############################################################################

ifeq ($(SATURN),ON)
SATURN_OPTIONS=-D SATURN
SATURN_SOURCES= \
src/saturndrivers.c \
src/saturnregisters.c \
src/saturnserver.c \
src/saturnmain.c \
src/saturn_menu.c
SATURN_HEADERS= \
src/saturndrivers.h \
src/saturnregisters.h \
src/saturnserver.h \
src/saturnmain.h \
src/saturn_menu.h
SATURN_OBJS= \
src/saturndrivers.o \
src/saturnregisters.o \
src/saturnserver.o \
src/saturnmain.o \
src/saturn_menu.o
endif
CPP_DEFINES += -DSATURN
CPP_SOURCES += src/saturndrivers.c  src/saturnregisters.c src/saturnserver.c
CPP_SOURCES += src/saturnmain.c src/saturn_menu.c


##############################################################################
#
# Add libraries for USB OZY support, if requested
#
##############################################################################

ifeq ($(USBOZY),ON)
USBOZY_OPTIONS=-D USBOZY
USBOZY_LIBS=-lusb-1.0
USBOZY_SOURCES= \
src/ozyio.c
USBOZY_HEADERS= \
src/ozyio.h
USBOZY_OBJS= \
src/ozyio.o
endif
CPP_DEFINES += -DUSBOZY
CPP_SOURCES += src/ozyio.c

##############################################################################
#
# Add libraries for SoapySDR support, if requested
#
##############################################################################

ifeq ($(SOAPYSDR),ON)
SOAPYSDR_OPTIONS=-D SOAPYSDR
SOAPYSDRLIBS=-lSoapySDR
SOAPYSDR_SOURCES= \
src/soapy_discovery.c \
src/soapy_protocol.c
SOAPYSDR_HEADERS= \
src/soapy_discovery.h \
src/soapy_protocol.h
SOAPYSDR_OBJS= \
src/soapy_discovery.o \
src/soapy_protocol.o
endif
CPP_DEFINES += -DSOAPYSDR
CPP_SOURCES += src/soapy_discovery.c src/soapy_protocol.c

##############################################################################
#
# Add libraries for GPIO support, if requested
#
# use -DGPIOV1 b default, but
# use -DGPIOV2 if libgpiod with V2 API has been detected,
#
##############################################################################

ifeq ($(GPIO),ON)
GPIO_OPTIONS=-D GPIO
GPIOD_VERSION:=$(shell $(PKG_CONFIG) --modversion libgpiod)
GPIOV2=$(GPIOD_VERSION:2.%=YES)
ifeq ($(GPIOV2),YES)
GPIO_OPTIONS += -D GPIOV2
else
GPIO_OPTIONS += -D GPIOV1
endif
ifeq ($(GPIOD_VERSION),1.2)
GPIO_OPTIONS += -D OLD_GPIOD
endif
GPIO_LIBS=-lgpiod -li2c
endif
CPP_DEFINES += -D GPIO -DGPIOV1 -DGPIOV2

##############################################################################
#
# Activate code for RedPitaya (Stemlab/Hamlab/plain vanilla), if requested
# This code detects the RedPitaya by its WWW interface and starts the SDR
# application.
# If the RedPitaya auto-starts the SDR application upon system start,
# this option is not needed!
#
##############################################################################

ifeq ($(STEMLAB), ON)
STEMLAB_OPTIONS=-D STEMLAB_DISCOVERY
STEMLAB_INCLUDE=`$(PKG_CONFIG) --cflags libcurl`
STEMLAB_LIBS=`$(PKG_CONFIG) --libs libcurl`
STEMLAB_SOURCES=src/stemlab_discovery.c
STEMLAB_HEADERS=src/stemlab_discovery.h
STEMLAB_OBJS=src/stemlab_discovery.o
endif
CPP_DEFINES += -DSTEMLAB_DISCOVERY
CPP_SOURCES += src/stemlab_discovery.c
CPP_INCLUDE += `$(PKG_CONFIG) --cflags libcurl`

##############################################################################
#
# Options for audio module
#  - MacOS: only PORTAUDIO
#  - Linux: either PULSEAUDIO (default) or ALSA (upon request)
#
##############################################################################

ifeq ($(UNAME_S), Darwin)
  AUDIO=PORTAUDIO
endif
ifeq ($(UNAME_S), Linux)
  ifneq ($(AUDIO) , ALSA)
    AUDIO=PULSE
  endif
endif

##############################################################################
#
# Add libraries for using PulseAudio, if requested
#
##############################################################################

ifeq ($(AUDIO), PULSE)
AUDIO_OPTIONS=-DPULSEAUDIO
AUDIO_INCLUDE=
ifeq ($(UNAME_S), Linux)
  AUDIO_LIBS=-lpulse-simple -lpulse -lpulse-mainloop-glib
endif
ifeq ($(UNAME_S), Darwin)
  AUDIO_LIBS=-lpulse-simple -lpulse
endif
AUDIO_SOURCES=src/pulseaudio.c
AUDIO_OBJS=src/pulseaudio.o
endif
CPP_DEFINES += -DPULSEAUDIO
CPP_SOURCES += src/pulseaudio.c

##############################################################################
#
# Add libraries for using ALSA, if requested
#
##############################################################################

ifeq ($(AUDIO), ALSA)
AUDIO_OPTIONS=-DALSA
AUDIO_INCLUDE=
AUDIO_LIBS=-lasound
AUDIO_SOURCES=src/audio.c
AUDIO_OBJS=src/audio.o
endif
CPP_DEFINES += -DALSA
CPP_SOURCES += src/audio.c

##############################################################################
#
# Add libraries for using PortAudio, if requested
#
##############################################################################

ifeq ($(AUDIO), PORTAUDIO)
AUDIO_OPTIONS=-DPORTAUDIO
AUDIO_INCLUDE=`$(PKG_CONFIG) --cflags portaudio-2.0`
AUDIO_LIBS=`$(PKG_CONFIG) --libs portaudio-2.0`
AUDIO_SOURCES=src/portaudio.c
AUDIO_OBJS=src/portaudio.o
endif
CPP_DEFINES += -DPORTAUDIO
CPP_SOURCES += src/portaudio.c
ifeq ($(UNAME_S), Darwin)
# does not exist on RaspPi
CPP_INCLUDE += `$(PKG_CONFIG) --cflags portaudio-2.0`
endif

##############################################################################
#
# End of "libraries for optional features" section
#
##############################################################################

##############################################################################
#
# Includes and Libraries for the graphical user interface (GTK)
#
##############################################################################

GTKINCLUDE=`$(PKG_CONFIG) --cflags gtk+-3.0`
GTKLIBS=`$(PKG_CONFIG) --libs gtk+-3.0`
CPP_INCLUDE += $(GTKINCLUDE)

##############################################################################
#
# Includes/Libraries for OpenSSL (used for TCI and client/server password)
#
##############################################################################

OPENSSL_INCLUDE=`$(PKG_CONFIG) --cflags openssl`
OPENSSL_LIBS=`$(PKG_CONFIG) --libs openssl`
CPP_INCLUDE += `$(PKG_CONFIG) --cflags openssl`

##############################################################################
#
# Specify additional OS-dependent system libraries
#
##############################################################################

ifeq ($(UNAME_S), Linux)
SYSLIBS=-lrt
endif

ifeq ($(UNAME_S), Darwin)
SYSLIBS=-framework IOKit
endif

##############################################################################
#
# All the command-line options to compile the *.c files
#
##############################################################################

OPTIONS=$(MIDI_OPTIONS) $(USBOZY_OPTIONS) \
	$(GPIO_OPTIONS) $(SOAPYSDR_OPTIONS) \
	$(ANDROMEDA_OPTIONS) \
	$(SATURN_OPTIONS) \
	$(STEMLAB_OPTIONS) \
	$(SERVER_OPTIONS) \
	$(TTS_OPTIONS) \
	$(AUDIO_OPTIONS) $(EXTNR_OPTIONS) $(TCI_OPTIONS) \
	-D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' -D GIT_COMMIT='"$(GIT_COMMIT)"'

INCLUDES=$(GTKINCLUDE) $(WDSP_INCLUDE) $(OPENSSL_INCLUDE) $(AUDIO_INCLUDE) $(STEMLAB_INCLUDE)
COMPILE=$(CC) $(CFLAGS) $(OPTIONS) $(INCLUDES)

.c.o:
	$(COMPILE) -c -o $@ $<

.m.o:
	$(COMPILE) -c -o $@ $<

##############################################################################
#
# All the libraries we need to link with (including WDSP, libm, $(SYSLIBS))
#
##############################################################################

LIBS=	$(LDFLAGS) $(AUDIO_LIBS) $(USBOZY_LIBS) $(GTKLIBS) $(GPIO_LIBS) $(SOAPYSDRLIBS) $(STEMLAB_LIBS) \
	$(MIDI_LIBS) $(TTS_LIBS) $(OPENSSL_LIBS) $(WDSP_LIBS) -lm $(SYSLIBS)

##############################################################################
#
# The main target, the pihpsdr program
#
##############################################################################

PROGRAM=pihpsdr

##############################################################################
#
# The core *.c files in alphabetical order
#
##############################################################################

SOURCES= \
src/MacOS.c \
src/about_menu.c \
src/actions.c \
src/action_dialog.c \
src/agc_menu.c \
src/andromeda.c \
src/ant_menu.c \
src/appearance.c \
src/band.c \
src/band_menu.c \
src/bandstack_menu.c \
src/client_server.c \
src/client_thread.c \
src/css.c \
src/cw_menu.c \
src/discovered.c \
src/discovery.c \
src/display_menu.c \
src/diversity_menu.c \
src/encoder_menu.c \
src/equalizer_menu.c \
src/exit_menu.c \
src/ext.c \
src/fft_menu.c \
src/filter.c \
src/filter_menu.c \
src/g2panel.c \
src/g2panel_menu.c \
src/gpio.c \
src/i2c.c \
src/iambic.c \
src/led.c \
src/main.c \
src/message.c \
src/meter.c \
src/meter_menu.c \
src/mode.c \
src/mode_menu.c \
src/new_discovery.c \
src/new_menu.c \
src/new_protocol.c \
src/noise_menu.c \
src/oc_menu.c \
src/old_discovery.c \
src/old_protocol.c \
src/pa_menu.c \
src/piHPSDR_logo.c \
src/property.c \
src/protocols.c \
src/ps_menu.c \
src/radio.c \
src/radio_menu.c \
src/receiver.c \
src/rigctl.c \
src/rigctl_menu.c \
src/rx_menu.c \
src/rx_panadapter.c \
src/screen_menu.c \
src/server_menu.c \
src/server_thread.c \
src/sintab.c \
src/sliders.c \
src/sliders_menu.h \
src/startup.c \
src/store.c \
src/store_menu.c \
src/switch_menu.c \
src/tci.c \
src/test_menu.c \
src/toolbar.c \
src/toolbar_menu.c \
src/transmitter.c \
src/tx_menu.c \
src/tx_panadapter.c \
src/version.c \
src/vfo.c \
src/vfo_menu.c \
src/vox.c \
src/vox_menu.c \
src/waterfall.c \
src/xvtr_menu.c

##############################################################################
#
# The core *.h (header) files in alphabetical order
#
##############################################################################

HEADERS= \
src/MacOS.h \
src/about_menu.h \
src/actions.h \
src/action_dialog.h \
src/adc.h \
src/agc.h \
src/agc_menu.h \
src/alex.h \
src/andromeda.h \
src/ant_menu.h \
src/appearance.h \
src/band.h \
src/band_menu.h \
src/bandstack_menu.h \
src/bandstack.h \
src/channel.h \
src/client_server.h \
src/css.h \
src/cw_menu.h \
src/discovered.h \
src/discovery.h \
src/display_menu.h \
src/diversity_menu.h \
src/encoder_menu.h \
src/equalizer_menu.h \
src/exit_menu.h \
src/ext.h \
src/fft_menu.h \
src/filter.h \
src/filter_menu.h \
src/g2panel.h \
src/g2panel_menu.h \
src/gpio.h \
src/iambic.h \
src/i2c.h \
src/led.h \
src/main.h \
src/message.h \
src/meter.h \
src/meter_menu.h \
src/mode.h \
src/mode_menu.h \
src/new_discovery.h \
src/new_menu.h \
src/new_protocol.h \
src/noise_menu.h \
src/oc_menu.h \
src/old_discovery.h \
src/old_protocol.h \
src/pa_menu.h \
src/piHPSDR_logo.h \
src/property.h \
src/protocols.h \
src/ps_menu.h \
src/radio.h \
src/radio_menu.h \
src/receiver.h \
src/rigctl.h \
src/rigctl_menu.h \
src/rx_menu.h \
src/rx_panadapter.h \
src/screen_menu.h \
src/server_menu.h \
src/sintab.h \
src/sliders.h \
src/sliders_menu.h \
src/startup.h \
src/store.h \
src/store_menu.h \
src/switch_menu.h \
src/tci.h \
src/test_menu.h \
src/toolbar.h \
src/toolbar_menu.h \
src/transmitter.h \
src/tx_menu.h \
src/tx_panadapter.h \
src/version.h \
src/vfo.h \
src/vfo_menu.h \
src/vox.h \
src/vox_menu.h \
src/waterfall.h \
src/xvtr_menu.h

##############################################################################
#
# The core *.o (object) files in alphabetical order
#
##############################################################################

OBJS= \
src/MacOS.o \
src/about_menu.o \
src/actions.o \
src/action_dialog.o \
src/agc_menu.o \
src/andromeda.o \
src/ant_menu.o \
src/appearance.o \
src/band.o \
src/band_menu.o \
src/bandstack_menu.o \
src/client_server.o \
src/client_thread.o \
src/css.o \
src/cw_menu.o \
src/discovered.o \
src/discovery.o \
src/display_menu.o \
src/diversity_menu.o \
src/encoder_menu.o \
src/equalizer_menu.o \
src/exit_menu.o \
src/ext.o \
src/fft_menu.o \
src/filter.o \
src/filter_menu.o \
src/g2panel.o \
src/g2panel_menu.o \
src/gpio.o \
src/iambic.o \
src/i2c.o \
src/led.o \
src/main.o \
src/message.o \
src/meter.o \
src/meter_menu.o \
src/mode.o \
src/mode_menu.o \
src/new_discovery.o \
src/new_menu.o \
src/new_protocol.o \
src/noise_menu.o \
src/oc_menu.o \
src/old_discovery.o \
src/old_protocol.o \
src/pa_menu.o \
src/piHPSDR_logo.o \
src/property.o \
src/protocols.o \
src/ps_menu.o \
src/radio.o \
src/radio_menu.o \
src/receiver.o \
src/rigctl.o \
src/rigctl_menu.o \
src/rx_menu.o \
src/rx_panadapter.o \
src/screen_menu.o \
src/server_menu.o \
src/server_thread.o \
src/sintab.o \
src/sliders.o \
src/sliders_menu.o \
src/startup.o \
src/store.o \
src/store_menu.o \
src/switch_menu.o \
src/tci.o \
src/test_menu.o \
src/toolbar.o \
src/toolbar_menu.o \
src/transmitter.o \
src/tx_menu.o \
src/tx_panadapter.o \
src/version.o \
src/vfo.o \
src/vfo_menu.o \
src/vox.o \
src/vox_menu.o \
src/xvtr_menu.o \
src/waterfall.o

##############################################################################
#
# How to link the program
#
##############################################################################

$(PROGRAM):  $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) $(TTS_OBJS)
	$(COMPILE) -c -o src/version.o src/version.c
ifneq (z$(WDSP_INCLUDE), z)
	@+make -C wdsp
endif
	$(LINK) -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) $(TTS_OBJS)\
		$(LIBS)

##############################################################################
#
# "make check" invokes the cppcheck program to do a source-code checking.
#
# The "-pthread" compiler option is not valid for cppcheck and must be filtered out.
# Furthermore, we can add additional options to cppcheck in the variable CPP_OPTIONS
#
# Normally cppcheck complains about variables that could be declared "const".
# Suppress this warning for callback functions because adding "const" would need
# an API change in many cases.
#
# On MacOS, cppcheck usually cannot find the system include files so we suppress any
# warnings therefrom, as well as warnings for functions defined in some
# library but never called.
# Furthermore, we can use --check-level=exhaustive on MacOS
# since there we have new newest version (>2.11), while on RaspPi we still have
# older versions.
#
##############################################################################

CPP_INCLUDE:=$(shell echo $(CPP_INCLUDE) | sed -e "s/ -pthread/ /" )

CPP_OPTIONS= --inline-suppr --enable=all --suppress=unmatchedSuppression

ifeq ($(UNAME_S), Darwin)
CPP_OPTIONS += -D__APPLE__
CPP_OPTIONS += --check-level=exhaustive
CPP_OPTIONS += --suppress=missingIncludeSystem
CPP_OPTIONS += --suppress=unusedFunction
else
CPP_OPTIONS += -D__linux__
CPP_OPTIONS += --suppress=missingIncludeSystem
CPP_OPTIONS += --suppress=unusedFunction
endif

.PHONY:	cppcheck
cppcheck:
	cppcheck $(CPP_OPTIONS) $(CPP_INCLUDE) $(CPP_DEFINES) $(SOURCES) $(CPP_SOURCES)

.PHONY:	clean
clean:
	rm -f src/*.o
	rm -f $(PROGRAM) hpsdrsim bootloader
	rm -rf $(PROGRAM).app
	@make -C release/LatexManual clean
	@make -C wdsp clean

#############################################################################
#
# hpsdrsim is a cool program that emulates an SDR board with UDP and TCP
# facilities. It even feeds back the TX signal and distorts it, so that
# you can test PureSignal.
# This feature only works if the sample rate is 48000
#
#############################################################################

src/hpsdrsim.o:     src/hpsdrsim.c  src/hpsdrsim.h
	$(CC) -c $(CFLAGS) -o src/hpsdrsim.o src/hpsdrsim.c
	
src/newhpsdrsim.o:	src/newhpsdrsim.c src/hpsdrsim.h
	$(CC) -c $(CFLAGS) -o src/newhpsdrsim.o src/newhpsdrsim.c

hpsdrsim:       src/hpsdrsim.o src/newhpsdrsim.o
	$(LINK) -o hpsdrsim src/hpsdrsim.o src/newhpsdrsim.o -lm


#############################################################################
#
# bootloader is a small command-line program that allows to
# set the radio's IP address and upload firmware through the
# ancient protocol. This program can only be run as root since
# this protocol requires "sniffing" at the Ethernet adapter
# (this "sniffing" is done via the pcap library)
#
#############################################################################

bootloader:	src/bootloader.c
	$(CC) -o bootloader src/bootloader.c -lpcap

#############################################################################
#
# Re-create the manual PDF from the manual LaTeX sources. This creates
# the PDF version of the manual in release/LaTexManual and DOES NOT over-
# write the manual in release.
# The PDF file in "release" is meant to be updated only once a year or so,
# because including frequently changing binaries in a git repository tends
# to blow up this repository. Instead, binaries should be re-created from
# source code files.
#
#############################################################################

#############################################################################
#
# Create a file named DEPEND containing dependencies, to be added to
# the Makefile. This is done here because we need lots of #defines
# to make it right.
# Since src/MacTTS.c is Objective-C, create the final line manually
#
#############################################################################

.PHONY: DEPEND
DEPEND:
	rm -f DEPEND
	touch DEPEND
	export LC_ALL=C && makedepend -DMIDI -DSATURN -DUSBOZY -DSOAPYSDR -DEXTNR -DGPIO \
		-DSTEMLAB_DISCOVERY -DPULSEAUDIO \
		-DPORTAUDIO -DALSA -DTTS -D__APPLE__ -D__linux__ \
		-f DEPEND -I./src src/*.c src/*.h
	echo "src/MacTTS.o: src/message.h" >> DEPEND
#############################################################################
#
# This is for MacOS "app" creation ONLY
#
#       The piHPSDR working directory is
#	$HOME -> Application Support -> piHPSDR
#
#       That is the directory where the WDSP wisdom file (created upon first
#       start of piHPSDR) but also the radio settings and the midi.props file
#       are stored.
#
#       No libraries are included in the app bundle, so it will only run
#       on the computer where it was created, and on other computers which
#       have all librariesand possibly the SoapySDR support
#       modules installed.
#############################################################################

.PHONY: app
app:	$(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  $(SOAPYSDR_OBJS) $(TCI_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) $(TTS_OBJS)
ifneq (z$(WDSP_INCLUDE), z)
	@+make -C wdsp
endif
	$(LINK) -headerpad_max_install_names -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  \
		$(SOAPYSDR_OBJS) $(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) $(TTS_OBJS) \
		$(TCI_OBJS) $(LIBS) $(LDFLAGS)
	@rm -rf pihpsdr.app
	@mkdir -p pihpsdr.app/Contents/MacOS
	@mkdir -p pihpsdr.app/Contents/Frameworks
	@mkdir -p pihpsdr.app/Contents/Resources
	@cp pihpsdr pihpsdr.app/Contents/MacOS/pihpsdr
	@cp MacOS/PkgInfo pihpsdr.app/Contents
	@cp MacOS/Info.plist pihpsdr.app/Contents
	@cp MacOS/piHPSDR_logo.icns pihpsdr.app/Contents/Resources/piHPSDR_logo.icns

#############################################################################
#
# What follows is automatically generated by the "makedepend" program
# implemented here with "make DEPEND". This should be re-done each time
# a header file is added, or added to a C source code file.
#
#############################################################################

# DO NOT DELETE

src/MacOS.o: src/message.h
src/about_menu.o: src/discovered.h src/new_menu.h src/radio.h src/adc.h
src/about_menu.o: src/receiver.h src/transmitter.h src/version.h
src/action_dialog.o: src/actions.h src/main.h
src/actions.o: src/actions.h src/agc.h src/band.h src/bandstack.h
src/actions.o: src/client_server.h src/mode.h src/receiver.h
src/actions.o: src/transmitter.h src/discovery.h src/ext.h src/filter.h
src/actions.o: src/gpio.h src/iambic.h src/main.h src/message.h
src/actions.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/ps_menu.h
src/actions.o: src/radio.h src/adc.h src/discovered.h src/sliders.h
src/actions.o: src/store.h src/toolbar.h src/vfo.h
src/agc_menu.o: src/agc.h src/band.h src/bandstack.h src/ext.h
src/agc_menu.o: src/client_server.h src/mode.h src/receiver.h
src/agc_menu.o: src/transmitter.h src/new_menu.h src/radio.h src/adc.h
src/agc_menu.o: src/discovered.h src/vfo.h
src/andromeda.o: src/actions.h src/band.h src/bandstack.h src/ext.h
src/andromeda.o: src/client_server.h src/mode.h src/receiver.h
src/andromeda.o: src/transmitter.h src/new_menu.h src/radio.h src/adc.h
src/andromeda.o: src/discovered.h src/toolbar.h src/vfo.h
src/ant_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/ant_menu.o: src/receiver.h src/transmitter.h src/message.h src/new_menu.h
src/ant_menu.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/ant_menu.o: src/discovered.h src/soapy_protocol.h
src/appearance.o: src/appearance.h src/css.h
src/audio.o: src/audio.h src/receiver.h src/client_server.h src/mode.h
src/audio.o: src/transmitter.h src/message.h src/radio.h src/adc.h
src/audio.o: src/discovered.h src/vfo.h
src/band.o: src/band.h src/bandstack.h src/filter.h src/mode.h src/message.h
src/band.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/band.o: src/receiver.h src/transmitter.h src/vfo.h
src/band_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/band_menu.o: src/receiver.h src/transmitter.h src/filter.h src/new_menu.h
src/band_menu.o: src/radio.h src/adc.h src/discovered.h src/vfo.h
src/bandstack_menu.o: src/band.h src/bandstack.h src/filter.h src/mode.h
src/bandstack_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/bandstack_menu.o: src/receiver.h src/transmitter.h src/vfo.h
src/client_server.o: src/band.h src/bandstack.h src/client_server.h
src/client_server.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/client_server.o: src/message.h src/radio.h src/adc.h src/discovered.h
src/client_server.o: src/store.h src/vfo.h
src/client_thread.o: src/audio.h src/receiver.h src/band.h src/bandstack.h
src/client_thread.o: src/client_server.h src/mode.h src/transmitter.h
src/client_thread.o: src/ext.h src/filter.h src/message.h src/radio.h
src/client_thread.o: src/adc.h src/discovered.h src/sliders.h src/actions.h
src/client_thread.o: src/store.h src/vfo.h src/vox.h
src/css.o: src/css.h src/message.h
src/cw_menu.o: src/client_server.h src/mode.h src/receiver.h
src/cw_menu.o: src/transmitter.h src/ext.h src/iambic.h src/new_menu.h
src/cw_menu.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/cw_menu.o: src/discovered.h
src/discovered.o: src/discovered.h
src/discovery.o: src/actions.h src/client_server.h src/mode.h src/receiver.h
src/discovery.o: src/transmitter.h src/discovered.h src/ext.h
src/discovery.o: src/gpio.h src/main.h src/message.h src/new_discovery.h
src/discovery.o: src/old_discovery.h src/ozyio.h src/property.h
src/discovery.o: src/protocols.h src/radio.h src/adc.h src/soapy_discovery.h
src/discovery.o: src/stemlab_discovery.h src/tts.h src/saturnmain.h
src/discovery.o: src/saturnregisters.h
src/display_menu.o: src/client_server.h src/mode.h src/receiver.h
src/display_menu.o: src/transmitter.h src/main.h src/new_menu.h src/radio.h
src/display_menu.o: src/adc.h src/discovered.h
src/diversity_menu.o: src/client_server.h src/mode.h src/receiver.h
src/diversity_menu.o: src/transmitter.h src/new_menu.h src/radio.h src/adc.h
src/diversity_menu.o: src/discovered.h
src/encoder_menu.o: src/action_dialog.h src/actions.h src/agc.h src/band.h
src/encoder_menu.o: src/bandstack.h src/channel.h src/gpio.h src/i2c.h
src/encoder_menu.o: src/main.h src/new_menu.h src/radio.h src/adc.h
src/encoder_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/encoder_menu.o: src/vfo.h src/mode.h
src/equalizer_menu.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/equalizer_menu.o: src/transmitter.h src/main.h src/message.h
src/equalizer_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/equalizer_menu.o: src/vfo.h
src/exit_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/exit_menu.o: src/receiver.h src/transmitter.h
src/ext.o: src/main.h src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/ext.o: src/receiver.h src/transmitter.h src/vfo.h src/mode.h
src/fft_menu.o: src/fft_menu.h src/message.h src/new_menu.h src/radio.h
src/fft_menu.o: src/adc.h src/discovered.h src/receiver.h src/transmitter.h
src/filter.o: src/actions.h src/ext.h src/client_server.h src/mode.h
src/filter.o: src/receiver.h src/transmitter.h src/filter.h src/message.h
src/filter.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/filter.o: src/sliders.h src/vfo.h
src/filter_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/filter_menu.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/filter_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/filter_menu.o: src/discovered.h src/vfo.h
src/g2panel.o: src/actions.h src/g2panel_menu.h src/property.h
src/g2panel_menu.o: src/action_dialog.h src/actions.h src/g2panel.h
src/g2panel_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/g2panel_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/gpio.o: src/actions.h src/band.h src/bandstack.h src/channel.h
src/gpio.o: src/discovered.h src/ext.h src/client_server.h src/mode.h
src/gpio.o: src/receiver.h src/transmitter.h src/filter.h src/gpio.h
src/gpio.o: src/i2c.h src/iambic.h src/main.h src/message.h
src/gpio.o: src/new_protocol.h src/MacOS.h src/property.h src/radio.h
src/gpio.o: src/adc.h src/sliders.h src/toolbar.h src/vfo.h
src/hpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/i2c.o: src/actions.h src/band.h src/bandstack.h src/ext.h
src/i2c.o: src/client_server.h src/mode.h src/receiver.h src/transmitter.h
src/i2c.o: src/gpio.h src/i2c.h src/message.h src/radio.h src/adc.h
src/i2c.o: src/discovered.h src/toolbar.h src/vfo.h
src/iambic.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/iambic.o: src/transmitter.h src/gpio.h src/iambic.h src/main.h
src/iambic.o: src/message.h src/new_protocol.h src/MacOS.h src/radio.h
src/iambic.o: src/adc.h src/discovered.h src/vfo.h
src/led.o: src/message.h
src/mac_midi.o: src/message.h src/midi.h src/actions.h src/midi_menu.h
src/main.o: src/actions.h src/appearance.h src/css.h src/audio.h
src/main.o: src/receiver.h src/band.h src/bandstack.h
src/main.o: src/discovery.h src/discovered.h src/ext.h src/client_server.h
src/main.o: src/mode.h src/transmitter.h src/gpio.h src/main.h src/message.h
src/main.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/old_protocol.h
src/main.o: src/radio.h src/adc.h src/saturnmain.h src/saturnregisters.h
src/main.o: src/soapy_protocol.h src/startup.h src/test_menu.h src/version.h
src/main.o: src/vfo.h
src/meter.o: src/appearance.h src/css.h src/band.h src/bandstack.h
src/meter.o: src/meter.h src/receiver.h src/message.h src/mode.h
src/meter.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/meter.o: src/transmitter.h src/version.h src/vfo.h src/vox.h
src/meter_menu.o: src/client_server.h src/mode.h src/receiver.h
src/meter_menu.o: src/transmitter.h src/meter.h src/new_menu.h src/radio.h
src/meter_menu.o: src/adc.h src/discovered.h
src/midi2.o: src/MacOS.h src/main.h src/message.h src/midi.h src/actions.h
src/midi2.o: src/property.h
src/midi3.o: src/actions.h src/message.h src/midi.h
src/midi_menu.o: src/action_dialog.h src/actions.h src/main.h src/message.h
src/midi_menu.o: src/midi.h src/new_menu.h src/property.h src/radio.h
src/midi_menu.o: src/adc.h src/discovered.h src/receiver.h src/transmitter.h
src/mode_menu.o: src/band.h src/bandstack.h src/filter.h src/mode.h
src/mode_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/mode_menu.o: src/receiver.h src/transmitter.h src/vfo.h
src/new_discovery.o: src/discovered.h src/discovery.h src/message.h
src/new_menu.o: src/about_menu.h src/actions.h src/agc_menu.h src/ant_menu.h
src/new_menu.o: src/audio.h src/receiver.h src/band_menu.h
src/new_menu.o: src/bandstack_menu.h src/client_server.h src/mode.h
src/new_menu.o: src/transmitter.h src/cw_menu.h src/display_menu.h
src/new_menu.o: src/diversity_menu.h src/encoder_menu.h src/equalizer_menu.h
src/new_menu.o: src/exit_menu.h src/fft_menu.h src/filter_menu.h
src/new_menu.o: src/g2panel_menu.h src/gpio.h src/main.h src/meter_menu.h
src/new_menu.o: src/midi_menu.h src/midi.h src/mode_menu.h src/new_menu.h
src/new_menu.o: src/new_protocol.h src/MacOS.h src/noise_menu.h src/oc_menu.h
src/new_menu.o: src/old_protocol.h src/pa_menu.h src/ps_menu.h
src/new_menu.o: src/radio_menu.h src/radio.h src/adc.h src/discovered.h
src/new_menu.o: src/rigctl_menu.h src/rx_menu.h src/saturn_menu.h
src/new_menu.o: src/server_menu.h src/screen_menu.h src/sliders_menu.h
src/new_menu.o: src/store_menu.h src/switch_menu.h src/toolbar_menu.h
src/new_menu.o: src/tx_menu.h src/xvtr_menu.h src/vfo_menu.h src/vox_menu.h
src/new_protocol.o: src/alex.h src/audio.h src/receiver.h src/band.h
src/new_protocol.o: src/bandstack.h src/discovered.h src/ext.h
src/new_protocol.o: src/client_server.h src/mode.h src/transmitter.h
src/new_protocol.o: src/filter.h src/iambic.h src/main.h src/message.h
src/new_protocol.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/new_protocol.o: src/rigctl.h src/saturnmain.h src/saturnregisters.h
src/new_protocol.o: src/toolbar.h src/actions.h src/vfo.h src/vox.h
src/newhpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/noise_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/noise_menu.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/noise_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/noise_menu.o: src/vfo.h
src/oc_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/oc_menu.o: src/receiver.h src/transmitter.h src/filter.h src/main.h
src/oc_menu.o: src/message.h src/new_menu.h src/new_protocol.h src/MacOS.h
src/oc_menu.o: src/radio.h src/adc.h src/discovered.h
src/old_discovery.o: src/discovered.h src/discovery.h src/message.h
src/old_discovery.o: src/old_discovery.h src/stemlab_discovery.h
src/old_protocol.o: src/MacOS.h src/audio.h src/receiver.h src/band.h
src/old_protocol.o: src/bandstack.h src/discovered.h src/ext.h
src/old_protocol.o: src/client_server.h src/mode.h src/transmitter.h
src/old_protocol.o: src/filter.h src/iambic.h src/main.h src/message.h
src/old_protocol.o: src/old_protocol.h src/radio.h src/adc.h src/vfo.h
src/old_protocol.o: src/ozyio.h
src/ozyio.o: src/message.h src/ozyio.h
src/pa_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/pa_menu.o: src/receiver.h src/transmitter.h src/message.h src/new_menu.h
src/pa_menu.o: src/radio.h src/adc.h src/discovered.h src/vfo.h
src/piHPSDR_logo.o: src/message.h
src/portaudio.o: src/audio.h src/receiver.h src/client_server.h src/mode.h
src/portaudio.o: src/transmitter.h src/message.h src/radio.h src/adc.h
src/portaudio.o: src/discovered.h src/vfo.h
src/property.o: src/main.h src/message.h src/property.h src/radio.h src/adc.h
src/property.o: src/discovered.h src/receiver.h src/transmitter.h
src/protocols.o: src/property.h src/protocols.h src/radio.h src/adc.h
src/protocols.o: src/discovered.h src/receiver.h src/transmitter.h
src/ps_menu.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/ps_menu.o: src/transmitter.h src/message.h src/new_menu.h
src/ps_menu.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/ps_menu.o: src/discovered.h src/toolbar.h src/actions.h src/vfo.h
src/pulseaudio.o: src/audio.h src/receiver.h src/client_server.h src/mode.h
src/pulseaudio.o: src/transmitter.h src/message.h src/radio.h src/adc.h
src/pulseaudio.o: src/discovered.h src/vfo.h
src/radio.o: src/actions.h src/adc.h src/agc.h src/appearance.h src/css.h
src/radio.o: src/audio.h src/receiver.h src/band.h src/bandstack.h
src/radio.o: src/channel.h src/client_server.h src/mode.h src/transmitter.h
src/radio.o: src/discovered.h src/ext.h src/filter.h src/g2panel.h src/gpio.h
src/radio.o: src/iambic.h src/main.h src/meter.h src/message.h src/midi.h
src/radio.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/old_protocol.h
src/radio.o: src/property.h src/radio.h src/rigctl.h src/rx_panadapter.h
src/radio.o: src/sliders.h src/tci.h src/test_menu.h src/toolbar.h src/tts.h
src/radio.o: src/tx_panadapter.h src/saturnmain.h src/saturnregisters.h
src/radio.o: src/saturnserver.h src/soapy_protocol.h src/store.h src/vfo.h
src/radio.o: src/vox.h src/waterfall.h
src/radio_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/radio_menu.o: src/receiver.h src/transmitter.h src/discovered.h src/ext.h
src/radio_menu.o: src/main.h src/new_menu.h src/new_protocol.h src/MacOS.h
src/radio_menu.o: src/radio.h src/adc.h src/sliders.h src/actions.h
src/radio_menu.o: src/soapy_protocol.h src/vfo.h
src/receiver.o: src/agc.h src/audio.h src/receiver.h src/band.h
src/receiver.o: src/bandstack.h src/channel.h src/client_server.h src/mode.h
src/receiver.o: src/transmitter.h src/discovered.h src/ext.h src/filter.h
src/receiver.o: src/main.h src/meter.h src/message.h src/new_menu.h
src/receiver.o: src/new_protocol.h src/MacOS.h src/old_protocol.h
src/receiver.o: src/property.h src/radio.h src/adc.h src/rx_panadapter.h
src/receiver.o: src/sliders.h src/actions.h src/soapy_protocol.h src/vfo.h
src/receiver.o: src/waterfall.h
src/rigctl.o: src/actions.h src/agc.h src/andromeda.h src/band.h
src/rigctl.o: src/bandstack.h src/channel.h src/ext.h src/client_server.h
src/rigctl.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/rigctl.o: src/g2panel.h src/g2panel_menu.h src/iambic.h src/main.h
src/rigctl.o: src/message.h src/new_protocol.h src/MacOS.h src/old_protocol.h
src/rigctl.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/rigctl.o: src/rigctl.h src/sliders.h src/store.h src/toolbar.h src/vfo.h
src/rigctl_menu.o: src/band.h src/bandstack.h src/message.h src/new_menu.h
src/rigctl_menu.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/rigctl_menu.o: src/transmitter.h src/rigctl.h src/tci.h src/vfo.h
src/rigctl_menu.o: src/mode.h
src/rx_menu.o: src/audio.h src/receiver.h src/band.h src/bandstack.h
src/rx_menu.o: src/client_server.h src/mode.h src/transmitter.h
src/rx_menu.o: src/discovered.h src/filter.h src/message.h src/new_menu.h
src/rx_menu.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/rx_menu.o: src/rx_menu.h src/sliders.h src/actions.h
src/rx_panadapter.o: src/actions.h src/agc.h src/appearance.h src/css.h
src/rx_panadapter.o: src/band.h src/bandstack.h src/client_server.h
src/rx_panadapter.o: src/mode.h src/receiver.h src/transmitter.h
src/rx_panadapter.o: src/discovered.h src/gpio.h src/message.h src/radio.h
src/rx_panadapter.o: src/adc.h src/ozyio.h src/rx_panadapter.h src/vfo.h
src/saturn_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/saturn_menu.o: src/receiver.h src/transmitter.h src/saturn_menu.h
src/saturn_menu.o: src/saturnserver.h
src/saturndrivers.o: src/message.h src/saturndrivers.h src/saturnregisters.h
src/saturnmain.o: src/discovered.h src/message.h src/new_protocol.h
src/saturnmain.o: src/MacOS.h src/receiver.h src/saturndrivers.h
src/saturnmain.o: src/saturnregisters.h src/saturnmain.h src/saturnserver.h
src/saturnregisters.o: src/saturndrivers.h src/saturnregisters.h
src/saturnregisters.o: src/message.h
src/saturnserver.o: src/message.h src/saturndrivers.h src/saturnregisters.h
src/saturnserver.o: src/saturnmain.h src/saturnserver.h
src/screen_menu.o: src/appearance.h src/css.h src/ext.h src/client_server.h
src/screen_menu.o: src/mode.h src/receiver.h src/transmitter.h src/main.h
src/screen_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/screen_menu.o: src/discovered.h
src/server_menu.o: src/client_server.h src/mode.h src/receiver.h
src/server_menu.o: src/transmitter.h src/message.h src/new_menu.h src/radio.h
src/server_menu.o: src/adc.h src/discovered.h src/server_menu.h
src/server_thread.o: src/actions.h src/band.h src/bandstack.h
src/server_thread.o: src/client_server.h src/mode.h src/receiver.h
src/server_thread.o: src/transmitter.h src/ext.h src/filter.h src/iambic.h
src/server_thread.o: src/main.h src/message.h src/new_protocol.h src/MacOS.h
src/server_thread.o: src/radio.h src/adc.h src/discovered.h
src/server_thread.o: src/soapy_protocol.h src/store.h src/vfo.h
src/sliders.o: src/actions.h src/ext.h src/client_server.h src/mode.h
src/sliders.o: src/receiver.h src/transmitter.h src/main.h src/message.h
src/sliders.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/sliders.o: src/sliders.h
src/sliders_menu.o: src/actions.h src/new_menu.h src/radio.h src/adc.h
src/sliders_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/sliders_menu.o: src/sliders.h
src/soapy_discovery.o: src/discovered.h src/message.h src/soapy_discovery.h
src/soapy_protocol.o: src/audio.h src/receiver.h src/band.h src/bandstack.h
src/soapy_protocol.o: src/channel.h src/discovered.h src/ext.h
src/soapy_protocol.o: src/client_server.h src/mode.h src/transmitter.h
src/soapy_protocol.o: src/filter.h src/main.h src/message.h src/radio.h
src/soapy_protocol.o: src/adc.h src/soapy_protocol.h src/vfo.h
src/startup.o: src/message.h
src/stemlab_discovery.o: src/discovered.h src/discovery.h src/message.h
src/stemlab_discovery.o: src/radio.h src/adc.h src/receiver.h
src/stemlab_discovery.o: src/transmitter.h
src/store.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/store.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/store.o: src/message.h src/property.h src/radio.h src/adc.h
src/store.o: src/discovered.h src/store.h src/store_menu.h src/vfo.h
src/store_menu.o: src/filter.h src/mode.h src/message.h src/new_menu.h
src/store_menu.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/store_menu.o: src/transmitter.h src/store_menu.h src/store.h
src/switch_menu.o: src/action_dialog.h src/actions.h src/agc.h src/band.h
src/switch_menu.o: src/bandstack.h src/channel.h src/gpio.h src/i2c.h
src/switch_menu.o: src/main.h src/new_menu.h src/radio.h src/adc.h
src/switch_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/switch_menu.o: src/toolbar.h src/vfo.h src/mode.h
src/tci.o: src/message.h src/radio.h src/adc.h src/discovered.h
src/tci.o: src/receiver.h src/transmitter.h src/rigctl.h src/vfo.h src/mode.h
src/test_menu.o: src/actions.h src/message.h
src/toolbar.o: src/actions.h src/gpio.h src/message.h src/property.h
src/toolbar.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/toolbar.o: src/transmitter.h src/toolbar.h
src/toolbar_menu.o: src/action_dialog.h src/actions.h src/gpio.h
src/toolbar_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/toolbar_menu.o: src/receiver.h src/transmitter.h src/toolbar.h
src/transmitter.o: src/audio.h src/receiver.h src/band.h src/bandstack.h
src/transmitter.o: src/channel.h src/ext.h src/client_server.h src/mode.h
src/transmitter.o: src/transmitter.h src/filter.h src/main.h src/meter.h
src/transmitter.o: src/message.h src/new_protocol.h src/MacOS.h
src/transmitter.o: src/old_protocol.h src/ozyio.h src/property.h
src/transmitter.o: src/ps_menu.h src/radio.h src/adc.h src/discovered.h
src/transmitter.o: src/sintab.h src/sliders.h src/actions.h
src/transmitter.o: src/soapy_protocol.h src/toolbar.h src/tx_panadapter.h
src/transmitter.o: src/vfo.h src/vox.h src/waterfall.h
src/tts.o: src/message.h src/radio.h src/adc.h src/discovered.h
src/tts.o: src/receiver.h src/transmitter.h src/vfo.h src/mode.h src/MacTTS.h
src/tx_menu.o: src/audio.h src/receiver.h src/ext.h src/client_server.h
src/tx_menu.o: src/mode.h src/transmitter.h src/filter.h src/message.h
src/tx_menu.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/radio.h
src/tx_menu.o: src/adc.h src/discovered.h src/sliders.h src/actions.h
src/tx_menu.o: src/vfo.h
src/tx_panadapter.o: src/actions.h src/agc.h src/appearance.h src/css.h
src/tx_panadapter.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/tx_panadapter.o: src/mode.h src/receiver.h src/transmitter.h
src/tx_panadapter.o: src/discovered.h src/gpio.h src/message.h src/radio.h
src/tx_panadapter.o: src/adc.h src/rx_panadapter.h src/tx_panadapter.h
src/tx_panadapter.o: src/vfo.h
src/vfo.o: src/appearance.h src/css.h src/discovered.h src/main.h src/agc.h
src/vfo.o: src/mode.h src/filter.h src/bandstack.h src/band.h src/property.h
src/vfo.o: src/radio.h src/adc.h src/receiver.h src/transmitter.h
src/vfo.o: src/new_protocol.h src/MacOS.h src/vfo.h src/channel.h
src/vfo.o: src/toolbar.h src/actions.h src/rigctl.h src/client_server.h
src/vfo.o: src/ext.h src/message.h src/sliders.h
src/vfo_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/vfo_menu.o: src/mode.h src/receiver.h src/transmitter.h src/filter.h
src/vfo_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/vfo_menu.o: src/radio_menu.h src/vfo.h
src/vox.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/vox.o: src/transmitter.h src/vox.h src/vfo.h src/mode.h src/ext.h
src/vox.o: src/client_server.h
src/vox_menu.o: src/appearance.h src/css.h src/ext.h src/client_server.h
src/vox_menu.o: src/mode.h src/receiver.h src/transmitter.h src/led.h
src/vox_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/vox_menu.o: src/discovered.h src/vfo.h src/vox.h
src/waterfall.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/waterfall.o: src/transmitter.h src/vfo.h src/mode.h src/band.h
src/waterfall.o: src/bandstack.h src/message.h src/waterfall.h
src/xvtr_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/xvtr_menu.o: src/receiver.h src/transmitter.h src/filter.h src/message.h
src/xvtr_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/xvtr_menu.o: src/vfo.h
src/action_dialog.o: src/actions.h
src/appearance.o: src/css.h
src/audio.o: src/receiver.h
src/band.o: src/bandstack.h
src/client_server.o: src/mode.h src/receiver.h src/transmitter.h
src/ext.o: src/client_server.h src/mode.h src/receiver.h src/transmitter.h
src/filter.o: src/mode.h
src/meter.o: src/receiver.h
src/midi.o: src/actions.h
src/midi_menu.o: src/midi.h src/actions.h
src/new_protocol.o: src/MacOS.h src/receiver.h
src/radio.o: src/adc.h src/discovered.h src/receiver.h src/transmitter.h
src/rx_panadapter.o: src/receiver.h
src/saturndrivers.o: src/saturnregisters.h
src/saturnmain.o: src/saturnregisters.h
src/sliders.o: src/actions.h src/receiver.h src/transmitter.h
src/soapy_protocol.o: src/receiver.h src/transmitter.h
src/toolbar.o: src/actions.h
src/tx_panadapter.o: src/transmitter.h
src/vfo.o: src/receiver.h src/mode.h
src/vox.o: src/transmitter.h
src/waterfall.o: src/receiver.h
src/MacTTS.o: src/message.h
