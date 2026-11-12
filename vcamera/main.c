#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"

#define MAX_INIT_TRIES 5
#define CLOSED_DB -45.0f
#define FULL_OPEN_DB -12.0f

int main(void)
{
    int attempt = 1;
    while (!InitMic()) {
        fprintf(stderr, "cannot init mic (%d/%d)\n", attempt, MAX_INIT_TRIES);
        if (attempt == MAX_INIT_TRIES) {
            return 1;
        }
        attempt++;
        sleep(1);
    }

    float voice_level = 0.0f;
    while (true) {
        voice_level = SmoothVoiceLevel(voice_level, abs(ReadMicSample()) / 32768.0f);
        ShowMouth(CalcMouthOpenness(voice_level, CLOSED_DB, FULL_OPEN_DB));
    }
}
