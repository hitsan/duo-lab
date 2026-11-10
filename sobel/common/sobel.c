#include <stdlib.h>
#include "sobel.h"

void sobel_rows(const uint8_t *in, uint8_t *out, int w, int h, int y0, int y1)
{
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                out[y * w + x] = 0;
                continue;
            }
            const uint8_t *p = &in[(y - 1) * w + (x - 1)];
            int gx = -p[0] + p[2] - 2 * p[w] + 2 * p[w + 2] - p[2 * w] + p[2 * w + 2];
            int gy = -p[0] - 2 * p[1] - p[2] + p[2 * w] + 2 * p[2 * w + 1] + p[2 * w + 2];
            int m = abs(gx) + abs(gy);
            out[y * w + x] = m > 255 ? 255 : m;
        }
    }
}
