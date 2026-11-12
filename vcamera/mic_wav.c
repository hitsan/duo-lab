#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mic.h"
#include "wav.h"

#define NS_PER_SECOND 1000000000LL
#define SAMPLES_PER_SLEEP (MIC_SAMPLE_RATE / 100)

static uint8_t *file_data;
static size_t file_size;
static Wav wav;
static size_t frame_index;
static size_t frames_per_sample;
static size_t samples_sent;
static struct timespec start_time;

/* Read until EOF instead of using the file size so pipes work too */
static bool readWholeStdin(void)
{
    size_t capacity = 1 << 16;
    file_size = 0;
    while (true) {
        uint8_t *const grown = realloc(file_data, capacity);
        if (grown == NULL) {
            return false;
        }
        file_data = grown;
        file_size += fread(file_data + file_size, 1, capacity - file_size, stdin);
        if (file_size < capacity) {
            return true;
        }
        capacity *= 2;
    }
}

bool InitMic(void)
{
    if (!readWholeStdin()) {
        return false;
    }
    if (!ParseWav(file_data, file_size, &wav)) {
        return false;
    }
    if (wav.sample_rate % MIC_SAMPLE_RATE != 0 || wav.frame_count == 0) {
        return false;
    }
    /* ponytail: decimation without LPF. Only the level matters, so aliasing is ignored */
    frames_per_sample = (size_t)(wav.sample_rate / MIC_SAMPLE_RATE);
    frame_index = 0;
    samples_sent = 0;
    return true;
}

/* Like the ADC, deliver samples in MIC_SAMPLE_RATE real time, sleeping every 10ms */
static void waitUntilSampleDue(void)
{
    if (samples_sent == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }
    if (samples_sent % SAMPLES_PER_SLEEP != 0) {
        return;
    }
    const long long due_ns = start_time.tv_nsec + (long long)samples_sent * NS_PER_SECOND / MIC_SAMPLE_RATE;
    const struct timespec due = {start_time.tv_sec + due_ns / NS_PER_SECOND, due_ns % NS_PER_SECOND};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL);
}

/* Like a mic, never end: wrap to the start after the last sample */
int16_t ReadMicSample(void)
{
    if (frame_index >= wav.frame_count) {
        frame_index = 0;
    }
    waitUntilSampleDue();
    samples_sent++;
    const int16_t sample = wav.samples[frame_index * wav.channels];  /* first channel only */
    frame_index += frames_per_sample;
    return sample;
}
