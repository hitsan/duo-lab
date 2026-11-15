WARN := -Wall -Wextra -Werror
HOST_CFLAGS := -std=c11 -Og -g $(WARN)
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

host: vcamera-host

duo: vcamera

reader: vcamera-reader

uvc: vcamera-uvc

calibrate: vcamera-calibrate

test: test_vcamera
	./test_vcamera

test_vcamera: test_vcamera.c adc_sample.c adc_sample.h vowel.c vowel.h lipsync.c lipsync.h mic.h mic_wav.c uvc_control.c uvc_control.h wav.c wav.h
	gcc $(HOST_CFLAGS) -o $@ test_vcamera.c adc_sample.c vowel.c lipsync.c mic_wav.c uvc_control.c wav.c -lm

vcamera-host: main.c display.h display_terminal.c lipsync.c lipsync.h mic.h mic_wav.c vowel.c vowel.h vowel_templates.h wav.c wav.h
	gcc $(HOST_CFLAGS) -o $@ main.c display_terminal.c lipsync.c mic_wav.c vowel.c wav.c -lm

vcamera: main.c display.h display_terminal.c lipsync.c lipsync.h mic.h mic_wav.c vowel.c vowel.h vowel_templates.h wav.c wav.h
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) -o $@ main.c display_terminal.c lipsync.c mic_wav.c vowel.c wav.c $(LDFLAGS) -lm

vcamera-reader: reader.c display.h display_terminal.c lipsync.c lipsync.h shared_openness.h vowel.h
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) -o $@ reader.c display_terminal.c lipsync.c $(LDFLAGS) -lm

vcamera-uvc: reader.c display.h display_uvc.c lipsync.c lipsync.h shared_openness.h uvc_control.c uvc_control.h vowel.h
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) -o $@ reader.c display_uvc.c lipsync.c uvc_control.c $(LDFLAGS) -lm

vcamera-calibrate: calibrate.c shared_openness.h vowel.h
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) -o $@ calibrate.c $(LDFLAGS)

run: vcamera-host
	$(if $(WAV),,$(error usage: make run WAV=path/to/voice.wav))
	./vcamera-host < $(WAV)

clean:
	rm -f test_vcamera vcamera-host vcamera vcamera-reader vcamera-uvc vcamera-calibrate

.PHONY: host duo reader uvc calibrate test run clean
