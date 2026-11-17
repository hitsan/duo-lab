#ifndef LIPSYNC_H
#define LIPSYNC_H

#include <stdint.h>

/* Follows the voice level (0..1): rises fast when speaking, falls slowly when quiet.
   Feed one sample at MIC_SAMPLE_RATE; returns the new level */
float SmoothVoiceLevel(float level, float sample);
/* 0 at or below closed_db, 255 at or above full_open_db, linear in dB between */
uint8_t CalcMouthOpenness(float level, float closed_db, float full_open_db);
int SelectMouthShape(uint8_t openness, int shape_count);

#endif
