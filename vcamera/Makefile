WARN := -Wall -Wextra -Werror
HOST_CFLAGS := -std=c11 -Og -g $(WARN)
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

host: vcamera-host

duo: vcamera

test: test_vcamera
	./test_vcamera

test_vcamera: test_vcamera.c lipsync.c lipsync.h mic.h mic_wav.c wav.c wav.h
	gcc $(HOST_CFLAGS) -o $@ test_vcamera.c lipsync.c mic_wav.c wav.c -lm

vcamera-host: main.c display.h display_terminal.c lipsync.c lipsync.h mic.h mic_wav.c wav.c wav.h
	gcc $(HOST_CFLAGS) -o $@ main.c display_terminal.c lipsync.c mic_wav.c wav.c -lm

vcamera: main.c display.h display_terminal.c lipsync.c lipsync.h mic.h mic_wav.c wav.c wav.h
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) -o $@ main.c display_terminal.c lipsync.c mic_wav.c wav.c $(LDFLAGS) -lm

run: vcamera-host
	$(if $(WAV),,$(error usage: make run WAV=path/to/voice.wav))
	./vcamera-host < $(WAV)

clean:
	rm -f test_vcamera vcamera-host vcamera

.PHONY: host duo test run clean
