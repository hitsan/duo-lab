WARN := -Wall -Wextra -Werror
INCLUDE := -Isrc -Iart
HOST_CFLAGS := -std=c11 -Og -g $(WARN) $(INCLUDE)
DUO_CFLAGS = $(CFLAGS) $(WARN) $(INCLUDE)
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

HEADERS := $(wildcard src/*.h)
TEST_SOURCES := test/test_vcamera.c src/adc_sample.c src/vowel.c src/lipsync.c src/mic_wav.c src/uvc_control.c src/wav.c
PLAYER_SOURCES := src/main.c src/display_terminal.c src/lipsync.c src/mic_wav.c src/vowel.c src/wav.c
READER_SOURCES := src/reader.c src/display_terminal.c src/lipsync.c
UVC_SOURCES := src/reader.c src/display_uvc.c src/lipsync.c src/uvc_control.c

host: build/vcamera-host

duo: build/vcamera

reader: build/vcamera-reader

uvc: build/vcamera-uvc

calibrate: build/vcamera-calibrate

test: build/test_vcamera
	build/test_vcamera

build:
	mkdir -p $@

build/test_vcamera: $(TEST_SOURCES) $(HEADERS) art/face_art.h | build
	gcc $(HOST_CFLAGS) -o $@ $(TEST_SOURCES) -lm

build/vcamera-host: $(PLAYER_SOURCES) $(HEADERS) | build
	gcc $(HOST_CFLAGS) -o $@ $(PLAYER_SOURCES) -lm

build/vcamera: $(PLAYER_SOURCES) $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ $(PLAYER_SOURCES) $(LDFLAGS) -lm

build/vcamera-reader: $(READER_SOURCES) $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ $(READER_SOURCES) $(LDFLAGS) -lm

build/vcamera-uvc: $(UVC_SOURCES) $(HEADERS) art/face_art.h | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ $(UVC_SOURCES) $(LDFLAGS) -lm

build/vcamera-calibrate: src/calibrate.c $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ src/calibrate.c $(LDFLAGS)

# The drawing stays out of git: put it at art/face.png or pass ART=path. Needs Pillow, taken from nix
ART ?= art/face.png
face_art: art/face_art.h

art/face_art.h: $(ART) art/convert.py
	nix shell --impure --expr 'with import <nixpkgs> {}; python3.withPackages (p: [ p.pillow ])' -c python3 art/convert.py $(ART) > $@

run: build/vcamera-host
	$(if $(WAV),,$(error usage: make run WAV=path/to/voice.wav))
	build/vcamera-host < $(WAV)

clean:
	rm -f build/test_vcamera build/vcamera-host build/vcamera build/vcamera-reader build/vcamera-uvc build/vcamera-calibrate

.PHONY: host duo reader uvc calibrate test face_art run clean
