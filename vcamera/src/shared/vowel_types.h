#ifndef VOWEL_TYPES_H
#define VOWEL_TYPES_H

/* Up to 2.5kHz: above that, normal-volume speech sinks into the mic noise */
#define VOWEL_BANDS 10

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

#endif
