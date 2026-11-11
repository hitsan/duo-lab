#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mic.h"
#include "wav.h"

static uint8_t *wav_data;
static Wav wav;
static size_t read_index, frames_per_sample, samples_sent;
static struct timespec start_time;

bool InitMic(void)
{
    /* Grow the buffer until EOF so pipes work too */
    size_t size = 0, capacity = 0;
    for (;;) {
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 1 << 16;
            uint8_t *grown = realloc(wav_data, capacity);
            if (!grown) {
                return false;
            }
            wav_data = grown;
        }
        size_t got = fread(wav_data + size, 1, capacity - size, stdin);
        if (got == 0) {
            break;
        }
        size += got;
    }
    read_index = samples_sent = 0;
    if (!ParseWav(wav_data, size, &wav) || wav.sample_rate % MIC_SAMPLE_RATE || wav.frame_count == 0) {
        return false;
    }
    /* ponytail: decimation without LPF. Only the level matters, so aliasing is ignored */
    frames_per_sample = (size_t)(wav.sample_rate / MIC_SAMPLE_RATE);
    return true;
}

/* Like the ADC, deliver samples in MIC_SAMPLE_RATE real time, sleeping every 10ms */
static void waitUntilSampleDue(void)
{
    if (samples_sent == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }
    if (samples_sent % (MIC_SAMPLE_RATE / 100)) {
        return;
    }
    long long due_ns = start_time.tv_nsec + (long long)samples_sent * 1000000000LL / MIC_SAMPLE_RATE;
    struct timespec due = {start_time.tv_sec + due_ns / 1000000000LL, due_ns % 1000000000LL};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL);
}

/* Like a mic, never end: wrap to the start after the last sample */
int16_t ReadMicSample(void)
{
    if (read_index >= wav.frame_count) {
        read_index = 0;
    }
    waitUntilSampleDue();
    samples_sent++;
    int16_t sample = wav.samples[read_index * wav.channels];  /* first channel only */
    read_index += frames_per_sample;
    return sample;
}
