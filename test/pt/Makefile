# Tests for the CPU port of PT SkyWalker541. Desktop only; never shipped.
#
#   make check       build everything, then both sweeps
#   make check-arm   the fast sweep with an ARM (Cortex-A53) build, under qemu
#
# Needs: gcc, EGL and GLES 2 headers (Mesa), python3; for check-arm,
# aarch64-linux-gnu-gcc and qemu-user.

PT      := ../../shaders/pt
PTFLAGS := -O3 -fno-trapping-math -fno-math-errno -ffp-contract=off
CFLAGS  := -std=gnu11 -Wall -Wextra
SRC     := $(PT)/pt_shader.c
HDRS    := $(PT)/pt_shader.h $(PT)/pt_math.h ppm.h

all: glref ptc ptref ptref_flat frames refs

glref: glref.c ppm.h
	$(CC) -O2 $(CFLAGS) -o $@ glref.c -lEGL -lGLESv2

ptc: ptc.c $(SRC) $(HDRS)
	$(CC) $(PTFLAGS) $(CFLAGS) -o $@ ptc.c $(SRC) -lm -lpthread

ptref: ptref.c pt_reference.c pt_reference.h $(SRC) $(HDRS)
	$(CC) $(PTFLAGS) $(CFLAGS) -o $@ ptref.c pt_reference.c $(SRC) -lm

ptref_flat: ptref.c pt_reference.c pt_reference.h $(SRC) $(HDRS)
	$(CC) $(PTFLAGS) $(CFLAGS) -DPT_TEST_FLAT_GRAIN -o $@ ptref.c pt_reference.c $(SRC) -lm

ptc_a53: ptc.c $(SRC) $(HDRS)
	aarch64-linux-gnu-gcc $(PTFLAGS) -mcpu=cortex-a53 $(CFLAGS) -static -o $@ ptc.c $(SRC) -lm -lpthread

frames: make_frames.py
	python3 make_frames.py
	touch frames

refs: mkref.py $(PT)/PT_SkyWalker541.glsl
	python3 mkref.py
	touch refs

check: all
	python3 mkref.py --check
	sh sweep.sh gl
	sh sweep.sh fast

check-arm: all ptc_a53
	PTC="qemu-aarch64 ./ptc_a53" sh sweep.sh fast

clean:
	rm -f glref ptc ptref ptref_flat ptc_a53 frames refs *.ppm *.glsl *.log

.PHONY: all check check-arm clean
