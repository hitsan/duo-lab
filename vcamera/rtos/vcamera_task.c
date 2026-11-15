#include <stdlib.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"

/* Measured with the MAX9814 at 40dB: room noise sits near -30dB, speech at a normal distance peaks near -21dB */
#define MIC_CLOSED_DB -27.0f
#define MIC_FULL_OPEN_DB -18.0f

void RunVcameraTask(void *parameters)
{
    (void)parameters;
    if (!InitMic()) {
        printf("vcamera: cannot start the mic\n");
        vTaskDelete(NULL);
    }

    float voice_level = 0.0f;
    while (true) {
        voice_level = SmoothVoiceLevel(voice_level, abs(ReadMicSample()) / 32768.0f);
        ShowMouth(CalcMouthOpenness(voice_level, MIC_CLOSED_DB, MIC_FULL_OPEN_DB));
    }
}
