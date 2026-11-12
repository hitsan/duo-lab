#include "mic.h"
#include "wav.h"
#include "FreeRTOS.h"
#include "task.h"

#define SAMPLES_PER_TICK (MIC_SAMPLE_RATE / configTICK_RATE_HZ)

_Static_assert(MIC_SAMPLE_RATE % configTICK_RATE_HZ == 0, "tick must split the sample rate evenly");

extern const uint8_t embedded_wav_start[];
extern const uint8_t embedded_wav_end[];

static WavPlayer player;
static size_t samples_sent;
static TickType_t last_wake_time;

bool InitMic(void)
{
    samples_sent = 0;
    return StartWavPlayer(&player, embedded_wav_start, (size_t)(embedded_wav_end - embedded_wav_start), MIC_SAMPLE_RATE);
}

/* ponytail: real time only on average; samples come in bursts of SAMPLES_PER_TICK every tick (5ms at 200Hz). Pace with a hardware timer if jitter matters */
int16_t ReadMicSample(void)
{
    if (samples_sent == 0) {
        last_wake_time = xTaskGetTickCount();
    } else if (samples_sent % SAMPLES_PER_TICK == 0) {
        vTaskDelayUntil(&last_wake_time, 1);
    }
    samples_sent++;
    return ReadWavPlayerSample(&player);
}
