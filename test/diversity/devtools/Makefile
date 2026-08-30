#
# DEVELOPMENT TOOLS for the diversity work. Not part of piHPSDR.
#
#   make -C test/diversity/devtools          build replay_rade, test_capture
#   make -C test/diversity/devtools run      round-trip check
#   make -C test/diversity/devtools score RADE_DIR=/path/to/radae
#
# This whole directory is deleted before the diversity work is submitted
# upstream, along with src/diversity_capture.[ch] and the guarded blocks
# in src/diversity_auto.c, src/diversity_menu.c and the top-level
# Makefile. See README.md.
#
TOP     = ../../..
BUILD   = build

#
# Same flags as test/diversity: no audio-backend define, because
# receiver.h only conditions on PORTAUDIO/ALSA/PULSEAUDIO for buffer
# types none of this touches, and naming one makes the build machine
# specific for no gain.
#
CFLAGS  = -O2 -Wall -I. -I$(BUILD) -I$(TOP)/src -I$(TOP)/wdsp \
          `pkg-config --cflags gtk+-3.0 fftw3f`
LIBS    = `pkg-config --libs gtk+-3.0 fftw3f` -lm

#
# The correlator used here is GENERATED from src/rade_correlator.c, with
# the constants named in tunable.manifest lifted into a struct so they can
# be swept without a rebuild. The original is never modified - that file
# is the one the diversity patch must not leave marks on.
#
GEN     = $(BUILD)/rade_correlator_tunable.c
SRC     = $(GEN) rade_tuning.c divcap_replay.c $(TOP)/src/diversity_auto.c

all: replay_rade test_capture run_ref

$(BUILD):
	mkdir -p $(BUILD)

#
# Regenerated whenever the correlator or the manifest changes. The
# generator exits non-zero if a manifest entry is missing, so a renamed
# constant stops the build instead of silently dropping out of the sweep.
#
$(GEN): $(TOP)/src/rade_correlator.c tunable.manifest mktunable.awk | $(BUILD)
	awk -v manifest=tunable.manifest -f mktunable.awk $< > $@ || (rm -f $@; false)

replay_rade: replay_rade.c $(SRC) rade_tuning.h divcap_replay.h
	$(CC) $(CFLAGS) -o $@ replay_rade.c $(SRC) $(LIBS)

#
# run_ref drives the whole engine - the worker thread, div_process_block()
# and every reference - rather than calling the correlator directly, which
# is the only way to reach the Digital I/Q solve over a recording.
#
run_ref: run_ref.c $(SRC) rade_tuning.h divcap_replay.h
	$(CC) $(CFLAGS) -o $@ run_ref.c $(SRC) $(LIBS)

#
# test_capture drives the *real* writer, so its copy of diversity_auto.c
# has to be the one with the capture hook compiled in.
#
test_capture: test_capture.c $(SRC) $(TOP)/src/diversity_capture.c rade_tuning.h divcap_replay.h
	$(CC) $(CFLAGS) -DDIVERSITY_CAPTURE -o $@ test_capture.c \
	      $(TOP)/src/diversity_capture.c $(SRC) $(LIBS)

run: test_capture
	./test_capture

#
# Decode scoring against librade. Optional and off by default: librade
# pulls in ONNX Runtime and Opus and needs its .onnx models at runtime,
# and none of the rest of this may depend on that being present.
#
#   make score RADE_DIR=$(HOME)/sdr/AetherSDR/third_party/radae \
#              RADE_LIB=$(TOP)/radae_nopy/build/src/librade.a
#
#   RADE_DIR   the radae source tree (contains src/rade_api.h)
#   RADE_INC   extra -I paths its private headers need - librade's
#              internals pull in opus's dnn/nnet.h and opus_types.h, so
#              this usually points into the opus build it was made with
#   RADE_LIB   the built library
#
RADE_LIB ?= $(TOP)/radae_nopy/build/src/librade.a
RADE_INC ?=

score: score_rade

#
# score_rade #includes the generated correlator rather than linking it -
# it has to reach the NCO, the decimator and the ring they write into,
# all of which are static - so $(GEN) is deliberately left off its link
# line.
#
score_rade: score_rade.c $(GEN) rade_tuning.c divcap_replay.c rade_tuning.h
ifndef RADE_DIR
	@echo "score_rade needs RADE_DIR=/path/to/radae (containing src/rade_api.h)"; false
endif
	$(CC) $(CFLAGS) -I$(RADE_DIR)/src $(addprefix -I,$(RADE_INC)) \
	      `pkg-config --cflags opus` -o $@ score_rade.c \
	      rade_tuning.c divcap_replay.c $(TOP)/src/diversity_auto.c \
	      $(RADE_LIB) `pkg-config --libs opus` $(LIBS)

clean:
	rm -rf $(BUILD) replay_rade test_capture score_rade run_ref

.PHONY: all run score clean
