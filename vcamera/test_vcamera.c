#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "adc_sample.h"
#include "lipsync.h"
#include "mic.h"
#include "uvc_control.h"
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

static float smoothRepeatedly(float level, float sample, int count)
{
    while (count > 0) {
        level = SmoothVoiceLevel(level, sample);
        count--;
    }
    return level;
}

static void testVoiceLevelReaches63PercentAfterRiseTime(void)
{
    const float level = smoothRepeatedly(0.0f, 1.0f, 200);  /* RISE_MS = 25ms at 8kHz */
    assert(level > 0.60f && level < 0.66f);
}

static void testVoiceLevelFallsSlowerThanItRises(void)
{
    const float loud_level = smoothRepeatedly(0.0f, 1.0f, 8000);
    const float level = smoothRepeatedly(loud_level, 0.0f, 200);  /* 25ms */
    assert(level > 0.37f);  /* does not fall to 1 - 0.63 within 25ms */
}

static void testMouthShapeSpansClosedToWidest(void)
{
    assert(SelectMouthShape(0, 5) == 0);
    assert(SelectMouthShape(255, 5) == 4);
    assert(SelectMouthShape(128, 5) == 2);
}

static uint8_t *putBytes(uint8_t *out, const void *bytes, size_t size)
{
    memcpy(out, bytes, size);
    return out + size;
}

static uint8_t *putChunkHeader(uint8_t *out, const char *id, uint32_t size)
{
    out = putBytes(out, id, 4);
    return putBytes(out, &size, 4);
}

/* If with_list, insert a LIST chunk between fmt and data */
static size_t buildWav(uint8_t *start, uint16_t bits, bool with_list, uint32_t sample_rate, uint16_t channels)
{
    const uint16_t format = 1;  /* PCM */
    const uint32_t byte_rate = sample_rate * 2 * channels;
    const uint16_t block_align = 2 * channels;
    const int16_t samples[] = {100, -200, 300, -400, 500, -600};

    uint8_t *out = putChunkHeader(start, "RIFF", 0);
    out = putBytes(out, "WAVE", 4);
    out = putChunkHeader(out, "fmt ", 16);
    out = putBytes(out, &format, 2);
    out = putBytes(out, &channels, 2);
    out = putBytes(out, &sample_rate, 4);
    out = putBytes(out, &byte_rate, 4);
    out = putBytes(out, &block_align, 2);
    out = putBytes(out, &bits, 2);
    if (with_list) {
        out = putChunkHeader(out, "LIST", 4);
        out = putBytes(out, "INFO", 4);
    }
    out = putChunkHeader(out, "data", sizeof samples);
    out = putBytes(out, samples, sizeof samples);
    return (size_t)(out - start);
}

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
    FILE *const file = fopen(path, "wb");
    fwrite(data, 1, size, file);
    fclose(file);
}

static void feedWavToStdin(uint32_t sample_rate, uint16_t channels)
{
    uint8_t wav[128];
    writeFile("test.wav", wav, buildWav(wav, 16, false, sample_rate, channels));
    assert(freopen("test.wav", "rb", stdin));
    remove("test.wav");
}

/* Check the mic yields expected in order, then wraps to the start */
static void assertMicReads(uint32_t sample_rate, uint16_t channels, const int16_t *expected, int count)
{
    feedWavToStdin(sample_rate, channels);
    assert(InitMic());
    int index = 0;
    while (index < count) {
        assert(ReadMicSample() == expected[index]);
        index++;
    }
    assert(ReadMicSample() == expected[0]);
}

static void testMicReadsSamplesThenWraps(void)
{
    assertMicReads(8000, 1, (const int16_t[]){100, -200, 300, -400, 500, -600}, 6);
}

static void testMicRejectsRateNotMultipleOf8k(void)
{
    feedWavToStdin(11025, 1);
    assert(!InitMic());
}

/* Check the player yields expected in order at 8kHz, then wraps to the start */
static void assertPlayerReads(uint32_t sample_rate, uint16_t channels, const int16_t *expected, int count)
{
    uint8_t data[128];
    const size_t size = buildWav(data, 16, false, sample_rate, channels);
    WavPlayer player;
    assert(StartWavPlayer(&player, data, size, 8000));
    int index = 0;
    while (index < count) {
        assert(ReadWavPlayerSample(&player) == expected[index]);
        index++;
    }
    assert(ReadWavPlayerSample(&player) == expected[0]);
}

static void testPlayerReadsSamplesThenWraps(void)
{
    assertPlayerReads(8000, 1, (const int16_t[]){100, -200, 300, -400, 500, -600}, 6);
}

static void testPlayerDecimates16kTo8k(void)
{
    assertPlayerReads(16000, 1, (const int16_t[]){100, 300, 500}, 3);
}

static void testPlayerUsesFirstChannel(void)
{
    assertPlayerReads(8000, 2, (const int16_t[]){100, 300, 500}, 3);
}

static void testPlayerRejectsRateNotMultipleOf8k(void)
{
    uint8_t data[128];
    const size_t size = buildWav(data, 16, false, 11025, 1);
    WavPlayer player;
    assert(!StartWavPlayer(&player, data, size, 8000));
}

static void testWavReads16bitMono(void)
{
    uint8_t data[128];
    const size_t size = buildWav(data, 16, false, 8000, 1);
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
    const size_t size = buildWav(data, 8, false, 8000, 1);
    Wav wav;
    assert(!ParseWav(data, size, &wav));
}

static void testWavSkipsUnknownChunk(void)
{
    uint8_t data[128];
    const size_t size = buildWav(data, 16, true, 8000, 1);
    Wav wav;
    assert(ParseWav(data, size, &wav));
    assert(wav.frame_count == 6);
    assert(wav.samples[2] == 300);
}

#define STREAMING_INTERFACE 3
#define CLASS_GET 0xa1
#define CLASS_SET 0x21
#define PROBE_CONTROL 1
#define COMMIT_CONTROL 2

static UvcResponse answerStreaming(uint8_t request_type, uint8_t request, int selector, uint16_t length)
{
    const UvcRequest uvc_request = {
        .request_type = request_type,
        .request = request,
        .value = (uint16_t)(selector << 8),
        .index = STREAMING_INTERFACE,
        .length = length,
    };
    UvcResponse response;
    AnswerUvcRequest(&uvc_request, STREAMING_INTERFACE, &response);
    return response;
}

static uint32_t readLe32(const uint8_t *bytes)
{
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void testProbeGetCurAnswersFormat(void)
{
    const UvcResponse response = answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 26);
    assert(response.length == 26);
    assert(response.data[2] == 1);                      /* bFormatIndex */
    assert(response.data[3] == 1);                      /* bFrameIndex */
    assert(readLe32(response.data + 4) == 333333);      /* dwFrameInterval: 30fps */
    assert(readLe32(response.data + 18) == 320 * 240 * 2);
    assert(readLe32(response.data + 22) == 1024);
}

static void testProbeMinMaxDefAnswerSameFormat(void)
{
    const UvcResponse current = answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 26);
    const uint8_t requests[] = {0x82, 0x83, 0x87};
    int index = 0;
    while (index < 3) {
        const UvcResponse response = answerStreaming(CLASS_GET, requests[index], COMMIT_CONTROL, 26);
        assert(response.length == 26);
        assert(memcmp(response.data, current.data, 26) == 0);
        index++;
    }
}

static void testProbeInfoAndLength(void)
{
    const UvcResponse info = answerStreaming(CLASS_GET, 0x86, PROBE_CONTROL, 1);
    assert(info.length == 1);
    assert(info.data[0] == 0x03);
    const UvcResponse length = answerStreaming(CLASS_GET, 0x85, PROBE_CONTROL, 2);
    assert(length.length == 2);
    assert(length.data[0] == 26 && length.data[1] == 0);
}

static void testProbeSetCurAcceptsData(void)
{
    assert(answerStreaming(CLASS_SET, 0x01, COMMIT_CONTROL, 26).length == 26);
}

static void testProbeShortRequestIsTruncated(void)
{
    assert(answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 8).length == 8);
}

static void testRequestsOutsideStreamingInterfaceStall(void)
{
    const UvcRequest unit_request = {.request_type = CLASS_GET, .request = 0x86, .value = 0x0200, .index = 0x0202, .length = 1};
    const UvcRequest control_interface_request = {.request_type = CLASS_GET, .request = 0x81, .value = 0x0200, .index = 2, .length = 1};
    UvcResponse response;
    AnswerUvcRequest(&unit_request, STREAMING_INTERFACE, &response);
    assert(response.length < 0);
    AnswerUvcRequest(&control_interface_request, STREAMING_INTERFACE, &response);
    assert(response.length < 0);
}

static void testMouthFrameIsSolidYuyvBrighterWhenOpen(void)
{
    static uint8_t closed[UVC_FRAME_BYTES];
    static uint8_t open[UVC_FRAME_BYTES];
    FillMouthFrame(closed, 0, 5);
    FillMouthFrame(open, 4, 5);
    assert(closed[0] < open[0]);
    assert(closed[1] == 128 && closed[3] == 128);       /* neutral chroma */
    assert(open[UVC_FRAME_BYTES - 2] == open[0]);
}

static void testAdcAtBiasIsSilence(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1500) == 0);
}

static void testAdcAboveBiasIsScaledTo16bit(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1600) == 1600);
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1400) == -1600);
}

static void testAdcBiasFollowsDrift(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    int count = 0;
    while (count < 8000) {
        ConvertAdcSample(&bias, 1550);
        count++;
    }
    assert(ConvertAdcSample(&bias, 1550) == 0);
}

static void testAdcClampsToInt16(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 0);
    assert(ConvertAdcSample(&bias, 4095) == 32767);
    StartAdcBias(&bias, 4095);
    assert(ConvertAdcSample(&bias, 0) == -32768);
}

int main(void)
{
    testAdcAtBiasIsSilence();
    testAdcAboveBiasIsScaledTo16bit();
    testAdcBiasFollowsDrift();
    testAdcClampsToInt16();
    testMicReadsSamplesThenWraps();
    testMicRejectsRateNotMultipleOf8k();
    testPlayerReadsSamplesThenWraps();
    testPlayerDecimates16kTo8k();
    testPlayerUsesFirstChannel();
    testPlayerRejectsRateNotMultipleOf8k();
    testWavReads16bitMono();
    testWavRejectsNon16bit();
    testWavSkipsUnknownChunk();
    testOpennessSilenceIsZeroFullScaleIsMax();
    testOpennessBelowClosedDbIsZero();
    testVoiceLevelReaches63PercentAfterRiseTime();
    testVoiceLevelFallsSlowerThanItRises();
    testMouthShapeSpansClosedToWidest();
    testProbeGetCurAnswersFormat();
    testProbeMinMaxDefAnswerSameFormat();
    testProbeInfoAndLength();
    testProbeSetCurAcceptsData();
    testProbeShortRequestIsTruncated();
    testRequestsOutsideStreamingInterfaceStall();
    testMouthFrameIsSolidYuyvBrighterWhenOpen();
    puts("ok");
    return 0;
}
