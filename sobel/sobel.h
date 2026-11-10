#ifndef SOBEL_H
#define SOBEL_H

#include <stdint.h>

void sobel_rows(const uint8_t *in, uint8_t *out, int w, int h, int y0, int y1);

#endif
