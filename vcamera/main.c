#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"

#define MAX_INIT_TRIES 5

/* MIC_SAMPLE_RATE / DISPLAY_FPS is not an integer; carry the remainder so frames don't drift */
static bool isFrameDue(void)
{
    static int frame_timer;
    frame_timer += DISPLAY_FPS;
    if (frame_timer < MIC_SAMPLE_RATE) {
        return false;
    }
    frame_timer -= MIC_SAMPLE_RATE;
    return true;
}

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
        const uint8_t openness = CalcMouthOpenness(voice_level, CLOSED_DB, FULL_OPEN_DB);
        if (isFrameDue()) {
            ShowMouth(openness);
        }
    }
}
