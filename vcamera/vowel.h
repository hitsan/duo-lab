#ifndef VOWEL_H
#define VOWEL_H

#include <stdbool.h>
#include <stdint.h>

/* 32ms at 8kHz */
#define VOWEL_FRAME_SAMPLES 256
#define VOWEL_BANDS 13

typedef enum {
    VOWEL_A,
    VOWEL_I,
    VOWEL_U,
    VOWEL_E,
    VOWEL_O,
    VOWEL_COUNT
} Vowel;

/* Three mouth shapes: E shares the spread lips of I, O the rounded lips of U.
   O sounds closer to A than to U, so the classifier keeps all five apart */
typedef enum {
    MOUTH_A,
    MOUTH_I,
    MOUTH_U,
    MOUTH_SHAPE_COUNT
} MouthShape;

/* Log band energies with their mean removed, so loudness drops out */
typedef struct {
    float bands[VOWEL_BANDS];
} VowelFeature;

void ExtractVowelFeature(const int16_t *frame, VowelFeature *feature);
/* The vowel whose template is nearest */
Vowel ClassifyVowel(const VowelFeature *feature, const VowelFeature templates[VOWEL_COUNT]);
MouthShape SelectMouthShapeOfVowel(Vowel vowel);

/* Collects samples into frames; zero-initialize, then set shape */
typedef struct {
    int16_t frame[VOWEL_FRAME_SAMPLES];
    int filled;
    VowelFeature feature;   /* of the last full frame */
    MouthShape shape;       /* kept through silence so the mouth closes in the last shape */
} VowelTracker;

/* true when this sample completed a frame and feature was updated. The shape changes only if voiced */
bool TrackVowel(VowelTracker *tracker, int16_t sample, bool voiced, const VowelFeature templates[VOWEL_COUNT]);

#endif
