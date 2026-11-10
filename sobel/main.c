#include <stb_image.h>
#include <stb_image_write.h>

#include <stdio.h>
#include <stdlib.h>
#include "sobel.h"

/* Duo's onboard LED, as used by /mnt/system/blink.sh */
#define LED_PIN "440"
#define LED_DIR "/sys/class/gpio/gpio" LED_PIN

/* On the host these files don't exist, so writes are silently skipped. */
static void write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fputs(s, f);
    fclose(f);
}

static void led(const char *value)
{
    write_file("/sys/class/gpio/export", LED_PIN);
    write_file(LED_DIR "/direction", "out");
    write_file(LED_DIR "/value", value);
}

static int run(const char *in_path, const char *out_path)
{
    int w, h, n;
    uint8_t *in = stbi_load(in_path, &w, &h, &n, 1);
    if (!in) {
        fprintf(stderr, "%s: %s\n", in_path, stbi_failure_reason());
        return 1;
    }

    uint8_t *out = malloc((size_t)w * h);
    if (!out) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    sobel_rows(in, out, w, h, 0, h);

    if (!stbi_write_png(out_path, w, h, 1, out, w)) {
        fprintf(stderr, "%s: write failed\n", out_path);
        return 1;
    }

    printf("%dx%d -> %s\n", w, h, out_path);
    stbi_image_free(in);
    free(out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s in.png out.png\n", argv[0]);
        return 1;
    }

    led("1");
    int rc = run(argv[1], argv[2]);
    led("0");
    return rc;
}
