#include "arch_helpers.h"
#include "display.h"
#include "shared_openness.h"

void ShowMouth(uint8_t openness)
{
    *(volatile uint32_t *)(uintptr_t)SHARED_OPENNESS_ADDRESS = openness;
    /* The C906 dcache is write-back; without this Linux keeps reading stale DRAM */
    clean_dcache_range(SHARED_OPENNESS_ADDRESS, 64);
}
