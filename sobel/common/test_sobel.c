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

int main(void)
{
    test_uniform_image_gives_zero();
    printf("all tests passed\n");
    return 0;
}
