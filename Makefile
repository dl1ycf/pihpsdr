# get the OS Name
UNAME_S := $(shell uname -s)

# Get git commit version and date
GIT_DATE := $(firstword $(shell git --no-pager show --date=short --format="%ai" --name-only))
GIT_VERSION := $(shell git describe --abbrev=0 --tags --always)

# uncomment the following line to force 480x320 screen
#SMALL_SCREEN_OPTIONS=-D SMALL_SCREEN

# uncomment the line below to include GPIO (needs libgpiod)
# For support of:
#    CONTROLLER1 (Original Controller)
#    CONTROLLER2_V1 single encoders with MCP23017 switches
#    CONTROLLER2_V2 dual encoders with MCP23017 switches
#    G2_FRONTPANEL dual encoders with MCP23017 switches
#
GPIO_INCLUDE=GPIO

# uncomment the line below to include MIDI support (needs MIDI support)
MIDI_INCLUDE=MIDI

# uncomment the line below to include ANDROMEDA support
# ANDROMEDA_OPTIONS=-D ANDROMEDA

# uncomment the line below to include USB Ozy support (needs libusb)
#USBOZY_INCLUDE=USBOZY

# uncomment the line below for SoapySDR (needs SOAPY libs)
# SOAPYSDR_INCLUDE=SOAPYSDR

# uncomment the line below to include support for STEMlab discovery (needs libcurl)
#STEMLAB_DISCOVERY=STEMLAB_DISCOVERY

# uncomment to get ALSA audio module on Linux (default is now to use pulseaudio)
#AUDIO_MODULE=ALSA

# un-comment if you link with an extended WDSP library containing new "external"
# noise reduction capabilities ("rnnoise" and "libspecbleach")
# (see: github.com/vu3rdd/wdsp). To use this, an extended version of WDSP has
# to be installed that contains calls to the "rnnoise" and "libspecbleach"
# libraries. The new features are called "NR3" (rnnoise) and "NR4" (libspecbleach),
# and the noise menu allows to speficy the NR4 parameters.
#EXTENDED_NOISE_REDUCTION_OPTIONS=EXTNR

# very early code not included yet
#SERVER_INCLUDE=SERVER

CFLAGS?= -O3 -Wno-deprecated-declarations -Wall
LINK?=   $(CC)

PKG_CONFIG = pkg-config

##############################################################################
#
# Settings for optional features, to be requested by un-commenting lines above
#
##############################################################################
#
# Add modules for MIDI if requested.
# Note these are different for Linux/MacOS
#
ifeq ($(MIDI_INCLUDE),MIDI)
MIDI_OPTIONS=-D MIDI
MIDI_HEADERS= midi.h midi_menu.h alsa_midi.h
ifeq ($(UNAME_S), Darwin)
MIDI_SOURCES= mac_midi.c midi2.c midi3.c midi_menu.c
MIDI_OBJS= mac_midi.o midi2.o midi3.o midi_menu.o
MIDI_LIBS= -framework CoreMIDI -framework Foundation
endif
ifeq ($(UNAME_S), Linux)
MIDI_SOURCES= alsa_midi.c midi2.c midi3.c midi_menu.c
MIDI_OBJS= alsa_midi.o midi2.o midi3.o midi_menu.o
MIDI_LIBS= -lasound
endif
endif

#
# Add libraries for USB OZY support, if requested
#
ifeq ($(USBOZY_INCLUDE),USBOZY)
USBOZY_OPTIONS=-D USBOZY
USBOZY_LIBS=-lusb-1.0
USBOZY_SOURCES= \
ozyio.c
USBOZY_HEADERS= \
ozyio.h
USBOZY_OBJS= \
ozyio.o
endif

#
# Add libraries for SoapySDR support, if requested
#
ifeq ($(SOAPYSDR_INCLUDE),SOAPYSDR)
SOAPYSDR_OPTIONS=-D SOAPYSDR
SOAPYSDRLIBS=-lSoapySDR
SOAPYSDR_SOURCES= \
soapy_discovery.c \
soapy_protocol.c
SOAPYSDR_HEADERS= \
soapy_discovery.h \
soapy_protocol.h
SOAPYSDR_OBJS= \
soapy_discovery.o \
soapy_protocol.o
endif

#
# disable GPIO for MacOS, in case it has erroneously been requested
#
ifeq ($(UNAME_S), Darwin)
GPIO_INCLUDE=
endif

#
# Add libraries for GPIO support, if requested
#
ifeq ($(GPIO_INCLUDE),GPIO)
GPIO_OPTIONS=-D GPIO
GPIOD_VERSION=$(shell pkg-config --modversion libgpiod)
ifeq ($(GPIOD_VERSION),1.2)
GPIO_OPTIONS += -D OLD_GPIOD
endif
GPIO_LIBS=-lgpiod -li2c
endif

#
# Activate code for RedPitaya (Stemlab/Hamlab/plain vanilla), if requested
# This code detects the RedPitaya by its WWW interface and starts the SDR
# application. If the SDR application starts automatically, this is
# not needed!
#
ifeq ($(STEMLAB_DISCOVERY), STEMLAB_DISCOVERY)
STEMLAB_OPTIONS=-D STEMLAB_DISCOVERY `$(PKG_CONFIG) --cflags libcurl`
STEMLAB_LIBS=`$(PKG_CONFIG) --libs libcurl`
STEMLAB_SOURCES=stemlab_discovery.c
STEMLAB_HEADERS=stemlab_discovery.h
STEMLAB_OBJS=stemlab_discovery.o
endif

#
# Activate code for remote operation, if requested.
# This feature is not yet finished. If finished, it
# allows to run two instances of piHPSDR on two
# different computers, one interacting with the operator
# and the other talking to the radio, and both computers
# may be connected by a long-distance internet connection.
#
ifeq ($(SERVER_INCLUDE), SERVER)
SERVER_OPTIONS=-D CLIENT_SERVER
SERVER_SOURCES= \
client_server.c server_menu.c
SERVER_HEADERS= \
client_server.h server_menu.h
SERVER_OBJS= \
client_server.o server_menu.o
endif

#
# Options for audio module
#  - MacOS: only PORTAUDIO tested (although PORTAUDIO might work)
#  - Linux: either PULSEAUDIO (default) or ALSO (upon request)
#
ifeq ($(UNAME_S), Darwin)
    AUDIO_MODULE=PORTAUDIO
endif
ifeq ($(UNAME_S), Linux)
  ifeq ($(AUDIO_MODULE) , ALSA)
    AUDIO_MODULE=ALSA
  else
    AUDIO_MODULE=PULSEAUDIO
  endif
endif

#
# Add libraries for using PulseAudio, if requested
#
ifeq ($(AUDIO_MODULE), PULSEAUDIO)
AUDIO_OPTIONS=-DPULSEAUDIO
AUDIO_LIBS=-lpulse-simple -lpulse -lpulse-mainloop-glib
AUDIO_SOURCES=pulseaudio.c
AUDIO_OBJS=pulseaudio.o
endif

#
# Add libraries for using ALSA, if requested
#
ifeq ($(AUDIO_MODULE), ALSA)
AUDIO_OPTIONS=-DALSA
AUDIO_LIBS=-lasound
AUDIO_SOURCES=audio.c
AUDIO_OBJS=audio.o
endif

#
# Add libraries for using PortAudio, if requested
#
ifeq ($(AUDIO_MODULE), PORTAUDIO)
AUDIO_OPTIONS=-DPORTAUDIO `$(PKG_CONFIG) --cflags portaudio-2.0`
AUDIO_LIBS=`$(PKG_CONFIG) --libs portaudio-2.0`
AUDIO_SOURCES=portaudio.c
AUDIO_OBJS=portaudio.o
endif

ifeq ($(EXTENDED_NOISE_REDUCTION_OPTIONS), EXTNR)
EXTNOISE_OPTIONS=-DEXTNR `$(PKG_CONFIG) --cflags libspecbleach` `$(PKG_CONFIG) --cflags rnnoise`
EXTNOISE_LIBS=`$(PKG_CONFIG) --libs libspecbleach` `$(PKG_CONFIG) --libs rnnoise`
endif

##############################################################################
#
# End of "libraries for optional features" section
#
##############################################################################

#
# Includes and Libraries for the graphical user interface (GTK)
#
GTKINCLUDES=`$(PKG_CONFIG) --cflags gtk+-3.0`
GTKLIBS=`$(PKG_CONFIG) --libs gtk+-3.0`

#
# Specify additional OS-dependent system libraries
#
ifeq ($(UNAME_S), Linux)
SYSLIBS=-lrt
endif

ifeq ($(UNAME_S), Darwin)
SYSLIBS=-framework IOKit
endif

#
# All the command-line options to compile the *.c files
#
OPTIONS=$(SMALL_SCREEN_OPTIONS) $(MIDI_OPTIONS) $(USBOZY_OPTIONS) \
	$(GPIO_OPTIONS) $(SOAPYSDR_OPTIONS) \
	$(ANDROMEDA_OPTIONS) \
	$(STEMLAB_OPTIONS) \
	$(SERVER_OPTIONS) \
	$(AUDIO_OPTIONS) $(EXTNOISE_OPTIONS) \
	-D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' $(DEBUG_OPTION)

INCLUDES=$(GTKINCLUDES)
COMPILE=$(CC) $(CFLAGS) $(OPTIONS) $(INCLUDES)

.c.o:
	$(COMPILE) -c -o $@ $<

#
# All the libraries we need to link with, including WDSP, libpthread, and libm
#
LIBS=	$(LDFLAGS) $(AUDIO_LIBS) $(USBOZY_LIBS) $(GTKLIBS) $(GPIO_LIBS) $(SOAPYSDRLIBS) $(STEMLAB_LIBS) \
	$(MIDI_LIBS) -lwdsp -lpthread -lm $(SYSLIBS) $(EXTNOISE_LIBS)

#
# The main target, the pihpsdr program
#
PROGRAM=pihpsdr

#
# All the *.c files
#
SOURCES= \
MacOS.c \
about_menu.c \
actions.c \
action_dialog.c \
agc_menu.c \
ant_menu.c \
band.c \
band_menu.c \
bandstack_menu.c \
button_text.c \
css.c \
configure.c \
cw_menu.c \
cwramp.c \
discovered.c \
discovery.c \
display_menu.c \
diversity_menu.c \
encoder_menu.c \
equalizer_menu.c \
exit_menu.c \
ext.c \
fft_menu.c \
filter.c \
filter_menu.c \
gpio.c \
i2c.c \
iambic.c \
led.c \
main.c \
meter.c \
meter_menu.c \
mode.c \
mode_menu.c \
new_discovery.c \
new_menu.c \
new_protocol.c \
noise_menu.c \
oc_menu.c \
old_discovery.c \
old_protocol.c \
pa_menu.c \
property.c \
protocols.c \
ps_menu.c \
radio.c \
radio_menu.c \
receiver.c \
rigctl.c \
rigctl_menu.c \
rx_menu.c \
rx_panadapter.c \
sintab.c \
sliders.c \
step_menu.c \
store.c \
store_menu.c \
switch_menu.c \
toolbar.c \
toolbar_menu.c \
transmitter.c \
tx_menu.c \
tx_panadapter.c \
version.c \
vfo.c \
vfo_menu.c \
vox.c \
vox_menu.c \
waterfall.c \
xvtr_menu.c \
zoompan.c

#
# All the *.h (header) files
#
HEADERS= \
MacOS.h \
about_menu.h \
actions.h \
action_dialog.h \
adc.h \
agc.h \
agc_menu.h \
alex.h \
ant_menu.h \
appearance.h \
band.h \
band_menu.h \
bandstack_menu.h \
bandstack.h \
button_text.h \
channel.h \
configure.h \
css.h \
cw_menu.h \
dac.h \
discovered.h \
discovery.h \
display_menu.h \
diversity_menu.h \
encoder_menu.h \
equalizer_menu.h \
exit_menu.h \
ext.h \
fft_menu.h \
filter.h \
filter_menu.h \
gpio.h \
iambic.h \
i2c.h \
led.h \
main.h \
meter.h \
meter_menu.h \
mode.h \
mode_menu.h \
new_discovery.h \
new_menu.h \
new_protocol.h \
noise_menu.h \
oc_menu.h \
old_discovery.h \
old_protocol.h \
pa_menu.h \
property.h \
protocols.h \
ps_menu.h \
radio.h \
radio_menu.h \
receiver.h \
rigctl.h \
rigctl_menu.h \
rx_menu.h \
rx_panadapter.h \
sintab.h \
sliders.h \
step_menu.h \
store.h \
store_menu.h \
switch_menu.h \
toolbar.h \
toolbar_menu.h \
transmitter.h \
tx_menu.h \
tx_panadapter.h \
version.h \
vfo.h \
vfo_menu.h \
vox.h \
vox_menu.h \
waterfall.h \
xvtr_menu.h \
zoompan.h

#
# All the *.o (object) files
#
OBJS= \
MacOS.o \
about_menu.o \
actions.o \
action_dialog.o \
agc_menu.o \
ant_menu.o \
band.o \
band_menu.o \
bandstack_menu.o \
button_text.o \
configure.o \
css.o \
cw_menu.o \
cwramp.o \
discovered.o \
discovery.o \
display_menu.o \
diversity_menu.o \
encoder_menu.o \
equalizer_menu.o \
exit_menu.o \
ext.o \
fft_menu.o \
filter.o \
filter_menu.o \
gpio.o \
iambic.o \
i2c.o \
led.o \
main.o \
meter.o \
meter_menu.o \
mode.o \
mode_menu.o \
new_discovery.o \
new_menu.o \
new_protocol.o \
noise_menu.o \
oc_menu.o \
old_discovery.o \
old_protocol.o \
pa_menu.o \
property.o \
protocols.o \
ps_menu.o \
radio.o \
radio_menu.o \
receiver.o \
rigctl.o \
rigctl_menu.o \
rx_menu.o \
rx_panadapter.o \
sintab.o \
sliders.o \
step_menu.o \
store.o \
store_menu.o \
switch_menu.o \
toolbar.o \
toolbar_menu.o \
transmitter.o \
tx_menu.o \
tx_panadapter.o \
version.o \
vfo.o \
vfo_menu.o \
vox.o \
vox_menu.o \
xvtr_menu.o \
waterfall.o \
zoompan.o

#
# How to link the program
#
$(PROGRAM):  $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS)
	$(LINK) -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(LIBS)

.PHONY:	all
all:	prebuild  $(PROGRAM) $(HEADERS) $(AUDIO_HEADERS) $(USBOZY_HEADERS) $(SOAPYSDR_HEADERS) \
	$(MIDI_HEADERS) $(STEMLAB_HEADERS) $(SERVER_HEADERS) $(AUDIO_SOURCES) $(SOURCES) \
	$(USBOZY_SOURCES) $(SOAPYSDR_SOURCES) \
	$(MIDI_SOURCES) $(STEMLAB_SOURCES) $(SERVER_SOURCES)

.PHONY:	prebuild
prebuild:
	rm -f version.o

#
# "make check" invokes the cppcheck program to do a source-code checking.
#
# On some platforms, INCLUDES contains "-pthread"  (from a pkg-config output)
# which is not a valid cppcheck option
# Therefore, correct this here. Furthermore, we can add additional options to cppcheck
# in the variable CPPOPTIONS
#
CPPOPTIONS= --enable=all
ifeq ($(UNAME_S), Darwin)
CPPOPTIONS += -D__APPLE__
else
CPPOPTIONS += -D__linux__
endif
CPPINCLUDES:=$(shell echo $(INCLUDES) | sed -e "s/-pthread / /" )

.PHONY:	cppcheck
cppcheck:
	cppcheck $(CPPOPTIONS) $(OPTIONS) $(CPPINCLUDES) $(AUDIO_SOURCES) $(SOURCES) $(USBOZY_SOURCES) \
	$(SOAPYSDR_SOURCES) $(MIDI_SOURCES) $(STEMLAB_SOURCES) $(SERVER_SOURCES)

.PHONY:	clean
clean:
	-rm -f *.o
	-rm -f $(PROGRAM) hpsdrsim bootloader
	-rm -rf $(PROGRAM).app

#
# If $DESTDIR is set, copy to that directory, otherwise use /usr/local/bin
#
DESTDIR?= /usr/local/bin

.PHONY:	install
install: $(PROGRAM)
	cp $(PROGRAM) $(DESTDIR)

.PHONY:	release
release: $(PROGRAM)
	cp $(PROGRAM) release/pihpsdr
	cd release; tar cvf pihpsdr.tar pihpsdr
	cd release; tar cvf pihpsdr-$(GIT_VERSION).tar pihpsdr

.PHONY:	nocontroller
nocontroller: clean controller1 $(PROGRAM)
	cp $(PROGRAM) release/pihpsdr
	cd release; tar cvf pihpsdr-nocontroller.$(GIT_VERSION).tar pihpsdr

.PHONY:	controller1
controller1: clean $(PROGRAM)
	cp $(PROGRAM) release/pihpsdr
	cd release; tar cvf pihpsdr-controller1.$(GIT_VERSION).tar pihpsdr

.PHONY:	controller2v1
controller2v1: clean $(PROGRAM)
	cp $(PROGRAM) release/pihpsdr
	cd release; tar cvf pihpsdr-controller2-v1.$(GIT_VERSION).tar pihpsdr

.PHONY:	controller2v2
controller2v2: clean $(PROGRAM)
	cp $(PROGRAM) release/pihpsdr
	cd release; tar cvf pihpsdr-controller2-v2.$(GIT_VERSION).tar pihpsdr


#############################################################################
#
# hpsdrsim is a cool program that emulates an SDR board with UDP and TCP
# facilities. It even feeds back the TX signal and distorts it, so that
# you can test PureSignal.
# This feature only works if the sample rate is 48000
#
#############################################################################

hpsdrsim.o:     hpsdrsim.c  hpsdrsim.h
	$(CC) -c -O hpsdrsim.c
	
newhpsdrsim.o:	newhpsdrsim.c hpsdrsim.h
	$(CC) -c -O newhpsdrsim.c

hpsdrsim:       hpsdrsim.o newhpsdrsim.o
	$(LINK) -o hpsdrsim hpsdrsim.o newhpsdrsim.o -lm -lpthread

#############################################################################
#
# bootloader is a small command-line program that allows to
# set the radio's IP address and upload firmware through the
# ancient protocol. This program can only be run as root since
# this protocol requires "sniffing" at the Ethernet adapter
# (this "sniffing" is done via the pcap library)
#
#############################################################################

bootloader:	bootloader.c
	$(CC) -o bootloader bootloader.c -lpcap

debian:
	cp $(PROGRAM) pkg/pihpsdr/usr/local/bin
	cp /usr/local/lib/libwdsp.so pkg/pihpsdr/usr/local/lib
	cp release/pihpsdr/hpsdr.png pkg/pihpsdr/usr/share/pihpsdr
	cp release/pihpsdr/hpsdr_icon.png pkg/pihpsdr/usr/share/pihpsdr
	cp release/pihpsdr/pihpsdr.desktop pkg/pihpsdr/usr/share/applications
	cd pkg; dpkg-deb --build pihpsdr

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
#       have all libraries (including WDSP) and possibly the SoapySDR support
#       modules installed.
#############################################################################

.PHONY: app
app:	$(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS)
	$(LINK) -headerpad_max_install_names -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  \
		$(SOAPYSDR_OBJS) $(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(EXTNOISE_LIBS) \
		$(LIBS) $(LDFLAGS)
	@rm -rf pihpsdr.app
	@mkdir -p pihpsdr.app/Contents/MacOS
	@mkdir -p pihpsdr.app/Contents/Frameworks
	@mkdir -p pihpsdr.app/Contents/Resources
	@cp pihpsdr pihpsdr.app/Contents/MacOS/pihpsdr
	@cp MacOS/PkgInfo pihpsdr.app/Contents
	@cp MacOS/Info.plist pihpsdr.app/Contents
	@cp MacOS/hpsdr.icns pihpsdr.app/Contents/Resources/hpsdr.icns
	@cp MacOS/hpsdr.png pihpsdr.app/Contents/Resources
#############################################################################
