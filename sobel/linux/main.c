#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <stdio.h>
#include <stdlib.h>
#include "sobel.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s in.png out.png\n", argv[0]);
        return 1;
    }

    int w, h, n;
    uint8_t *in = stbi_load(argv[1], &w, &h, &n, 1);
    if (!in) {
        fprintf(stderr, "%s: %s\n", argv[1], stbi_failure_reason());
        return 1;
    }

    uint8_t *out = malloc((size_t)w * h);
    if (!out) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    sobel_rows(in, out, w, h, 0, h);

    if (!stbi_write_png(argv[2], w, h, 1, out, w)) {
        fprintf(stderr, "%s: write failed\n", argv[2]);
        return 1;
    }

    printf("%dx%d -> %s\n", w, h, argv[2]);
    stbi_image_free(in);
    free(out);
    return 0;
}
