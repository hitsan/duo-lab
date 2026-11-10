#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sobel.h"

static void test_uniform_image_gives_zero(void)
{
    enum { W = 5, H = 4 };
    uint8_t in[W * H], out[W * H];
    memset(in, 120, sizeof in);
    memset(out, 0xAA, sizeof out);

    sobel_rows(in, out, W, H, 0, H);

    for (int i = 0; i < W * H; i++)
        assert(out[i] == 0);
}

static void test_vertical_edge_lights_edge_columns(void)
{
    enum { W = 6, H = 5 };
    uint8_t in[W * H], out[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            in[y * W + x] = x < 3 ? 10 : 50;

    sobel_rows(in, out, W, H, 0, H);

    for (int y = 1; y < H - 1; y++) {
        assert(out[y * W + 1] == 0);
        assert(out[y * W + 2] == 160);
        assert(out[y * W + 3] == 160);
        assert(out[y * W + 4] == 0);
    }
}

int main(void)
{
    test_uniform_image_gives_zero();
    test_vertical_edge_lights_edge_columns();
    printf("all tests passed\n");
    return 0;
}
