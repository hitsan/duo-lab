#include <math.h>
#include <stdbool.h>
#include "mic.h"
#include "vowel.h"

#define PI 3.14159265f
/* Per voiced frame, about 100ms to settle */
#define SMOOTHING 0.3f
/* Switch shape only when another is clearly nearer (squared distances) */
#define SWITCH_RATIO 0.7f
#define HALF (VOWEL_FRAME_SAMPLES / 2)

/* Narrow where F1 lives, wider toward F2 and above */
static const float BAND_EDGES_HZ[VOWEL_BANDS + 1] = {
    150, 300, 450, 600, 800, 1000, 1250, 1500, 1800, 2100, 2500, 3000, 3500, 4000,
};

static float window[VOWEL_FRAME_SAMPLES];
static float cosines[HALF];
static float sines[HALF];
static bool tables_ready;

static void prepareTables(void)
{
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        window[n] = 0.5f - 0.5f * cosf(2.0f * PI * n / VOWEL_FRAME_SAMPLES);
        n++;
    }
    int k = 0;
    while (k < HALF) {
        cosines[k] = cosf(2.0f * PI * k / VOWEL_FRAME_SAMPLES);
        sines[k] = -sinf(2.0f * PI * k / VOWEL_FRAME_SAMPLES);
        k++;
    }
    tables_ready = true;
}

/* In-place radix-2 FFT */
static void transform(float *real, float *imaginary)
{
    int j = 0;
    int i = 1;
    while (i < VOWEL_FRAME_SAMPLES) {
        int bit = VOWEL_FRAME_SAMPLES >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if (i < j) {
            const float real_swap = real[i];
            real[i] = real[j];
            real[j] = real_swap;
            const float imaginary_swap = imaginary[i];
            imaginary[i] = imaginary[j];
            imaginary[j] = imaginary_swap;
        }
        i++;
    }
    int length = 2;
    while (length <= VOWEL_FRAME_SAMPLES) {
        const int step = VOWEL_FRAME_SAMPLES / length;
        int start = 0;
        while (start < VOWEL_FRAME_SAMPLES) {
            int k = 0;
            while (k < length / 2) {
                const float twiddle_real = cosines[k * step];
                const float twiddle_imaginary = sines[k * step];
                const int top = start + k;
                const int bottom = top + length / 2;
                const float product_real = real[bottom] * twiddle_real - imaginary[bottom] * twiddle_imaginary;
                const float product_imaginary = real[bottom] * twiddle_imaginary + imaginary[bottom] * twiddle_real;
                real[bottom] = real[top] - product_real;
                imaginary[bottom] = imaginary[top] - product_imaginary;
                real[top] += product_real;
                imaginary[top] += product_imaginary;
                k++;
            }
            start += length;
        }
        length *= 2;
    }
}

void ExtractVowelFeature(const int16_t *frame, VowelFeature *feature)
{
    if (!tables_ready) {
        prepareTables();
    }
    float real[VOWEL_FRAME_SAMPLES];
    float imaginary[VOWEL_FRAME_SAMPLES];
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        real[n] = frame[n] * window[n];
        imaginary[n] = 0.0f;
        n++;
    }
    transform(real, imaginary);

    const float hz_per_bin = (float)MIC_SAMPLE_RATE / VOWEL_FRAME_SAMPLES;
    float mean = 0.0f;
    int band = 0;
    while (band < VOWEL_BANDS) {
        int bin = (int)(BAND_EDGES_HZ[band] / hz_per_bin);
        const int end = (int)(BAND_EDGES_HZ[band + 1] / hz_per_bin);
        float energy = 1.0f;  /* floor so an empty band stays finite */
        while (bin < end && bin <= HALF) {
            energy += real[bin] * real[bin] + imaginary[bin] * imaginary[bin];
            bin++;
        }
        feature->bands[band] = log10f(energy);
        mean += feature->bands[band];
        band++;
    }
    mean /= VOWEL_BANDS;
    band = 0;
    while (band < VOWEL_BANDS) {
        feature->bands[band] -= mean;
        band++;
    }
}

static float distanceSquared(const VowelFeature *a, const VowelFeature *b)
{
    float distance = 0.0f;
    int band = 0;
    while (band < VOWEL_BANDS) {
        const float difference = a->bands[band] - b->bands[band];
        distance += difference * difference;
        band++;
    }
    return distance;
}

Vowel ClassifyVowel(const VowelFeature *feature, const VowelFeature templates[VOWEL_COUNT])
{
    Vowel nearest = VOWEL_A;
    float nearest_distance = INFINITY;
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        const float distance = distanceSquared(feature, &templates[vowel]);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = (Vowel)vowel;
        }
        vowel++;
    }
    return nearest;
}

MouthShape SelectMouthShapeOfVowel(Vowel vowel)
{
    static const MouthShape SHAPES[VOWEL_COUNT] = {
        [VOWEL_A] = MOUTH_A,
        [VOWEL_I] = MOUTH_I,
        [VOWEL_U] = MOUTH_U,
        [VOWEL_E] = MOUTH_I,
        [VOWEL_O] = MOUTH_U,
    };
    return SHAPES[vowel];
}

static void smoothFeature(VowelTracker *tracker)
{
    int band = 0;
    while (band < VOWEL_BANDS) {
        const float target = tracker->feature.bands[band];
        float *smoothed = &tracker->smoothed.bands[band];
        *smoothed = tracker->smoothing ? *smoothed + SMOOTHING * (target - *smoothed) : target;
        band++;
    }
    tracker->smoothing = true;
}

static MouthShape chooseShape(const VowelTracker *tracker, const VowelFeature templates[VOWEL_COUNT])
{
    float shape_distances[MOUTH_SHAPE_COUNT] = {INFINITY, INFINITY, INFINITY};
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        const MouthShape shape = SelectMouthShapeOfVowel((Vowel)vowel);
        const float distance = distanceSquared(&tracker->smoothed, &templates[vowel]);
        if (distance < shape_distances[shape]) {
            shape_distances[shape] = distance;
        }
        vowel++;
    }
    MouthShape best = tracker->shape;
    int shape = 0;
    while (shape < MOUTH_SHAPE_COUNT) {
        if (shape_distances[shape] < shape_distances[best]) {
            best = (MouthShape)shape;
        }
        shape++;
    }
    return shape_distances[best] < SWITCH_RATIO * shape_distances[tracker->shape] ? best : tracker->shape;
}

bool TrackVowel(VowelTracker *tracker, int16_t sample, bool voiced, const VowelFeature templates[VOWEL_COUNT])
{
    tracker->frame[tracker->filled] = sample;
    tracker->filled++;
    if (tracker->filled < VOWEL_FRAME_SAMPLES) {
        return false;
    }
    tracker->filled = 0;
    ExtractVowelFeature(tracker->frame, &tracker->feature);
    if (voiced) {
        smoothFeature(tracker);
        tracker->shape = chooseShape(tracker, templates);
    }
    return true;
}
