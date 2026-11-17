#include "adc_sample.h"

/* 12-bit -> 16-bit: (raw - bias) * 16 in Q16 is a division by 65536 / 16 */
#define Q16_TO_SAMPLE 4096
/* Bias follows with a 1024-sample (128ms at 8kHz) time constant, far below voice frequencies */
#define BIAS_SHIFT 10

void StartAdcBias(AdcBias *bias, uint16_t first_raw)
{
    bias->bias_q16 = (int32_t)first_raw << 16;
}

int16_t ConvertAdcSample(AdcBias *bias, uint16_t raw)
{
    const int32_t difference_q16 = ((int32_t)raw << 16) - bias->bias_q16;
    const int32_t sample = difference_q16 / Q16_TO_SAMPLE;
    bias->bias_q16 += difference_q16 >> BIAS_SHIFT;
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}
