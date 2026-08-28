#
# Standalone checks for the diversity auto-phasing engine.
#
#   make -C test/diversity run
#
# test_modes_live drives the real engine, with the real RECEIVER and VFO
# types, and asserts that every reference mode actually produces a weight.
# It exists because "this mode silently never starts" is the failure this
# code has produced more than once - most recently the carrier reference,
# whose bin range was computed before the transform its carrier tracker
# needed, so it could never become valid and sat on "searching" for ever.
#
TOP     = ../..
#
# No audio-backend define is needed: receiver.h only conditions on
# PORTAUDIO/ALSA/PULSEAUDIO for its buffer types, none of which this test
# touches. Passing -DPIPEWIRE here made the test unbuildable on an ALSA or
# PortAudio machine, which rather defeats the point of a regression test.
#
CFLAGS  = -O2 -Wall -I$(TOP)/src -I$(TOP)/wdsp \
          `pkg-config --cflags gtk+-3.0 fftw3f`
LIBS    = `pkg-config --libs gtk+-3.0 fftw3f` -lm
SRC     = $(TOP)/src/diversity_auto.c $(TOP)/src/rade_correlator.c

all: test_modes_live test_window bench_cpu

test_modes_live: test_modes_live.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LIBS)

test_window: test_window.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LIBS)

bench_cpu: bench_cpu.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LIBS)

run: test_modes_live test_window
	./test_modes_live
	@echo
	./test_window

bench: bench_cpu
	./bench_cpu

clean:
	rm -f test_modes_live test_window bench_cpu
