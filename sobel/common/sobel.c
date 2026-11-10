#include "sobel.h"

void sobel_rows(const uint8_t *in, uint8_t *out, int w, int h, int y0, int y1)
{
    (void)in; (void)y0; (void)y1;
    for (int i = 0; i < w * h; i++)
        out[i] = 0;
}
