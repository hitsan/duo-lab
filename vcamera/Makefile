WARN := -Wall -Wextra -Werror
INCLUDE := -Isrc
HOST_CFLAGS := -std=c11 -Og -g $(WARN) $(INCLUDE)
DUO_CFLAGS = $(CFLAGS) $(WARN) $(INCLUDE)
STB_INCLUDE := -isystem ../sobel/third_party
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

HEADERS := $(wildcard src/*.h)
TEST_SOURCES := test/test_vcamera.c src/adc_sample.c src/config.c src/vowel.c src/lipsync.c src/mic_wav.c src/uvc_control.c src/wav.c
PLAYER_SOURCES := src/main.c src/display_terminal.c src/lipsync.c src/mic_wav.c src/vowel.c src/wav.c
READER_SOURCES := src/reader.c src/display_terminal.c src/lipsync.c
UVC_SOURCES := src/reader.c src/config.c src/display_uvc.c src/lipsync.c src/uvc_control.c

host: build/vcamera-host

duo: build/vcamera

reader: build/vcamera-reader

uvc: build/vcamera-uvc

calibrate: build/vcamera-calibrate

test: build/test_vcamera
	build/test_vcamera

build:
	mkdir -p $@

build/test_vcamera: $(TEST_SOURCES) $(HEADERS) | build
	gcc $(HOST_CFLAGS) -o $@ $(TEST_SOURCES) -lm

build/vcamera-host: $(PLAYER_SOURCES) $(HEADERS) | build
	gcc $(HOST_CFLAGS) -o $@ $(PLAYER_SOURCES) -lm

build/vcamera: $(PLAYER_SOURCES) $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ $(PLAYER_SOURCES) $(LDFLAGS) -lm

build/vcamera-reader: $(READER_SOURCES) $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ $(READER_SOURCES) $(LDFLAGS) -lm

# stb_image is third-party code, so it is built without our warnings
build/stb_impl.duo.o: src/stb_impl.c | build
	$(check-duo)
	$(CC) $(CFLAGS) $(STB_INCLUDE) -c -o $@ $<

build/vcamera-uvc: $(UVC_SOURCES) $(HEADERS) build/stb_impl.duo.o | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) $(STB_INCLUDE) -o $@ $(UVC_SOURCES) build/stb_impl.duo.o $(LDFLAGS) -lm

build/vcamera-calibrate: src/calibrate.c $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ src/calibrate.c $(LDFLAGS)

run: build/vcamera-host
	$(if $(WAV),,$(error usage: make run WAV=path/to/voice.wav))
	build/vcamera-host < $(WAV)

clean:
	rm -f build/test_vcamera build/vcamera-host build/vcamera build/vcamera-reader build/vcamera-uvc build/vcamera-calibrate build/stb_impl.duo.o

.PHONY: host duo reader uvc calibrate test run clean
