#include <math.h>
#include "lipsync.h"
#include "mic.h"

#define RISE_MS 25.0f
#define FALL_MS 80.0f
/* Factor that reaches 63% of a step change in *_MS. Constant, so folded at compile time */
#define RISE_RATE (1.0f - expf(-1000.0f / (MIC_SAMPLE_RATE * RISE_MS)))
#define FALL_RATE (1.0f - expf(-1000.0f / (MIC_SAMPLE_RATE * FALL_MS)))

float SmoothVoiceLevel(float level, float sample)
{
    float rate = sample > level ? RISE_RATE : FALL_RATE;
    return level + rate * (sample - level);
}

int SelectMouthShape(uint8_t openness, int shape_count)
{
    return (openness * (shape_count - 1) + 127) / 255;
}

uint8_t CalcMouthOpenness(float level, float closed_db, float full_open_db)
{
    if (level <= 0.0f) {
        return 0;
    }
    float level_db = 20.0f * log10f(level);
    float ratio = (level_db - closed_db) / (full_open_db - closed_db);
    if (ratio <= 0.0f) {
        return 0;
    }
    if (ratio >= 1.0f) {
        return 255;
    }
    return (uint8_t)(ratio * 255.0f + 0.5f);
}
