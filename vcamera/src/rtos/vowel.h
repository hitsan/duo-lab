#ifndef VOWEL_H
#define VOWEL_H

#include <stdbool.h>
#include <stdint.h>
#include "vowel_types.h"

/* 32ms at 8kHz */
#define VOWEL_FRAME_SAMPLES 256
/* Linear energy per band */
void MeasureVowelBands(const int16_t *frame, float energies[VOWEL_BANDS]);
/* noise: energies measured in silence, subtracted before the logs. NULL for none */
void MakeVowelFeature(const float energies[VOWEL_BANDS], const float *noise, VowelFeature *feature);
void ExtractVowelFeature(const int16_t *frame, VowelFeature *feature);
/* The vowel whose template is nearest */
Vowel ClassifyVowel(const VowelFeature *feature, const VowelFeature templates[VOWEL_COUNT]);
MouthShape SelectMouthShapeOfVowel(Vowel vowel);

/* Collects samples into frames; zero-initialize, then set shape */
typedef struct {
    int16_t frame[VOWEL_FRAME_SAMPLES];
    int filled;
    VowelFeature feature;   /* of the last full frame */
    float voice[VOWEL_BANDS];  /* band energies averaged over voiced frames */
    VowelFeature smoothed;     /* of voice */
    float noise[VOWEL_BANDS];  /* band energies averaged over silent frames */
    bool noise_learned;
    bool smoothing;
    MouthShape shape;       /* kept through silence so the mouth closes in the last shape */
} VowelTracker;

/* true when this sample completed a frame and feature was updated. The shape changes only if voiced */
bool TrackVowel(VowelTracker *tracker, int16_t sample, bool voiced, const VowelFeature templates[VOWEL_COUNT]);

#endif
