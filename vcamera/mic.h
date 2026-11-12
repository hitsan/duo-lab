#ifndef MIC_H
#define MIC_H

#include <stdbool.h>
#include <stdint.h>

#define MIC_SAMPLE_RATE 8000

/* Implementation chosen at link time: mic_wav.c on host, ADC on the small core */
/* true on success. On failure the caller may retry */
bool InitMic(void);
/* Returns one sample in MIC_SAMPLE_RATE real time. Blocks until it arrives; never ends */
int16_t ReadMicSample(void);

#endif
