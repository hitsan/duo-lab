#include <string.h>
#include "wav.h"

#define RIFF_HEADER_SIZE 12
#define CHUNK_HEADER_SIZE 8
#define FMT_CHUNK_MIN_SIZE 16
#define FORMAT_PCM 1

static uint32_t readU32(const uint8_t *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, 4);
    return value;
}

static uint16_t readU16(const uint8_t *bytes)
{
    uint16_t value;
    memcpy(&value, bytes, 2);
    return value;
}

static bool isChunk(const uint8_t *chunk, const char *id)
{
    return memcmp(chunk, id, 4) == 0;
}

static bool readFmtChunk(const uint8_t *body, uint32_t body_size, Wav *wav)
{
    if (body_size < FMT_CHUNK_MIN_SIZE) {
        return false;
    }
    if (readU16(body) != FORMAT_PCM || readU16(body + 14) != 16) {
        return false;
    }
    wav->channels = readU16(body + 2);
    wav->sample_rate = (int)readU32(body + 4);
    return wav->channels >= 1 && wav->sample_rate >= 1;
}

bool ParseWav(const uint8_t *data, size_t size, Wav *wav)
{
    if (size < RIFF_HEADER_SIZE || !isChunk(data, "RIFF") || !isChunk(data + 8, "WAVE")) {
        return false;
    }

    bool found_fmt = false;
    size_t offset = RIFF_HEADER_SIZE;
    while (offset + CHUNK_HEADER_SIZE <= size) {
        const uint8_t *const chunk = data + offset;
        const uint8_t *const body = chunk + CHUNK_HEADER_SIZE;
        const uint32_t body_size = readU32(chunk + 4);
        if (body_size > size - offset - CHUNK_HEADER_SIZE) {
            return false;
        }

        if (isChunk(chunk, "fmt ")) {
            if (!readFmtChunk(body, body_size, wav)) {
                return false;
            }
            found_fmt = true;
        }
        if (isChunk(chunk, "data")) {
            if (!found_fmt) {
                return false;
            }
            wav->samples = (const int16_t *)body;
            wav->frame_count = body_size / (2u * wav->channels);
            return true;
        }
        /* Chunks are padded to an even size */
        offset += CHUNK_HEADER_SIZE + body_size + (body_size & 1);
    }
    return false;
}

bool StartWavPlayer(WavPlayer *player, const uint8_t *data, size_t size, int sample_rate)
{
    if (!ParseWav(data, size, &player->wav)) {
        return false;
    }
    if (player->wav.sample_rate % sample_rate != 0 || player->wav.frame_count == 0) {
        return false;
    }
    /* ponytail: decimation without LPF. Only the level matters, so aliasing is ignored */
    player->frames_per_sample = (size_t)(player->wav.sample_rate / sample_rate);
    player->frame_index = 0;
    return true;
}

int16_t ReadWavPlayerSample(WavPlayer *player)
{
    if (player->frame_index >= player->wav.frame_count) {
        player->frame_index = 0;
    }
    const int16_t sample = player->wav.samples[player->frame_index * player->wav.channels];
    player->frame_index += player->frames_per_sample;
    return sample;
}
