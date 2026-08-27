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
CFLAGS  = -O2 -Wall -I$(TOP)/src -I$(TOP)/wdsp -DPIPEWIRE \
          `pkg-config --cflags gtk+-3.0 fftw3f libpipewire-0.3`
LIBS    = `pkg-config --libs gtk+-3.0 fftw3f` -lm
SRC     = $(TOP)/src/diversity_auto.c $(TOP)/src/rade_correlator.c

all: test_modes_live

test_modes_live: test_modes_live.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LIBS)

run: test_modes_live
	./test_modes_live

clean:
	rm -f test_modes_live
