#ifndef ADC_SAMPLE_H
#define ADC_SAMPLE_H

#include <stdint.h>

/* The mic output sits on a DC bias (about 1500 counts) that drifts with temperature and parts */
typedef struct {
    int32_t bias_q16;
} AdcBias;

void StartAdcBias(AdcBias *bias, uint16_t first_raw);
/* 12-bit raw ADC value -> signed 16-bit sample around the tracked bias */
int16_t ConvertAdcSample(AdcBias *bias, uint16_t raw);

#endif
