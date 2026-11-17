WARN := -Wall -Wextra -Werror
INCLUDE := -Isrc/shared -Isrc/linux
HOST_CFLAGS := -std=c11 -Og -g $(WARN) $(INCLUDE) -Isrc/rtos
DUO_CFLAGS = $(CFLAGS) $(WARN) $(INCLUDE)
STB_INCLUDE := -isystem ../sobel/third_party
DUO ?= root@192.168.42.1
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

HEADERS := $(wildcard src/*/*.h)
TEST_SOURCES := test/test_vcamera.c src/rtos/adc_sample.c src/linux/config.c src/rtos/vowel.c src/rtos/lipsync.c src/linux/uvc_control.c
UVC_SOURCES := src/linux/reader.c src/linux/config.c src/linux/display_uvc.c src/linux/uvc_control.c

uvc: build/vcamera-uvc

calibrate: build/vcamera-calibrate

test: build/test_vcamera
	build/test_vcamera

image kernel:
	./build.sh $@

# Overwriting the running vcamera-uvc takes NCM down, so swap it in by rename and reboot
install: build/vcamera-uvc
	scp -O build/vcamera-uvc $(DUO):vcamera-uvc.new
	scp -O duo/usb-uvc-ncm.sh duo/vcamera.conf $(wildcard art/*.png art/*.jpg) $(DUO):
	scp -O duo/auto.sh $(DUO):/mnt/data/
	ssh $(DUO) 'mv vcamera-uvc.new vcamera-uvc'
	-ssh $(DUO) reboot

build:
	mkdir -p $@

build/test_vcamera: $(TEST_SOURCES) $(HEADERS) | build
	gcc $(HOST_CFLAGS) -o $@ $(TEST_SOURCES) -lm

# stb_image is third-party code, so it is built without our warnings
build/stb_impl.duo.o: src/linux/stb_impl.c | build
	$(check-duo)
	$(CC) $(CFLAGS) $(STB_INCLUDE) -c -o $@ $<

build/vcamera-uvc: $(UVC_SOURCES) $(HEADERS) build/stb_impl.duo.o | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) $(STB_INCLUDE) -o $@ $(UVC_SOURCES) build/stb_impl.duo.o $(LDFLAGS) -lm

build/vcamera-calibrate: tools/calibrate.c $(HEADERS) | build
	$(check-duo)
	$(CC) $(DUO_CFLAGS) -o $@ tools/calibrate.c $(LDFLAGS)

clean:
	rm -f build/test_vcamera build/vcamera-uvc build/vcamera-calibrate build/stb_impl.duo.o

.PHONY: uvc calibrate test image kernel install clean
