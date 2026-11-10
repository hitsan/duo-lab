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

static void test_horizontal_edge_lights_edge_rows(void)
{
    enum { W = 5, H = 6 };
    uint8_t in[W * H], out[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            in[y * W + x] = y < 3 ? 10 : 50;

    sobel_rows(in, out, W, H, 0, H);

    for (int x = 1; x < W - 1; x++) {
        assert(out[1 * W + x] == 0);
        assert(out[2 * W + x] == 160);
        assert(out[3 * W + x] == 160);
        assert(out[4 * W + x] == 0);
    }
}

static void test_magnitude_saturates_at_255(void)
{
    enum { W = 4, H = 3 };
    uint8_t in[W * H], out[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            in[y * W + x] = x < 2 ? 0 : 255;

    sobel_rows(in, out, W, H, 0, H);

    assert(out[1 * W + 1] == 255);
    assert(out[1 * W + 2] == 255);
}

static void test_border_pixels_are_zero(void)
{
    enum { W = 5, H = 4 };
    uint8_t in[W * H], out[W * H];
    for (int i = 0; i < W * H; i++)
        in[i] = (i % 2) ? 200 : 0;
    memset(out, 0xAA, sizeof out);

    sobel_rows(in, out, W, H, 0, H);

    for (int x = 0; x < W; x++) {
        assert(out[x] == 0);
        assert(out[(H - 1) * W + x] == 0);
    }
    for (int y = 0; y < H; y++) {
        assert(out[y * W] == 0);
        assert(out[y * W + W - 1] == 0);
    }
}

static void test_band_writes_only_its_rows(void)
{
    enum { W = 5, H = 6 };
    uint8_t in[W * H], out[W * H];
    memset(in, 120, sizeof in);
    memset(out, 0xAA, sizeof out);

    sobel_rows(in, out, W, H, 2, 4);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            assert(out[y * W + x] == ((y >= 2 && y < 4) ? 0 : 0xAA));
}

int main(void)
{
    test_uniform_image_gives_zero();
    test_vertical_edge_lights_edge_columns();
    test_horizontal_edge_lights_edge_rows();
    test_magnitude_saturates_at_255();
    test_border_pixels_are_zero();
    test_band_writes_only_its_rows();
    printf("all tests passed\n");
    return 0;
}
