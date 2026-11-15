#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include "mic.h"
#include "vowel.h"

#define PI 3.14159265f
/* Per voiced frame, about 100ms to settle */
#define SMOOTHING 0.3f
/* Switch shape only when another is clearly nearer (squared distances) */
#define SWITCH_RATIO 0.7f
/* Per silent frame, about 1s to learn the noise */
#define NOISE_LEARNING 0.03f
/* Keep this much of the noise after subtraction so near-empty bands stay finite and steady */
#define NOISE_FLOOR 0.1f
#define HALF (VOWEL_FRAME_SAMPLES / 2)

/* Narrow where F1 lives, wider toward F2 and above */
static const float BAND_EDGES_HZ[VOWEL_BANDS + 1] = {
    150, 300, 450, 600, 800, 1000, 1250, 1500, 1800, 2100, 2500,
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

void MeasureVowelBands(const int16_t *frame, float energies[VOWEL_BANDS])
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
    int band = 0;
    while (band < VOWEL_BANDS) {
        int bin = (int)(BAND_EDGES_HZ[band] / hz_per_bin);
        const int end = (int)(BAND_EDGES_HZ[band + 1] / hz_per_bin);
        float energy = 0.0f;
        while (bin < end && bin <= HALF) {
            energy += real[bin] * real[bin] + imaginary[bin] * imaginary[bin];
            bin++;
        }
        energies[band] = energy;
        band++;
    }
}

void MakeVowelFeature(const float energies[VOWEL_BANDS], const float *noise, VowelFeature *feature)
{
    float mean = 0.0f;
    int band = 0;
    while (band < VOWEL_BANDS) {
        const float noise_energy = noise != NULL ? noise[band] : 0.0f;
        const float floor = NOISE_FLOOR * noise_energy + 1.0f;  /* +1 so an empty band stays finite */
        const float voice_energy = energies[band] - noise_energy;
        feature->bands[band] = log10f(voice_energy > floor ? voice_energy : floor);
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

void ExtractVowelFeature(const int16_t *frame, VowelFeature *feature)
{
    float energies[VOWEL_BANDS];
    MeasureVowelBands(frame, energies);
    MakeVowelFeature(energies, NULL, feature);
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

/* Averaging energies, not logs, shrinks the noise before it is subtracted */
static void smoothEnergies(VowelTracker *tracker, const float energies[VOWEL_BANDS])
{
    int band = 0;
    while (band < VOWEL_BANDS) {
        float *voice = &tracker->voice[band];
        *voice = tracker->smoothing ? *voice + SMOOTHING * (energies[band] - *voice) : energies[band];
        band++;
    }
    tracker->smoothing = true;
    MakeVowelFeature(tracker->voice, tracker->noise, &tracker->smoothed);
}

/* sticky: within an utterance, switch only when another shape is clearly nearer */
static MouthShape chooseShape(const VowelTracker *tracker, const VowelFeature templates[VOWEL_COUNT], bool sticky)
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
    if (!sticky) {
        return best;
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
    float energies[VOWEL_BANDS];
    MeasureVowelBands(tracker->frame, energies);
    if (!voiced) {
        /* A new utterance starts fresh: no averaging with, or sticking to, the last one */
        tracker->smoothing = false;
        int band = 0;
        while (band < VOWEL_BANDS) {
            float *noise = &tracker->noise[band];
            *noise = tracker->noise_learned ? *noise + NOISE_LEARNING * (energies[band] - *noise) : energies[band];
            band++;
        }
        tracker->noise_learned = true;
    }
    MakeVowelFeature(energies, tracker->noise, &tracker->feature);
    if (voiced) {
        const bool continuing = tracker->smoothing;
        smoothEnergies(tracker, energies);
        tracker->shape = chooseShape(tracker, templates, continuing);
    }
    return true;
}
