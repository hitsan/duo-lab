#include <stdlib.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"

void RunVcameraTask(void *parameters)
{
    (void)parameters;
    if (!InitMic()) {
        printf("vcamera: cannot parse the embedded WAV\n");
        vTaskDelete(NULL);
    }

    float voice_level = 0.0f;
    while (true) {
        voice_level = SmoothVoiceLevel(voice_level, abs(ReadMicSample()) / 32768.0f);
        ShowMouth(CalcMouthOpenness(voice_level, CLOSED_DB, FULL_OPEN_DB));
    }
}
