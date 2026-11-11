#include <string.h>
#include "wav.h"

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

bool ParseWav(const uint8_t *data, size_t size, Wav *wav)
{
    if (size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) {
        return false;
    }

    bool found_fmt = false;
    for (size_t offset = 12; offset + 8 <= size;) {
        const uint8_t *chunk = data + offset;
        uint32_t chunk_size = readU32(chunk + 4);
        if (chunk_size > size - offset - 8) {
            return false;
        }

        if (!memcmp(chunk, "fmt ", 4)) {
            if (chunk_size < 16 || readU16(chunk + 8) != 1 || readU16(chunk + 22) != 16) {
                return false;
            }
            wav->channels = readU16(chunk + 10);
            wav->sample_rate = (int)readU32(chunk + 12);
            if (wav->channels < 1 || wav->sample_rate < 1) {
                return false;
            }
            found_fmt = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (!found_fmt) {
                return false;
            }
            wav->samples = (const int16_t *)(chunk + 8);
            wav->frame_count = chunk_size / (2u * wav->channels);
            return true;
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }
    return false;
}
