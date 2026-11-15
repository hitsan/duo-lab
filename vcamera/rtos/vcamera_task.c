#include <math.h>
#include <stdlib.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"

/* Measured with the MAX9814 at 40dB: room noise sits near -30dB, speech at a normal distance peaks near -21dB */
#define MIC_CLOSED_DB -28.0f
#define MIC_FULL_OPEN_DB -21.0f
/* Holds the mouth open across the gaps between syllables; 80ms in lipsync.c flickered on camera */
#define MIC_HOLD_MS 250.0f
#define MIC_HOLD_RATE (1.0f - expf(-1000.0f / (MIC_SAMPLE_RATE * MIC_HOLD_MS)))

void RunVcameraTask(void *parameters)
{
    (void)parameters;
    if (!InitMic()) {
        printf("vcamera: cannot start the mic\n");
        vTaskDelete(NULL);
    }

    float voice_level = 0.0f;
    float held_level = 0.0f;
    while (true) {
        voice_level = SmoothVoiceLevel(voice_level, abs(ReadMicSample()) / 32768.0f);
        held_level = voice_level > held_level ? voice_level : held_level - MIC_HOLD_RATE * held_level;
        ShowMouth(CalcMouthOpenness(held_level, MIC_CLOSED_DB, MIC_FULL_OPEN_DB));
    }
}
