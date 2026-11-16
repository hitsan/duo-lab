#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include "display.h"
#include "shared_openness.h"

#define NS_PER_SECOND 1000000000L

/* Reads the openness the small core writes to shared memory and draws it at DISPLAY_FPS */
int main(void)
{
    const int memory_file = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_file < 0) {
        perror("open /dev/mem");
        return 1;
    }
    void *const page = mmap(NULL, SHARED_OPENNESS_PAGE_SIZE, PROT_READ, MAP_SHARED, memory_file, SHARED_OPENNESS_ADDRESS);
    if (page == MAP_FAILED) {
        perror("mmap shared openness");
        return 1;
    }
    const volatile uint32_t *const shared_openness = page;

    struct timespec due;
    clock_gettime(CLOCK_MONOTONIC, &due);
    while (true) {
        const uint32_t shared = *shared_openness;
        const MouthShape shape = (MouthShape)((shared >> SHARED_SHAPE_SHIFT) & 0xFF);
        ShowMouth((uint8_t)shared, shape < MOUTH_SHAPE_COUNT ? shape : MOUTH_A);
        due.tv_nsec += NS_PER_SECOND / DISPLAY_FPS;
        if (due.tv_nsec >= NS_PER_SECOND) {
            due.tv_sec++;
            due.tv_nsec -= NS_PER_SECOND;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL);
    }
}
