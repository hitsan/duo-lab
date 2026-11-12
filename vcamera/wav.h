#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int sample_rate;
    int channels;
    const int16_t *samples;  /* channels interleaved */
    size_t frame_count;      /* samples per channel */
} Wav;

/* true on success. Accepts 16bit PCM only. data must be 2-byte aligned */
bool ParseWav(const uint8_t *data, size_t size, Wav *wav);

typedef struct {
    Wav wav;
    size_t frame_index;
    size_t frames_per_sample;
} WavPlayer;

/* true on success. The WAV rate must be a multiple of sample_rate */
bool StartWavPlayer(WavPlayer *player, const uint8_t *data, size_t size, int sample_rate);
/* Returns the next sample at sample_rate, first channel only. Wraps to the start after the last */
int16_t ReadWavPlayerSample(WavPlayer *player);

#endif
