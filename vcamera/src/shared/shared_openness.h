#ifndef SHARED_OPENNESS_H
#define SHARED_OPENNESS_H

/* uint32 in the last page of the FreeRTOS region: bits 0-7 the mouth openness, bits 8-15 the MouthShape.
   Written by the small core, read by Linux through /dev/mem */
#define SHARED_OPENNESS_ADDRESS 0x83FFF000u
#define SHARED_OPENNESS_PAGE_SIZE 4096u
#define SHARED_SHAPE_SHIFT 8
/* VowelFeature of the last 32ms frame, read by vcamera-calibrate. Its own 64-byte cache line */
#define SHARED_FEATURE_OFFSET 64u

#include "vowel_types.h"
/* Small core only */
void ShareVowelFeature(const VowelFeature *feature);

#endif
