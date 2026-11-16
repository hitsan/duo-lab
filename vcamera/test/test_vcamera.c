#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "adc_sample.h"
#include "face_art.h"
#include <math.h>
#include "vowel.h"
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

static uint8_t lumaAt(const uint8_t *frame, int x, int y)
{
    return frame[(y * UVC_FRAME_WIDTH + x) * 2];
}

static uint8_t closed_frame[UVC_FRAME_BYTES];
static uint8_t half_frame[UVC_FRAME_BYTES];
static uint8_t open_frame[UVC_FRAME_BYTES];

#define ART_LEFT ((UVC_FRAME_WIDTH - FACE_ART_SIZE) / 2)
#define MOUTH_CENTER_X (ART_LEFT + FACE_ART_MOUTH_LEFT + FACE_ART_MOUTH_WIDTH / 2)
#define MOUTH_CENTER_Y (FACE_ART_MOUTH_TOP + FACE_ART_MOUTH_HEIGHT / 2)
#define AROUND_MOUTH 60

static void fillTestFrames(void)
{
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    FillMouthFrame(half_frame, CalcMouthSize(128, MOUTH_A));
    FillMouthFrame(open_frame, CalcMouthSize(255, MOUTH_A));
}

static void testMouthSizeSpansClosedLineToDrawnSize(void)
{
    const MouthSize closed = CalcMouthSize(0, MOUTH_A);
    const MouthSize open = CalcMouthSize(255, MOUTH_A);
    assert(closed.width == FACE_ART_MOUTH_WIDTH && closed.height == 3);
    assert(open.width == FACE_ART_MOUTH_WIDTH && open.height == FACE_ART_MOUTH_HEIGHT);
}

static void testEasedMouthMovesPartWayToTarget(void)
{
    const MouthSize current = {.width = 10, .height = 10};
    const MouthSize target = {.width = 50, .height = 3};
    const MouthSize eased = EaseMouthSize(current, target);
    assert(eased.width > 10 && eased.width < 50);
    assert(eased.height < 10 && eased.height > 3);
}

static void testEasedMouthReachesTarget(void)
{
    MouthSize size = {.width = 10, .height = 10};
    const MouthSize target = {.width = 50, .height = 3};
    int frame = 0;
    while (frame < 15) {
        size = EaseMouthSize(size, target);
        frame++;
    }
    assert(size.width == target.width && size.height == target.height);
}

static bool artInk(int x, int y)
{
    return FACE_ART[y * (FACE_ART_SIZE / 8) + x / 8] & (0x80 >> (x % 8));
}

static bool isInk(const uint8_t *frame, int x, int y)
{
    return lumaAt(frame, x, y) < 128;
}

static int countMouthInk(const uint8_t *frame)
{
    int count = 0;
    int y = MOUTH_CENTER_Y - AROUND_MOUTH;
    while (y < MOUTH_CENTER_Y + AROUND_MOUTH) {
        int x = MOUTH_CENTER_X - AROUND_MOUTH;
        while (x < MOUTH_CENTER_X + AROUND_MOUTH) {
            count += isInk(frame, x, y) && !artInk(x - ART_LEFT, y);
            x++;
        }
        y++;
    }
    return count;
}

static int mouthInkWidth(const uint8_t *frame)
{
    int left = MOUTH_CENTER_X;
    int right = MOUTH_CENTER_X;
    int y = MOUTH_CENTER_Y - AROUND_MOUTH;
    while (y < MOUTH_CENTER_Y + AROUND_MOUTH) {
        int x = MOUTH_CENTER_X - AROUND_MOUTH;
        while (x < MOUTH_CENTER_X + AROUND_MOUTH) {
            if (isInk(frame, x, y) && !artInk(x - ART_LEFT, y)) {
                left = x < left ? x : left;
                right = x > right ? x : right;
            }
            x++;
        }
        y++;
    }
    return right - left;
}

static void testArtFaceIsCopiedOntoPaper(void)
{
    fillTestFrames();
    assert(!isInk(open_frame, 0, 0));
    int y = 0;
    while (y < FACE_ART_SIZE) {
        int x = 0;
        while (x < FACE_ART_SIZE) {
            if (artInk(x, y)) {
                assert(isInk(closed_frame, ART_LEFT + x, y));
            }
            x++;
        }
        y++;
    }
}

static void testClosedMouthIsAThinLine(void)
{
    fillTestFrames();
    const int closed = countMouthInk(closed_frame);
    assert(closed > 0);
    assert(closed * 4 < countMouthInk(open_frame));
}

static void testMouthGrowsWithOpenness(void)
{
    fillTestFrames();
    assert(countMouthInk(closed_frame) < countMouthInk(half_frame));
    assert(countMouthInk(half_frame) < countMouthInk(open_frame));
}

static void testMouthShapeChangesWidth(void)
{
    static uint8_t spread[UVC_FRAME_BYTES];
    static uint8_t rounded[UVC_FRAME_BYTES];
    fillTestFrames();
    FillMouthFrame(spread, CalcMouthSize(255, MOUTH_I));
    FillMouthFrame(rounded, CalcMouthSize(255, MOUTH_U));
    assert(mouthInkWidth(spread) > mouthInkWidth(open_frame));
    assert(mouthInkWidth(rounded) < mouthInkWidth(open_frame));
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

/* Harmonics of f0 shaped by two formant resonances, like a sustained vowel */
static void synthesizeVowel(int16_t *frame, float f0, float f1, float f2, float amplitude)
{
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        float value = 0.0f;
        float harmonic = f0;
        while (harmonic < 4000.0f) {
            const float d1 = (harmonic - f1) / 100.0f;
            const float d2 = (harmonic - f2) / 120.0f;
            const float gain = (1.0f / (1.0f + d1 * d1) + 0.7f / (1.0f + d2 * d2)) * f0 / harmonic;
            value += gain * sinf(2.0f * 3.14159265f * harmonic * n / 8000.0f);
            harmonic += f0;
        }
        frame[n] = (int16_t)(amplitude * value);
        n++;
    }
}

/* Japanese adult formants (F1, F2) */
static const float FORMANTS[5][2] = {
    {750.0f, 1200.0f},  /* a */
    {300.0f, 2300.0f},  /* i */
    {330.0f, 1300.0f},  /* u */
    {500.0f, 1900.0f},  /* e */
    {500.0f, 850.0f},   /* o */
};

static Vowel classifySynthetic(const VowelFeature templates[VOWEL_COUNT], int vowel, float f0, float amplitude)
{
    int16_t frame[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(frame, f0, FORMANTS[vowel][0], FORMANTS[vowel][1], amplitude);
    VowelFeature feature;
    ExtractVowelFeature(frame, &feature);
    return ClassifyVowel(&feature, templates);
}

static void learnSyntheticTemplates(VowelFeature templates[VOWEL_COUNT])
{
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        int16_t frame[VOWEL_FRAME_SAMPLES];
        synthesizeVowel(frame, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 3000.0f);
        ExtractVowelFeature(frame, &templates[vowel]);
        vowel++;
    }
}

static int loudestBand(const VowelFeature *feature)
{
    int loudest = 0;
    int band = 1;
    while (band < VOWEL_BANDS) {
        if (feature->bands[band] > feature->bands[loudest]) {
            loudest = band;
        }
        band++;
    }
    return loudest;
}

static void testVowelFeaturePeaksAtToneBand(void)
{
    int16_t low[VOWEL_FRAME_SAMPLES];
    int16_t high[VOWEL_FRAME_SAMPLES];
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        low[n] = (int16_t)(8000.0f * sinf(2.0f * 3.14159265f * 400.0f * n / 8000.0f));
        high[n] = (int16_t)(8000.0f * sinf(2.0f * 3.14159265f * 2300.0f * n / 8000.0f));
        n++;
    }
    VowelFeature low_feature;
    VowelFeature high_feature;
    ExtractVowelFeature(low, &low_feature);
    ExtractVowelFeature(high, &high_feature);
    assert(loudestBand(&low_feature) < loudestBand(&high_feature));
}

static void testVowelFeatureIgnoresLoudness(void)
{
    int16_t quiet[VOWEL_FRAME_SAMPLES];
    int16_t loud[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(quiet, 120.0f, 750.0f, 1200.0f, 4000.0f);
    synthesizeVowel(loud, 120.0f, 750.0f, 1200.0f, 16000.0f);
    VowelFeature quiet_feature;
    VowelFeature loud_feature;
    ExtractVowelFeature(quiet, &quiet_feature);
    ExtractVowelFeature(loud, &loud_feature);
    int band = 0;
    while (band < VOWEL_BANDS) {
        assert(fabsf(quiet_feature.bands[band] - loud_feature.bands[band]) < 0.05f);
        band++;
    }
}

static void testVowelClassifiesAllFiveAtAnotherPitch(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        assert(classifySynthetic(templates, vowel, 150.0f, 2000.0f) == (Vowel)vowel);
        assert(classifySynthetic(templates, vowel, 100.0f, 6000.0f) == (Vowel)vowel);
        vowel++;
    }
}

static void testMouthShapeGroupsEWithIAndOWithU(void)
{
    assert(SelectMouthShapeOfVowel(VOWEL_A) == MOUTH_A);
    assert(SelectMouthShapeOfVowel(VOWEL_I) == MOUTH_I);
    assert(SelectMouthShapeOfVowel(VOWEL_E) == MOUTH_I);
    assert(SelectMouthShapeOfVowel(VOWEL_U) == MOUTH_U);
    assert(SelectMouthShapeOfVowel(VOWEL_O) == MOUTH_U);
}

static void feedSyntheticFrame(VowelTracker *tracker, int vowel, bool voiced, const VowelFeature templates[VOWEL_COUNT], int count, bool *completed)
{
    int16_t frame[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(frame, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 3000.0f);
    int n = 0;
    while (n < count) {
        *completed = TrackVowel(tracker, frame[n % VOWEL_FRAME_SAMPLES], voiced, templates);
        n++;
    }
}

static void testTrackerUpdatesShapeOncePerFrame(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_A};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES - 1, &completed);
    assert(!completed && tracker.shape == MOUTH_A);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, 1, &completed);
    assert(completed);
}

static void testTrackerSwitchesWithinFourFrames(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    assert(tracker.shape == MOUTH_I);
}

static void testTrackerIgnoresOneStrayFrame(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(tracker.shape == MOUTH_U);
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(tracker.shape == MOUTH_U);
}

static void testTrackerKeepsShapeWhenSilent(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_A, false, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(completed && tracker.shape == MOUTH_U);
}


/* Deterministic white noise */
static int16_t nextNoise(uint32_t *state, int amplitude)
{
    *state = *state * 1664525u + 1013904223u;
    return (int16_t)((int)(*state >> 16) % (2 * amplitude + 1) - amplitude);
}

#define ROOM_NOISE 600

static void feedSilence(VowelTracker *tracker, const VowelFeature templates[VOWEL_COUNT], uint32_t *state)
{
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES * 40) {
        TrackVowel(tracker, nextNoise(state, ROOM_NOISE), false, templates);
        n++;
    }
}

/* Like vcamera-calibrate: loud vowels in room noise, averaged over the tracker's features */
static void calibrateInNoise(VowelFeature templates[VOWEL_COUNT])
{
    static const VowelFeature none[VOWEL_COUNT];
    uint32_t state = 7;
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        VowelTracker tracker = {.shape = MOUTH_A};
        feedSilence(&tracker, none, &state);
        int16_t voice[VOWEL_FRAME_SAMPLES];
        synthesizeVowel(voice, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 4000.0f);
        VowelFeature sum = {{0}};
        int frame = 0;
        while (frame < 10) {
            int n = 0;
            while (n < VOWEL_FRAME_SAMPLES) {
                TrackVowel(&tracker, voice[n] + nextNoise(&state, ROOM_NOISE), true, none);
                n++;
            }
            int band = 0;
            while (band < VOWEL_BANDS) {
                sum.bands[band] += tracker.feature.bands[band] / 10.0f;
                band++;
            }
            frame++;
        }
        templates[vowel] = sum;
        vowel++;
    }
}

static MouthShape trackNormalVoice(const VowelFeature templates[VOWEL_COUNT], int vowel, MouthShape start)
{
    VowelTracker tracker = {.shape = start};
    uint32_t state = 1;
    feedSilence(&tracker, templates, &state);
    int16_t voice[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(voice, 140.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 1350.0f);
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES * 10) {
        TrackVowel(&tracker, voice[n % VOWEL_FRAME_SAMPLES] + nextNoise(&state, ROOM_NOISE), true, templates);
        n++;
    }
    return tracker.shape;
}

/* Registered loud, spoken at a third of that: the high bands drown in noise and U looked like I */
static void testTrackerKeepsUAndIApartAtNormalVolume(void)
{
    VowelFeature templates[VOWEL_COUNT];
    calibrateInNoise(templates);
    assert(trackNormalVoice(templates, VOWEL_U, MOUTH_I) == MOUTH_U);
    assert(trackNormalVoice(templates, VOWEL_I, MOUTH_U) == MOUTH_I);
}

int main(void)
{
    testTrackerKeepsUAndIApartAtNormalVolume();
    testMouthShapeChangesWidth();
    testTrackerUpdatesShapeOncePerFrame();
    testTrackerSwitchesWithinFourFrames();
    testTrackerIgnoresOneStrayFrame();
    testTrackerKeepsShapeWhenSilent();
    testVowelFeaturePeaksAtToneBand();
    testVowelFeatureIgnoresLoudness();
    testVowelClassifiesAllFiveAtAnotherPitch();
    testMouthShapeGroupsEWithIAndOWithU();
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
    testArtFaceIsCopiedOntoPaper();
    testClosedMouthIsAThinLine();
    testMouthGrowsWithOpenness();
    testMouthSizeSpansClosedLineToDrawnSize();
    testEasedMouthMovesPartWayToTarget();
    testEasedMouthReachesTarget();
    puts("ok");
    return 0;
}
