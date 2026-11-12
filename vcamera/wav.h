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

#endif
