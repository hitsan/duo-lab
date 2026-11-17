#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include "shared_openness.h"
#include "vowel_types.h"

#define RECORD_SECONDS 3
#define POLL_MS 32
#define MIN_FRAMES 20

static const char *const VOWEL_NAMES[VOWEL_COUNT] = {"a", "i", "u", "e", "o"};

static void sleepMs(long ms)
{
    const struct timespec duration = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&duration, NULL);
}

/* Averages the small core's features while the mouth is open. Returns the frames used */
static int recordVowel(const volatile uint8_t *page, VowelFeature *average)
{
    const volatile uint32_t *openness = (const volatile uint32_t *)page;
    const volatile VowelFeature *feature = (const volatile VowelFeature *)(page + SHARED_FEATURE_OFFSET);
    VowelFeature sum = {{0}};
    int frames = 0;
    int poll = 0;
    while (poll < RECORD_SECONDS * 1000 / POLL_MS) {
        if ((*openness & 0xFF) > 0) {
            int band = 0;
            while (band < VOWEL_BANDS) {
                sum.bands[band] += feature->bands[band];
                band++;
            }
            frames++;
        }
        sleepMs(POLL_MS);
        poll++;
    }
    int band = 0;
    while (band < VOWEL_BANDS) {
        average->bands[band] = frames > 0 ? sum.bands[band] / frames : 0.0f;
        band++;
    }
    return frames;
}

/* Asks for each vowel through the mic, then prints vowel_templates.h on stdout */
int main(void)
{
    const int memory_file = open("/dev/mem", O_RDONLY | O_SYNC);
    if (memory_file < 0) {
        perror("open /dev/mem");
        return 1;
    }
    const void *const page = mmap(NULL, SHARED_OPENNESS_PAGE_SIZE, PROT_READ, MAP_SHARED, memory_file, SHARED_OPENNESS_ADDRESS);
    if (page == MAP_FAILED) {
        perror("mmap shared page");
        return 1;
    }

    VowelFeature templates[VOWEL_COUNT];
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        fprintf(stderr, "Say \"%s\" steadily for %d seconds. Press Enter to start.", VOWEL_NAMES[vowel], RECORD_SECONDS);
        getchar();
        const int frames = recordVowel(page, &templates[vowel]);
        if (frames < MIN_FRAMES) {
            fprintf(stderr, "only %d voiced frames; speak louder or closer and retry\n", frames);
            continue;
        }
        fprintf(stderr, "ok (%d frames)\n", frames);
        vowel++;
    }

    printf("#ifndef VOWEL_TEMPLATES_H\n#define VOWEL_TEMPLATES_H\n\n#include \"vowel.h\"\n\n");
    printf("/* Written by vcamera-calibrate */\nstatic const VowelFeature VOWEL_TEMPLATES[VOWEL_COUNT] = {\n");
    vowel = 0;
    while (vowel < VOWEL_COUNT) {
        printf("    {{");
        int band = 0;
        while (band < VOWEL_BANDS) {
            printf("%s%.3ff", band > 0 ? ", " : "", templates[vowel].bands[band]);
            band++;
        }
        printf("}},  /* %s */\n", VOWEL_NAMES[vowel]);
        vowel++;
    }
    printf("};\n\n#endif\n");
    return 0;
}
