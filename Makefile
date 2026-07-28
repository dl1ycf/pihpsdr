#######################################################################################
# IMPORTANT NOTE:
# This file is no longer meant to be changed by the end user.
# To use non-default compile-time options please create a file
# make.config.pihpsdr (see below)
#
#######################################################################################

#######################################################################################
#
# Get info about the operating system
#
#######################################################################################

UNAME_S := $(shell uname -s)
UNAME_R := $(shell uname -r)

MACOS:=NO
LINUX:=NO

ifeq ($(findstring Darwin, $(UNAME_S)), Darwin)
 MACOS:=YES
endif

ifeq ($(findstring Linux, $(UNAME_S)), Linux)
 LINUX:=YES
 ifeq ($(findstring 6.12., $(UNAME_R)), 6.12.)
 endif
endif

#######################################################################################
#
# Default Compile-time options. Default AUDIO on LINUX is PULSE, on MacOS PORTAUDIO.
#
#######################################################################################

GPIO=ON
MIDI=ON
USBOZY=OFF
SOAPYSDR=OFF
PORTFORWARD=ON
TCI=ON
ifeq ($(MACOS), YES)
AUDIO=PORTAUDIO
endif
ifeq ($(LINUX), YES)
AUDIO=PULSE
endif

#######################################################################################
#
# Explanation of compile time options
#
# GPIO         | If ON, compile with GPIO support (RaspPi only, needs libgpiod)
# MIDI         | If ON, compile with MIDI support (needs support from operating system)
# USBOZY       | If ON, piHPSDR can talk to legacy USB OZY radios (needs  libusb-1.0)
# SOAPYSDR     | If ON, piHPSDR can talk to radios via SoapySDR library
# TCI          | If ON, activate TCI server (needs libwebsockets)
# AUDIO        | Select audio module (ALSA, PULSE, PORTAUDO)
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
CFLAGS?= -O3 -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter -Wno-deprecated-declarations -Wcast-align
LINK?=   $(CC)

#
# The "official" way to compile+link with pthreads is now to use the -pthread option
# *both* for the compile and the link step.
#
CFLAGS+=-pthread -I./src
LINK+=-pthread

PKG_CONFIG = pkg-config

WDSP_INCLUDE=-I./wdsp

##############################################################################
# CPP_DEFINES and CPP_SOURCES are "filled" with all  possible options,
# so that everything is processed when running "cppcheck".
##############################################################################

CPP_DEFINES=
CPP_SOURCES=
CPP_INCLUDE= $(WDSP_INCLUDE)

##############################################################################
#
# Add support for extended noise reduction, if requested. Note libspecbleach
# needs linking with the single-precision version of fftw.
#
##############################################################################

WDSP_LIBS=wdsp/libwdsp.a rnnoise/librnnoise.a libspecbleach/libspecbleach.a \
	`$(PKG_CONFIG) --libs fftw3` `$(PKG_CONFIG) --libs fftw3f`

##############################################################################
#
# Settings for optional features, to be requested by un-commenting lines above
#
##############################################################################

##############################################################################
#
# MacOSX:
# -disable GPIO, simply because it is not there
# -if AUDIO is ALSA, switch to PORTAUDIO
#
##############################################################################

ifeq ($(MACOS), YES)
GPIO=OFF
ifeq ($(AUDIO), ALSA)
AUDIO=PORTAUDIO
endif
endif

##############################################################################
#
# Add modules for MIDI [optional, requires OS support]
#
##############################################################################

ifeq ($(MIDI),ON)
MIDI_OPTIONS=-D MIDI
ifeq ($(MACOS), YES)
MIDI_OBJS= src/mac_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -framework CoreMIDI -framework Foundation
endif
ifeq ($(LINUX), YES)
MIDI_OBJS= src/alsa_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -lasound
endif
endif
CPP_DEFINES += -DMIDI
CPP_SOURCES += src/mac_midi.c src/midi2.c src/midi3.c src/midi_menu.c
CPP_SOURCES += src/alsa_midi.c src/midi2.c src/midi3.c src/midi_menu.c

##############################################################################
#
# Add libraries for USB OZY support [optional, requires libusb-1.0]
#
##############################################################################

ifeq ($(USBOZY),ON)
USBOZY_OPTIONS=-D USBOZY
USBOZY_INCLUDE=`$(PKG_CONFIG) --cflags libusb-1.0`
USBOZY_LIBS=`$(PKG_CONFIG) --libs libusb-1.0`
USBOZY_OBJS= \
src/ozyio.o
endif
CPP_DEFINES += -DUSBOZY
CPP_SOURCES += src/ozyio.c
CPP_INCLUDE += `$(PKG_CONFIG) --cflags libusb-1.0`

##############################################################################
#
# Add libraries for SoapySDR support [optional, requires SOAPY drivers]
#
# On MacOS, SoapySDR libs are installed via homebrew
# and their correct location can be found via pkg-config.
# On LINUX, we have "manually" compiled+installed Soapy stuff in
# LINUX/libinstall.sh because some older repsitories still have
# the v0.7 Soapy libraries which have a different API. This
# "manual" installation places the Soapy stuff in /usr/local
#
##############################################################################

ifeq ($(SOAPYSDR),ON)
SOAPYSDR_OPTIONS=-D SOAPYSDR
ifeq ($(MACOS), YES)
SOAPYSDR_LIBS=`$(PKG_CONFIG) --libs soapysdr`
SOAPYSDR_INCLUDE=`$(PKG_CONFIG) --cflags soapysdr`
endif
ifeq ($(LINUX), YES)
SOAPYSDR_INCLUDE= -I/usr/local/include
SOAPYSDR_LIBS= -L/usr/local/lib -lSoapySDR
endif
SOAPYSDR_OBJS= \
src/soapy_discovery.o \
src/soapy_protocol.o
endif
CPP_DEFINES += -DSOAPYSDR
CPP_SOURCES += src/soapy_discovery.c src/soapy_protocol.c
ifeq ($(MACOS), YES)
CPP_INCLUDE +=`$(PKG_CONFIG) --cflags soapysdr`
endif
ifeq ($(LINUX), YES)
CPP_INCLUDE += -I/usr/local/include
endif

##############################################################################
#
# Add libraries for GPIO support [optional, requires OS and hardware support]
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
# Add libraries for using PulseAudio [optional]
# NOTE: exactly one of the audio modules must be selected
#
##############################################################################

ifeq ($(AUDIO), PULSE)
AUDIO_OPTIONS=-DPULSEAUDIO
AUDIO_INCLUDE=
AUDIO_LIBS=-lpulse-simple -lpulse -lpulse-mainloop-glib
AUDIO_OBJS=src/pulseaudio.o
endif
CPP_DEFINES += -DPULSEAUDIO
CPP_SOURCES += src/pulseaudio.c

##############################################################################
#
# Add libraries for using ALSA [optional]
# NOTE: exactly one of the audio modules must be selected
#
##############################################################################

ifeq ($(AUDIO), ALSA)
AUDIO_OPTIONS=-DALSA
AUDIO_INCLUDE=
AUDIO_LIBS=-lasound
AUDIO_OBJS=src/audio.o
endif
CPP_DEFINES += -DALSA
CPP_SOURCES += src/audio.c

##############################################################################
#
# Add libraries for using PortAudio [optional]
# NOTE: exactly one of the audio modules must be selected
#
##############################################################################

ifeq ($(AUDIO), PORTAUDIO)
AUDIO_OPTIONS=-DPORTAUDIO
AUDIO_INCLUDE=`$(PKG_CONFIG) --cflags portaudio-2.0`
AUDIO_LIBS=`$(PKG_CONFIG) --libs portaudio-2.0`
AUDIO_OBJS=src/portaudio.o
endif
CPP_DEFINES += -DPORTAUDIO
CPP_SOURCES += src/portaudio.c

##############################################################################
#
# Add libraries for using PipeWire [optional]
# NOTE: exactly one of the audio modules must be selected
#
##############################################################################

ifeq ($(AUDIO), PIPEWIRE)
AUDIO_OPTIONS=-DPIPEWIRE
AUDIO_INCLUDE=`$(PKG_CONFIG) --cflags libpipewire-0.3`
AUDIO_LIBS=`$(PKG_CONFIG) --libs libpipewire-0.3`
AUDIO_OBJS=src/pipewire.o
endif
CPP_DEFINES += -DPIPEWIRE
CPP_SOURCES += src/pipewire.c
ifeq ($(MACOS), YES)
# PortAudio on Linux does not have/need pkg-config.
CPP_INCLUDE += `$(PKG_CONFIG) --cflags portaudio-2.0`
endif

##############################################################################
#
# Add libraries for uPNP port forwarding [optional, needs miniupnpc lib]
#
##############################################################################

ifeq ($(PORTFORWARD), ON)
PNP_OPTIONS=-DPORTFORWARD
PNP_INCLUDE=`$(PKG_CONFIG) --cflags miniupnpc`
PNP_LIBS=`$(PKG_CONFIG) --libs miniupnpc`
CPP_DEFINES += -DPORTFORWARD
CPPINCLUDE += `$(PKG_CONFIG) --cflags miniupnpc`
endif

##############################################################################
#
# Add libraries for TCI [optional, requires libwebsockets]
#
##############################################################################
ifeq ($(TCI), ON)
TCI_OPTIONS=-DTCI
TCI_INCLUDE=`$(PKG_CONFIG) --cflags libwebsockets`
TCI_OBJS=src/tci.o src/tci_audio.o
TCI_LIBS=`$(PKG_CONFIG) --libs libwebsockets`
CPP_DEFINES += -DTCI
CPPINCLUDE += `$(PKG_CONFIG) --cflags libwebsockets`
endif

##############################################################################
#
# End of "libraries for optional features" section
# Add pkg-config support for external libraries we depend on.
#
##############################################################################

ifeq ($(MACOS), YES)
TTS_OBJS=  src/MacTTS.o
TTS_LIBS= -framework Foundation -framework AVFoundation
endif

ZLIB_INCLUDE=`$(PKG_CONFIG) --cflags zlib`
ZLIB_LIBS=`$(PKG_CONFIG) --libs zlib`
CPP_INCLUDE += `$(PKG_CONFIG) --cflags zlib`

OPUS_INCLUDE=`$(PKG_CONFIG) --cflags opus`
OPUS_LIBS=`$(PKG_CONFIG) --libs opus`
CPP_INCLUDE += `$(PKG_CONFIG) --cflags opus`

DXCLUSTER_INCLUDE=`$(PKG_CONFIG) --cflags sqlite3`
DXCLUSTER_LIBS=`$(PKG_CONFIG) --libs sqlite3`
CPP_INCLUDE += $(DXCLUSTER_INCLUDE)

CURL_INCLUDE=`$(PKG_CONFIG) --cflags libcurl`
CURL_LIBS=`$(PKG_CONFIG) --libs libcurl`
CPP_INCLUDE += $(CURL_INCLUDE)


GTK_INCLUDE=`$(PKG_CONFIG) --cflags gtk+-3.0`
GTK_LIBS=`$(PKG_CONFIG) --libs gtk+-3.0`
CPP_INCLUDE += $(GTK_INCLUDE)

OPENSSL_INCLUDE=`$(PKG_CONFIG) --cflags openssl`
OPENSSL_LIBS=`$(PKG_CONFIG) --libs openssl`
CPP_INCLUDE += `$(PKG_CONFIG) --cflags openssl`

##############################################################################
#
# Specify additional OS-dependent system libraries
#
##############################################################################

ifeq ($(LINUX), YES)
SYS_LIBS=-lrt
endif

ifeq ($(MACOS), YES)
SYS_LIBS=-framework IOKit
endif

##############################################################################
#
# All the command-line options to compile the *.c files
#
##############################################################################

OPTIONS=$(MIDI_OPTIONS) $(USBOZY_OPTIONS) \
	$(GPIO_OPTIONS) $(SOAPYSDR_OPTIONS) \
	$(ANDROMEDA_OPTIONS) \
	$(STEMLAB_OPTIONS) \
	$(SERVER_OPTIONS) \
	$(TTS_OPTIONS) \
	$(AUDIO_OPTIONS) \
    $(TCI_OPTIONS) \
	$(OPUS_OPTIONS) \
	$(ZLIB_OPTIONS) \
	$(DXCLUSTER_OPTIONS) \
	$(CURL_OPTIONS) \
	$(PNP_OPTIONS) \
	-D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' -D GIT_COMMIT='"$(GIT_COMMIT)"'

INCLUDES=$(GTK_INCLUDE) $(WDSP_INCLUDE) $(OPENSSL_INCLUDE) $(AUDIO_INCLUDE) \
	$(STEMLAB_INCLUDE) $(USBOZY_INCLUDE) $(SOAPYSDR_INCLUDE) $(OPUS_INCLUDE) \
	$(ZLIB_INCLUDE) $(DXCLUSTER_INCLUDE) $(CURL_INCLUDE) $(PNP_INCLUDE)

COMPILE=$(CC) $(CFLAGS) $(OPTIONS) $(INCLUDES)

.c.o:
	$(COMPILE) -c -o $@ $<

.m.o:
	$(COMPILE) -c -o $@ $<

##############################################################################
#
# All the libraries we need to link with (including WDSP, libm, $(SYS_LIBS))
#
##############################################################################

LIBS=	$(LDFLAGS) $(AUDIO_LIBS) $(USBOZY_LIBS) $(GTK_LIBS) $(GPIO_LIBS) \
	$(SOAPYSDR_LIBS) $(STEMLAB_LIBS) $(MIDI_LIBS) $(TTS_LIBS) $(OPUS_LIBS) \
	$(ZLIB_LIBS) $(DXCLUSTER_LIBS) $(CURL_LIBS) $(PNP_LIBS) $(TCI_LIBS) \
	$(OPENSSL_LIBS) $(WDSP_LIBS) -lm $(SYS_LIBS)

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
src/dxcluster_menu.c \
src/dxcluster.c \
src/dxcluster_db.c \
src/dxcluster_history_menu.c \
src/dxcluster_popup.c \
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
src/profile_menu.c \
src/profiles.c \
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
src/saturndrivers.c \
src/saturnregisters.c \
src/saturnmain.c \
src/screen_menu.c \
src/server_menu.c \
src/server_thread.c \
src/sintab.c \
src/sliders.c \
src/sliders_menu.h \
src/startup.c \
src/stemlab_discovery.c \
src/store.c \
src/store_menu.c \
src/switch_menu.c \
src/test_menu.c \
src/theme.c \
src/theme_menu.c \
src/toolbar.c \
src/toolbar_menu.c \
src/transmitter.c \
src/tts.c \
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
src/dxcluster_menu.o \
src/dxcluster.o \
src/dxcluster_db.o \
src/dxcluster_history_menu.o \
src/dxcluster_popup.o \
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
src/profile_menu.o \
src/profiles.o \
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
src/saturndrivers.o \
src/saturnregisters.o \
src/saturnmain.o \
src/screen_menu.o \
src/server_menu.o \
src/server_thread.o \
src/sintab.o \
src/sliders.o \
src/sliders_menu.o \
src/startup.o \
src/stemlab_discovery.o \
src/store.o \
src/store_menu.o \
src/switch_menu.o \
src/test_menu.o \
src/theme.o \
src/theme_menu.o \
src/toolbar.o \
src/toolbar_menu.o \
src/transmitter.o \
src/tts.o \
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
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(TTS_OBJS) $(TCI_OBJS)
	$(COMPILE) -c -o src/version.o src/version.c
	@+make -C libspecbleach
	@+make -C rnnoise
	@+make -C wdsp
	$(LINK) -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(TTS_OBJS) $(TCI_OBJS) \
		$(LIBS)

##############################################################################
#
# "make cppcheck" invokes the cppcheck program to do a source-code checking.
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
#
# We want to use the --check-level=exhaustive flag for cppcheck. A sufficiently
# recent version of cppcheck if available on MacOS, and for Debian since
# version 13 "Trixie" which introduced kernel version 6.12
#
##############################################################################

CPP_INCLUDE:=$(shell echo $(CPP_INCLUDE) | sed -e "s/ -pthread/ /" )

CPP_OPTIONS= --inline-suppr --enable=all --suppress=unmatchedSuppression

ifeq ($(MACOS), YES)
CPP_OPTIONS += -D__APPLE__
CPP_OPTIONS += --check-level=exhaustive
endif
ifeq ($(LINUX), YES)
CPP_OPTIONS += -D__linux__
# there are "old" versions of cppcheck around which do not yet have
# check-level=exhaustive. So we do not do this check on LINUX.
endif

CPP_OPTIONS += --suppress=missingIncludeSystem
CPP_OPTIONS += --suppress=unusedFunction

.PHONY:	cppcheck
cppcheck:
	cppcheck $(CPP_OPTIONS) $(CPP_INCLUDE) $(CPP_DEFINES) $(SOURCES) $(CPP_SOURCES)

.PHONY:	clean
clean:
	rm -f src/*.o
	rm -f $(PROGRAM) hpsdrsim bootloader
	rm -rf $(PROGRAM).app
	yes | rm -rf LINUX/SoapySDR
	@make -C libspecbleach clean
	@make -C rnnoise clean
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
# Since src/MacTTS.m is Objective-C, create the final line manually
#
#############################################################################

.PHONY: DEPEND
DEPEND:
	rm -f DEPEND
	touch DEPEND
	export LC_ALL=C && makedepend -DMIDI -DUSBOZY -DSOAPYSDR -DGPIO \
		-DPULSEAUDIO  -DPORTAUDIO -DALSA -DPORTFORWARD -DTCI -D__APPLE__ -D__linux__ \
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
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(TTS_OBJS)
	@+make -C libspecbleach
	@+make -C rnnoise
	@+make -C wdsp
	$(LINK) -headerpad_max_install_names -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  \
		$(SOAPYSDR_OBJS) $(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(TTS_OBJS) \
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
src/about_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/version.h
src/action_dialog.o: src/actions.h src/main.h src/message.h
src/actions.o: src/actions.h src/agc.h src/band.h src/bandstack.h
src/actions.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/actions.o: src/transmitter.h src/discovery.h src/ext.h src/filter.h
src/actions.o: src/gpio.h src/iambic.h src/main.h src/message.h
src/actions.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/ps_menu.h
src/actions.o: src/radio.h src/adc.h src/discovered.h src/rigctl.h
src/actions.o: src/sliders.h src/store.h src/toolbar.h src/vfo.h
src/agc_menu.o: src/agc.h src/band.h src/bandstack.h src/ext.h
src/agc_menu.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/agc_menu.o: src/transmitter.h src/new_menu.h src/radio.h src/adc.h
src/agc_menu.o: src/discovered.h src/vfo.h
src/andromeda.o: src/actions.h src/band.h src/bandstack.h src/ext.h
src/andromeda.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/andromeda.o: src/transmitter.h src/new_menu.h src/radio.h src/adc.h
src/andromeda.o: src/discovered.h src/toolbar.h src/vfo.h
src/ant_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/ant_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/message.h
src/ant_menu.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/radio.h
src/ant_menu.o: src/adc.h src/discovered.h src/soapy_protocol.h
src/appearance.o: src/appearance.h src/css.h
src/audio.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/audio.o: src/client_server.h src/mode.h src/message.h src/radio.h
src/audio.o: src/adc.h src/discovered.h src/vfo.h
src/band.o: src/band.h src/bandstack.h src/filter.h src/mode.h src/message.h
src/band.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/band.o: src/receiver.h src/atomic.h src/transmitter.h src/vfo.h
src/band_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/band_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/filter.h
src/band_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/band_menu.o: src/vfo.h
src/bandstack_menu.o: src/band.h src/bandstack.h src/filter.h src/mode.h
src/bandstack_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/bandstack_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/vfo.h
src/client_server.o: src/band.h src/bandstack.h src/client_server.h
src/client_server.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/client_server.o: src/filter.h src/message.h src/radio.h src/adc.h
src/client_server.o: src/discovered.h src/store.h src/vfo.h
src/client_thread.o: src/MacOS.h src/audio.h src/receiver.h src/atomic.h
src/client_thread.o: src/transmitter.h src/band.h src/bandstack.h
src/client_thread.o: src/client_server.h src/mode.h src/ext.h src/filter.h
src/client_thread.o: src/main.h src/message.h src/meter.h src/profiles.h
src/client_thread.o: src/radio.h src/adc.h src/discovered.h
src/client_thread.o: src/rx_panadapter.h src/sliders.h src/actions.h
src/client_thread.o: src/store.h src/tci.h src/tci_audio.h
src/client_thread.o: src/tx_panadapter.h src/vfo.h src/vox.h src/waterfall.h
src/css.o: src/css.h src/message.h
src/cw_menu.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/cw_menu.o: src/transmitter.h src/ext.h src/iambic.h src/message.h
src/cw_menu.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/radio.h
src/cw_menu.o: src/adc.h src/discovered.h src/rigctl.h
src/discovered.o: src/discovered.h
src/discovery.o: src/actions.h src/client_server.h src/mode.h src/receiver.h
src/discovery.o: src/atomic.h src/transmitter.h src/discovered.h src/ext.h
src/discovery.o: src/gpio.h src/i2c.h src/main.h src/message.h
src/discovery.o: src/new_discovery.h src/old_discovery.h src/ozyio.h
src/discovery.o: src/property.h src/protocols.h src/radio.h src/adc.h
src/discovery.o: src/soapy_discovery.h src/stemlab_discovery.h src/tts.h
src/discovery.o: src/saturnmain.h
src/display_menu.o: src/client_server.h src/mode.h src/receiver.h
src/display_menu.o: src/atomic.h src/transmitter.h src/main.h src/new_menu.h
src/display_menu.o: src/radio.h src/adc.h src/discovered.h
src/diversity_menu.o: src/client_server.h src/mode.h src/receiver.h
src/diversity_menu.o: src/atomic.h src/transmitter.h src/new_menu.h
src/diversity_menu.o: src/radio.h src/adc.h src/discovered.h
src/dxcluster.o: src/dxcluster.h src/dxcluster_db.h src/property.h
src/dxcluster.o: src/message.h
src/dxcluster_db.o: src/dxcluster_db.h src/dxcluster.h src/message.h
src/dxcluster_history_menu.o: src/band.h src/bandstack.h src/dxcluster.h
src/dxcluster_history_menu.o: src/dxcluster_db.h src/dxcluster_history_menu.h
src/dxcluster_history_menu.o: src/new_menu.h src/radio.h src/adc.h
src/dxcluster_history_menu.o: src/discovered.h src/receiver.h src/atomic.h
src/dxcluster_history_menu.o: src/transmitter.h src/vfo.h src/mode.h
src/dxcluster_history_menu.o: src/message.h
src/dxcluster_menu.o: src/dxcluster.h src/dxcluster_menu.h src/new_menu.h
src/dxcluster_menu.o: src/message.h src/radio.h src/adc.h src/discovered.h
src/dxcluster_menu.o: src/receiver.h src/atomic.h src/transmitter.h
src/dxcluster_popup.o: src/dxcluster_popup.h src/dxcluster.h src/band.h
src/dxcluster_popup.o: src/bandstack.h src/main.h src/radio.h src/adc.h
src/dxcluster_popup.o: src/discovered.h src/receiver.h src/atomic.h
src/dxcluster_popup.o: src/transmitter.h src/vfo.h src/mode.h src/message.h
src/encoder_menu.o: src/action_dialog.h src/actions.h src/agc.h src/band.h
src/encoder_menu.o: src/bandstack.h src/channel.h src/gpio.h src/i2c.h
src/encoder_menu.o: src/main.h src/new_menu.h src/radio.h src/adc.h
src/encoder_menu.o: src/discovered.h src/receiver.h src/atomic.h
src/encoder_menu.o: src/transmitter.h src/vfo.h src/mode.h
src/equalizer_menu.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/equalizer_menu.o: src/atomic.h src/transmitter.h src/main.h src/message.h
src/equalizer_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/equalizer_menu.o: src/vfo.h
src/exit_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/exit_menu.o: src/receiver.h src/atomic.h src/transmitter.h
src/ext.o: src/main.h src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/ext.o: src/receiver.h src/atomic.h src/transmitter.h src/vfo.h src/mode.h
src/fft_menu.o: src/fft_menu.h src/message.h src/new_menu.h src/radio.h
src/fft_menu.o: src/adc.h src/discovered.h src/receiver.h src/atomic.h
src/fft_menu.o: src/transmitter.h
src/filter.o: src/actions.h src/ext.h src/client_server.h src/mode.h
src/filter.o: src/receiver.h src/atomic.h src/transmitter.h src/filter.h
src/filter.o: src/message.h src/property.h src/radio.h src/adc.h
src/filter.o: src/discovered.h src/sliders.h src/vfo.h
src/filter_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/filter_menu.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/filter_menu.o: src/filter.h src/message.h src/new_menu.h src/radio.h
src/filter_menu.o: src/adc.h src/discovered.h src/vfo.h
src/g2panel.o: src/actions.h src/g2panel_menu.h src/property.h
src/g2panel_menu.o: src/action_dialog.h src/actions.h src/g2panel.h
src/g2panel_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/g2panel_menu.o: src/discovered.h src/receiver.h src/atomic.h
src/g2panel_menu.o: src/transmitter.h
src/gpio.o: src/gpio.h src/actions.h src/band.h src/bandstack.h src/channel.h
src/gpio.o: src/discovered.h src/ext.h src/client_server.h src/mode.h
src/gpio.o: src/receiver.h src/atomic.h src/transmitter.h src/filter.h
src/gpio.o: src/i2c.h src/iambic.h src/main.h src/message.h
src/gpio.o: src/new_protocol.h src/MacOS.h src/property.h src/radio.h
src/gpio.o: src/adc.h src/sliders.h src/toolbar.h src/vfo.h
src/hpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/i2c.o: src/actions.h src/band.h src/bandstack.h src/ext.h
src/i2c.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/i2c.o: src/transmitter.h src/gpio.h src/i2c.h src/message.h src/radio.h
src/i2c.o: src/adc.h src/discovered.h src/toolbar.h src/vfo.h
src/iambic.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/iambic.o: src/atomic.h src/transmitter.h src/gpio.h src/iambic.h
src/iambic.o: src/main.h src/message.h src/new_protocol.h src/MacOS.h
src/iambic.o: src/radio.h src/adc.h src/discovered.h src/vfo.h
src/led.o: src/message.h
src/mac_midi.o: src/message.h src/midi.h src/actions.h src/midi_menu.h
src/main.o: src/actions.h src/appearance.h src/css.h src/audio.h
src/main.o: src/receiver.h src/atomic.h src/transmitter.h src/band.h
src/main.o: src/bandstack.h src/discovery.h src/discovered.h src/ext.h
src/main.o: src/client_server.h src/mode.h src/gpio.h src/piHPSDR_logo.h
src/main.o: src/main.h src/message.h src/new_menu.h src/new_protocol.h
src/main.o: src/MacOS.h src/old_protocol.h src/property.h src/radio.h
src/main.o: src/adc.h src/soapy_protocol.h src/startup.h src/test_menu.h
src/main.o: src/version.h src/vfo.h
src/meter.o: src/appearance.h src/css.h src/band.h src/bandstack.h
src/meter.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/meter.o: src/transmitter.h src/meter.h src/message.h src/new_menu.h
src/meter.o: src/radio.h src/adc.h src/discovered.h src/theme.h src/version.h
src/meter.o: src/vfo.h src/vox.h
src/meter_menu.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/meter_menu.o: src/transmitter.h src/meter.h src/new_menu.h src/radio.h
src/meter_menu.o: src/adc.h src/discovered.h
src/midi2.o: src/MacOS.h src/main.h src/message.h src/midi.h src/actions.h
src/midi2.o: src/midi_menu.h src/property.h
src/midi3.o: src/actions.h src/message.h src/midi.h
src/midi_menu.o: src/action_dialog.h src/actions.h src/main.h src/message.h
src/midi_menu.o: src/midi.h src/new_menu.h src/property.h src/radio.h
src/midi_menu.o: src/adc.h src/discovered.h src/receiver.h src/atomic.h
src/midi_menu.o: src/transmitter.h
src/mode_menu.o: src/band.h src/bandstack.h src/filter.h src/mode.h
src/mode_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/mode_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/vfo.h
src/new_discovery.o: src/discovered.h src/discovery.h src/message.h
src/new_menu.o: src/about_menu.h src/actions.h src/agc_menu.h src/ant_menu.h
src/new_menu.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/new_menu.o: src/band_menu.h src/bandstack_menu.h src/client_server.h
src/new_menu.o: src/mode.h src/cw_menu.h src/display_menu.h
src/new_menu.o: src/diversity_menu.h src/dxcluster_menu.h
src/new_menu.o: src/dxcluster_history_menu.h src/encoder_menu.h
src/new_menu.o: src/equalizer_menu.h src/exit_menu.h src/fft_menu.h
src/new_menu.o: src/filter_menu.h src/g2panel_menu.h src/gpio.h src/main.h
src/new_menu.o: src/meter_menu.h src/midi_menu.h src/midi.h src/mode_menu.h
src/new_menu.o: src/new_menu.h src/new_protocol.h src/MacOS.h
src/new_menu.o: src/noise_menu.h src/oc_menu.h src/old_protocol.h
src/new_menu.o: src/pa_menu.h src/profile_menu.h src/ps_menu.h
src/new_menu.o: src/radio_menu.h src/radio.h src/adc.h src/discovered.h
src/new_menu.o: src/rigctl_menu.h src/rx_menu.h src/server_menu.h
src/new_menu.o: src/screen_menu.h src/sliders_menu.h src/store_menu.h
src/new_menu.o: src/switch_menu.h src/theme_menu.h src/toolbar_menu.h
src/new_menu.o: src/tx_menu.h src/xvtr_menu.h src/vfo_menu.h src/vox_menu.h
src/new_protocol.o: src/alex.h src/atomic.h src/audio.h src/receiver.h
src/new_protocol.o: src/transmitter.h src/band.h src/bandstack.h
src/new_protocol.o: src/discovered.h src/ext.h src/client_server.h src/mode.h
src/new_protocol.o: src/filter.h src/iambic.h src/main.h src/message.h
src/new_protocol.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/new_protocol.o: src/rigctl.h src/saturnmain.h src/toolbar.h src/actions.h
src/new_protocol.o: src/vfo.h src/vox.h
src/newhpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/noise_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/noise_menu.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/noise_menu.o: src/filter.h src/message.h src/new_menu.h src/radio.h
src/noise_menu.o: src/adc.h src/discovered.h src/vfo.h
src/oc_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/oc_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/filter.h
src/oc_menu.o: src/main.h src/message.h src/new_menu.h src/new_protocol.h
src/oc_menu.o: src/MacOS.h src/radio.h src/adc.h src/discovered.h
src/old_discovery.o: src/discovered.h src/discovery.h src/message.h
src/old_discovery.o: src/old_discovery.h src/stemlab_discovery.h
src/old_protocol.o: src/MacOS.h src/atomic.h src/audio.h src/receiver.h
src/old_protocol.o: src/transmitter.h src/band.h src/bandstack.h
src/old_protocol.o: src/discovered.h src/ext.h src/client_server.h src/mode.h
src/old_protocol.o: src/filter.h src/iambic.h src/main.h src/message.h
src/old_protocol.o: src/old_protocol.h src/radio.h src/adc.h src/vfo.h
src/old_protocol.o: src/ozyio.h
src/ozyio.o: src/message.h src/ozyio.h
src/pa_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/pa_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/message.h
src/pa_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/pa_menu.o: src/vfo.h
src/piHPSDR_logo.o: src/message.h
src/portaudio.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/portaudio.o: src/client_server.h src/mode.h src/message.h src/radio.h
src/portaudio.o: src/adc.h src/discovered.h src/vfo.h
src/profile_menu.o: src/message.h src/mode.h src/new_menu.h src/profiles.h
src/profile_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/radio.h
src/profile_menu.o: src/adc.h src/discovered.h
src/profiles.o: src/agc.h src/audio.h src/receiver.h src/atomic.h
src/profiles.o: src/transmitter.h src/ext.h src/client_server.h src/mode.h
src/profiles.o: src/filter.h src/main.h src/message.h src/profiles.h
src/profiles.o: src/property.h src/radio.h src/adc.h src/discovered.h
src/profiles.o: src/vfo.h
src/property.o: src/main.h src/message.h src/property.h src/radio.h src/adc.h
src/property.o: src/discovered.h src/receiver.h src/atomic.h
src/property.o: src/transmitter.h
src/protocols.o: src/property.h src/protocols.h src/radio.h src/adc.h
src/protocols.o: src/discovered.h src/receiver.h src/atomic.h
src/protocols.o: src/transmitter.h
src/ps_menu.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/ps_menu.o: src/atomic.h src/transmitter.h src/message.h src/new_menu.h
src/ps_menu.o: src/new_protocol.h src/MacOS.h src/radio.h src/adc.h
src/ps_menu.o: src/discovered.h src/toolbar.h src/actions.h src/vfo.h
src/pulseaudio.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/pulseaudio.o: src/client_server.h src/mode.h src/message.h src/radio.h
src/pulseaudio.o: src/adc.h src/discovered.h src/vfo.h
src/radio.o: src/actions.h src/adc.h src/agc.h src/appearance.h src/css.h
src/radio.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/radio.o: src/band.h src/bandstack.h src/channel.h src/client_server.h
src/radio.o: src/mode.h src/discovered.h src/dxcluster.h src/ext.h
src/radio.o: src/filter.h src/g2panel.h src/gpio.h src/iambic.h src/main.h
src/radio.o: src/meter.h src/message.h src/midi.h src/new_menu.h
src/radio.o: src/new_protocol.h src/MacOS.h src/old_protocol.h src/profiles.h
src/radio.o: src/property.h src/radio.h src/rigctl.h src/rx_panadapter.h
src/radio.o: src/server_menu.h src/sliders.h src/tci.h src/test_menu.h
src/radio.o: src/theme.h src/toolbar.h src/tts.h src/tx_panadapter.h
src/radio.o: src/saturnmain.h src/soapy_protocol.h src/store.h src/vfo.h
src/radio.o: src/vox.h src/waterfall.h
src/radio_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/radio_menu.o: src/receiver.h src/atomic.h src/transmitter.h
src/radio_menu.o: src/discovered.h src/ext.h src/gpio.h src/main.h
src/radio_menu.o: src/message.h src/new_menu.h src/new_protocol.h src/MacOS.h
src/radio_menu.o: src/radio.h src/adc.h src/sliders.h src/actions.h
src/radio_menu.o: src/soapy_protocol.h src/vfo.h
src/receiver.o: src/agc.h src/audio.h src/receiver.h src/atomic.h
src/receiver.o: src/transmitter.h src/band.h src/bandstack.h src/channel.h
src/receiver.o: src/client_server.h src/mode.h src/discovered.h src/ext.h
src/receiver.o: src/filter.h src/main.h src/meter.h src/message.h
src/receiver.o: src/new_menu.h src/new_protocol.h src/MacOS.h
src/receiver.o: src/old_protocol.h src/profiles.h src/property.h src/radio.h
src/receiver.o: src/adc.h src/rx_panadapter.h src/sliders.h src/actions.h
src/receiver.o: src/soapy_protocol.h src/tci.h src/tci_audio.h src/vfo.h
src/receiver.o: src/vox.h src/waterfall.h
src/rigctl.o: src/actions.h src/agc.h src/andromeda.h src/band.h
src/rigctl.o: src/bandstack.h src/channel.h src/ext.h src/client_server.h
src/rigctl.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/rigctl.o: src/filter.h src/g2panel.h src/g2panel_menu.h src/iambic.h
src/rigctl.o: src/main.h src/message.h src/new_protocol.h src/MacOS.h
src/rigctl.o: src/old_protocol.h src/property.h src/radio.h src/adc.h
src/rigctl.o: src/discovered.h src/rigctl.h src/sliders.h src/store.h
src/rigctl.o: src/toolbar.h src/vfo.h
src/rigctl_menu.o: src/band.h src/bandstack.h src/message.h src/new_menu.h
src/rigctl_menu.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/rigctl_menu.o: src/atomic.h src/transmitter.h src/rigctl.h src/tci.h
src/rigctl_menu.o: src/vfo.h src/mode.h
src/rx_menu.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/rx_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/rx_menu.o: src/discovered.h src/filter.h src/message.h src/new_menu.h
src/rx_menu.o: src/new_protocol.h src/MacOS.h src/profiles.h src/radio.h
src/rx_menu.o: src/adc.h src/rx_menu.h src/sliders.h src/actions.h src/vfo.h
src/rx_panadapter.o: src/actions.h src/agc.h src/appearance.h src/css.h
src/rx_panadapter.o: src/band.h src/bandstack.h src/client_server.h
src/rx_panadapter.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/rx_panadapter.o: src/discovered.h src/dxcluster_popup.h src/dxcluster.h
src/rx_panadapter.o: src/gpio.h src/message.h src/radio.h src/adc.h
src/rx_panadapter.o: src/ozyio.h src/rx_panadapter.h src/theme.h src/vfo.h
src/saturndrivers.o: src/message.h src/saturndrivers.h src/saturnregisters.h
src/saturnmain.o: src/discovered.h src/message.h src/new_protocol.h
src/saturnmain.o: src/MacOS.h src/receiver.h src/atomic.h src/saturndrivers.h
src/saturnmain.o: src/saturnregisters.h src/saturnmain.h
src/saturnregisters.o: src/saturndrivers.h src/saturnregisters.h
src/saturnregisters.o: src/message.h
src/screen_menu.o: src/appearance.h src/css.h src/ext.h src/client_server.h
src/screen_menu.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/screen_menu.o: src/main.h src/message.h src/new_menu.h src/radio.h
src/screen_menu.o: src/adc.h src/discovered.h
src/server_menu.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/server_menu.o: src/transmitter.h src/main.h src/message.h src/new_menu.h
src/server_menu.o: src/radio.h src/adc.h src/discovered.h src/server_menu.h
src/server_thread.o: src/actions.h src/atomic.h src/band.h src/bandstack.h
src/server_thread.o: src/client_server.h src/mode.h src/receiver.h
src/server_thread.o: src/transmitter.h src/ext.h src/filter.h src/iambic.h
src/server_thread.o: src/main.h src/message.h src/new_protocol.h src/MacOS.h
src/server_thread.o: src/profiles.h src/radio.h src/adc.h src/discovered.h
src/server_thread.o: src/soapy_protocol.h src/store.h src/vfo.h
src/sliders.o: src/actions.h src/ext.h src/client_server.h src/mode.h
src/sliders.o: src/receiver.h src/atomic.h src/transmitter.h src/main.h
src/sliders.o: src/message.h src/property.h src/radio.h src/adc.h
src/sliders.o: src/discovered.h src/sliders.h
src/sliders_menu.o: src/action_dialog.h src/actions.h src/new_menu.h
src/sliders_menu.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/sliders_menu.o: src/atomic.h src/transmitter.h src/sliders.h
src/soapy_discovery.o: src/discovered.h src/message.h src/soapy_discovery.h
src/soapy_protocol.o: src/audio.h src/receiver.h src/atomic.h
src/soapy_protocol.o: src/transmitter.h src/band.h src/bandstack.h
src/soapy_protocol.o: src/channel.h src/discovered.h src/ext.h
src/soapy_protocol.o: src/client_server.h src/mode.h src/filter.h src/main.h
src/soapy_protocol.o: src/message.h src/radio.h src/adc.h
src/soapy_protocol.o: src/soapy_protocol.h src/vfo.h
src/startup.o: src/message.h
src/stemlab_discovery.o: src/discovered.h src/discovery.h src/main.h
src/stemlab_discovery.o: src/message.h src/radio.h src/adc.h src/receiver.h
src/stemlab_discovery.o: src/atomic.h src/transmitter.h
src/store.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/store.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/store.o: src/filter.h src/message.h src/profiles.h src/property.h
src/store.o: src/radio.h src/adc.h src/discovered.h src/store.h
src/store.o: src/store_menu.h src/vfo.h
src/store_menu.o: src/filter.h src/mode.h src/message.h src/new_menu.h
src/store_menu.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/store_menu.o: src/atomic.h src/transmitter.h src/store_menu.h src/store.h
src/switch_menu.o: src/action_dialog.h src/actions.h src/agc.h src/band.h
src/switch_menu.o: src/bandstack.h src/channel.h src/gpio.h src/i2c.h
src/switch_menu.o: src/main.h src/new_menu.h src/radio.h src/adc.h
src/switch_menu.o: src/discovered.h src/receiver.h src/atomic.h
src/switch_menu.o: src/transmitter.h src/toolbar.h src/vfo.h src/mode.h
src/tci.o: src/radio.h src/adc.h src/discovered.h src/receiver.h src/atomic.h
src/tci.o: src/transmitter.h src/vfo.h src/mode.h src/rigctl.h src/ext.h
src/tci.o: src/client_server.h src/message.h src/main.h src/discovery.h
src/tci.o: src/tci_audio.h src/audio.h src/band.h src/bandstack.h
src/tci.o: src/filter.h src/agc.h src/sliders.h src/actions.h
src/tci_audio.o: src/message.h src/receiver.h src/atomic.h src/tci_audio.h
src/tci_audio.o: src/tci.h
src/test_menu.o: src/actions.h src/message.h
src/theme.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/theme.o: src/atomic.h src/transmitter.h src/theme.h
src/theme_menu.o: src/appearance.h src/css.h src/ext.h src/client_server.h
src/theme_menu.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/theme_menu.o: src/main.h src/message.h src/new_menu.h src/radio.h
src/theme_menu.o: src/adc.h src/discovered.h src/theme.h
src/toolbar.o: src/actions.h src/gpio.h src/message.h src/property.h
src/toolbar.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/toolbar.o: src/atomic.h src/transmitter.h src/toolbar.h
src/toolbar_menu.o: src/action_dialog.h src/actions.h src/gpio.h
src/toolbar_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/toolbar_menu.o: src/receiver.h src/atomic.h src/transmitter.h
src/toolbar_menu.o: src/toolbar.h
src/transmitter.o: src/atomic.h src/audio.h src/receiver.h src/transmitter.h
src/transmitter.o: src/band.h src/bandstack.h src/channel.h src/ext.h
src/transmitter.o: src/client_server.h src/mode.h src/filter.h src/main.h
src/transmitter.o: src/meter.h src/message.h src/new_protocol.h src/MacOS.h
src/transmitter.o: src/old_protocol.h src/ozyio.h src/profiles.h
src/transmitter.o: src/property.h src/ps_menu.h src/radio.h src/adc.h
src/transmitter.o: src/discovered.h src/sintab.h src/sliders.h src/actions.h
src/transmitter.o: src/soapy_protocol.h src/tci.h src/tci_audio.h
src/transmitter.o: src/toolbar.h src/tx_panadapter.h src/vfo.h src/vox.h
src/transmitter.o: src/waterfall.h
src/tts.o: src/message.h src/radio.h src/adc.h src/discovered.h
src/tts.o: src/receiver.h src/atomic.h src/transmitter.h src/vfo.h src/mode.h
src/tts.o: src/MacTTS.h
src/tx_menu.o: src/audio.h src/receiver.h src/atomic.h src/transmitter.h
src/tx_menu.o: src/ext.h src/client_server.h src/mode.h src/filter.h
src/tx_menu.o: src/gpio.h src/message.h src/new_menu.h src/new_protocol.h
src/tx_menu.o: src/MacOS.h src/profiles.h src/radio.h src/adc.h
src/tx_menu.o: src/discovered.h src/sliders.h src/actions.h src/vfo.h
src/tx_panadapter.o: src/actions.h src/agc.h src/appearance.h src/css.h
src/tx_panadapter.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/tx_panadapter.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/tx_panadapter.o: src/discovered.h src/gpio.h src/message.h src/radio.h
src/tx_panadapter.o: src/adc.h src/rx_panadapter.h src/theme.h
src/tx_panadapter.o: src/tx_panadapter.h src/vfo.h
src/vfo.o: src/appearance.h src/css.h src/audio.h src/receiver.h src/atomic.h
src/vfo.o: src/transmitter.h src/discovered.h src/main.h src/agc.h src/mode.h
src/vfo.o: src/filter.h src/bandstack.h src/band.h src/profiles.h
src/vfo.o: src/property.h src/radio.h src/adc.h src/new_protocol.h
src/vfo.o: src/MacOS.h src/vfo.h src/channel.h src/toolbar.h src/actions.h
src/vfo.o: src/rigctl.h src/client_server.h src/ext.h src/message.h
src/vfo.o: src/sliders.h src/theme.h
src/vfo_menu.o: src/band.h src/bandstack.h src/ext.h src/client_server.h
src/vfo_menu.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/vfo_menu.o: src/filter.h src/new_menu.h src/radio.h src/adc.h
src/vfo_menu.o: src/discovered.h src/radio_menu.h src/vfo.h
src/vox.o: src/radio.h src/adc.h src/discovered.h src/receiver.h src/atomic.h
src/vox.o: src/transmitter.h src/vox.h src/vfo.h src/mode.h src/ext.h
src/vox.o: src/client_server.h
src/vox_menu.o: src/ext.h src/client_server.h src/mode.h src/receiver.h
src/vox_menu.o: src/atomic.h src/transmitter.h src/led.h src/message.h
src/vox_menu.o: src/new_menu.h src/radio.h src/adc.h src/discovered.h
src/vox_menu.o: src/sliders.h src/actions.h src/vfo.h src/vox.h
src/waterfall.o: src/radio.h src/adc.h src/discovered.h src/receiver.h
src/waterfall.o: src/atomic.h src/transmitter.h src/vfo.h src/mode.h
src/waterfall.o: src/band.h src/bandstack.h src/message.h src/waterfall.h
src/xvtr_menu.o: src/band.h src/bandstack.h src/client_server.h src/mode.h
src/xvtr_menu.o: src/receiver.h src/atomic.h src/transmitter.h src/filter.h
src/xvtr_menu.o: src/message.h src/new_menu.h src/radio.h src/adc.h
src/xvtr_menu.o: src/discovered.h src/vfo.h
src/action_dialog.o: src/actions.h
src/appearance.o: src/css.h
src/audio.o: src/receiver.h src/atomic.h src/transmitter.h
src/band.o: src/bandstack.h
src/client_server.o: src/mode.h src/receiver.h src/atomic.h src/transmitter.h
src/dxcluster_db.o: src/dxcluster.h
src/dxcluster_popup.o: src/dxcluster.h
src/ext.o: src/client_server.h src/mode.h src/receiver.h src/atomic.h
src/ext.o: src/transmitter.h
src/filter.o: src/mode.h
src/midi.o: src/actions.h
src/midi_menu.o: src/midi.h src/actions.h
src/new_protocol.o: src/MacOS.h src/receiver.h src/atomic.h
src/profiles.o: src/receiver.h src/atomic.h src/transmitter.h
src/radio.o: src/adc.h src/discovered.h src/receiver.h src/atomic.h
src/radio.o: src/transmitter.h
src/receiver.o: src/atomic.h
src/rx_panadapter.o: src/receiver.h src/atomic.h
src/saturndrivers.o: src/saturnregisters.h
src/sliders.o: src/actions.h src/receiver.h src/atomic.h src/transmitter.h
src/soapy_protocol.o: src/receiver.h src/atomic.h src/transmitter.h
src/tci.o: src/receiver.h src/atomic.h
src/toolbar.o: src/actions.h
src/transmitter.o: src/atomic.h
src/tx_panadapter.o: src/transmitter.h src/atomic.h
src/vfo.o: src/receiver.h src/atomic.h src/mode.h
src/vox.o: src/transmitter.h src/atomic.h
src/waterfall.o: src/receiver.h src/atomic.h
src/MacTTS.o: src/message.h
