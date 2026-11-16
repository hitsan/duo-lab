#include "arch_helpers.h"
#include "display.h"
#include "shared_openness.h"

void ShowMouth(uint8_t openness, MouthShape shape)
{
    *(volatile uint32_t *)(uintptr_t)SHARED_OPENNESS_ADDRESS = openness | (uint32_t)shape << SHARED_SHAPE_SHIFT;
    /* The C906 dcache is write-back; without this Linux keeps reading stale DRAM */
    clean_dcache_range(SHARED_OPENNESS_ADDRESS, 64);
}

void ShareVowelFeature(const VowelFeature *feature)
{
    const uintptr_t address = SHARED_OPENNESS_ADDRESS + SHARED_FEATURE_OFFSET;
    *(volatile VowelFeature *)address = *feature;
    clean_dcache_range(address, 64);
}
