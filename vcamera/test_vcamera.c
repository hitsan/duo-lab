#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lipsync.h"
#include "mic.h"
#include "wav.h"

#define OPENNESS(level) CalcMouthOpenness((level), -45.0f, -12.0f)

static void testOpennessSilenceIsZeroFullScaleIsMax(void)
{
    assert(OPENNESS(0.0f) == 0);
    assert(OPENNESS(1.0f) == 255);
}

static void testOpennessBelowClosedDbIsZero(void)
{
    assert(OPENNESS(0.001f) == 0);  /* -60dB */
    assert(OPENNESS(0.05f) > 0);    /* -26dB */
}

static void testVoiceLevelReaches63PercentAfterRiseTime(void)
{
    float level = 0.0f;
    for (int i = 0; i < 200; i++)  /* RISE_MS = 25ms at 8kHz */
        level = SmoothVoiceLevel(level, 1.0f);
    assert(level > 0.60f && level < 0.66f);
}

static void testVoiceLevelFallsSlowerThanItRises(void)
{
    float level = 0.0f;
    for (int i = 0; i < 8000; i++)
        level = SmoothVoiceLevel(level, 1.0f);
    for (int i = 0; i < 200; i++)  /* 25ms */
        level = SmoothVoiceLevel(level, 0.0f);
    assert(level > 0.37f);  /* does not fall to 1 - 0.63 within 25ms */
}

static void testMouthShapeSpansClosedToWidest(void)
{
    assert(SelectMouthShape(0, 5) == 0);
    assert(SelectMouthShape(255, 5) == 4);
    assert(SelectMouthShape(128, 5) == 2);
}

static size_t putChunkHeader(uint8_t *out, const char *id, uint32_t size)
{
    memcpy(out, id, 4);
    memcpy(out + 4, &size, 4);
    return 8;
}

/* If with_list, insert a LIST chunk between fmt and data */
static size_t buildWav(uint8_t *out, uint16_t bits, int with_list, uint32_t sample_rate, uint16_t channels)
{
    size_t written = 0;
    written += putChunkHeader(out + written, "RIFF", 0);
    memcpy(out + written, "WAVE", 4), written += 4;
    written += putChunkHeader(out + written, "fmt ", 16);
    uint16_t format_and_channels[] = {1, channels};  /* 1 = PCM */
    uint32_t byte_rate = sample_rate * 2 * channels;
    uint16_t block_align = 2 * channels;
    memcpy(out + written, format_and_channels, 4), written += 4;
    memcpy(out + written, &sample_rate, 4), written += 4;
    memcpy(out + written, &byte_rate, 4), written += 4;
    memcpy(out + written, &block_align, 2), written += 2;
    memcpy(out + written, &bits, 2), written += 2;
    if (with_list) {
        written += putChunkHeader(out + written, "LIST", 4);
        memcpy(out + written, "INFO", 4), written += 4;
    }
    int16_t samples[] = {100, -200, 300, -400, 500, -600};
    written += putChunkHeader(out + written, "data", sizeof samples);
    memcpy(out + written, samples, sizeof samples), written += sizeof samples;
    return written;
}

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, size, f);
    fclose(f);
}

static void feedWavToStdin(uint32_t sample_rate, uint16_t channels)
{
    uint8_t wav[128];
    writeFile("test.wav", wav, buildWav(wav, 16, 0, sample_rate, channels));
    assert(freopen("test.wav", "rb", stdin));
    remove("test.wav");
}

/* Check the mic yields expected in order, then wraps to the start */
static void assertMicReads(uint32_t sample_rate, uint16_t channels, const int16_t *expected, int count)
{
    feedWavToStdin(sample_rate, channels);
    assert(InitMic());
    for (int i = 0; i < count; i++)
        assert(ReadMicSample() == expected[i]);
    assert(ReadMicSample() == expected[0]);
}

static void testMicReadsSamplesThenWraps(void)
{
    assertMicReads(8000, 1, (int16_t[]){100, -200, 300, -400, 500, -600}, 6);
}

static void testMicDecimates16kTo8k(void)
{
    assertMicReads(16000, 1, (int16_t[]){100, 300, 500}, 3);
}

static void testMicUsesFirstChannel(void)
{
    assertMicReads(8000, 2, (int16_t[]){100, 300, 500}, 3);
}

static void testMicRejectsRateNotMultipleOf8k(void)
{
    feedWavToStdin(11025, 1);
    assert(!InitMic());
}

static void testWavReads16bitMono(void)
{
    uint8_t data[128];
    size_t size = buildWav(data, 16, 0, 8000, 1);
    Wav wav;
    assert(ParseWav(data, size, &wav));
    assert(wav.sample_rate == 8000);
    assert(wav.channels == 1);
    assert(wav.frame_count == 6);
    assert(wav.samples[1] == -200);
}

static void testWavRejectsNon16bit(void)
{
    uint8_t data[128];
    size_t size = buildWav(data, 8, 0, 8000, 1);
    Wav wav;
    assert(!ParseWav(data, size, &wav));
}

static void testWavSkipsUnknownChunk(void)
{
    uint8_t data[128];
    size_t size = buildWav(data, 16, 1, 8000, 1);
    Wav wav;
    assert(ParseWav(data, size, &wav));
    assert(wav.frame_count == 6);
    assert(wav.samples[2] == 300);
}

int main(void)
{
    testMicReadsSamplesThenWraps();
    testMicDecimates16kTo8k();
    testMicUsesFirstChannel();
    testMicRejectsRateNotMultipleOf8k();
    testWavReads16bitMono();
    testWavRejectsNon16bit();
    testWavSkipsUnknownChunk();
    testOpennessSilenceIsZeroFullScaleIsMax();
    testOpennessBelowClosedDbIsZero();
    testVoiceLevelReaches63PercentAfterRiseTime();
    testVoiceLevelFallsSlowerThanItRises();
    testMouthShapeSpansClosedToWidest();
    puts("ok");
    return 0;
}
