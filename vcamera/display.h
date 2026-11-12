#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_FPS 30

/* Implementation chosen at link time: terminal, shared memory for the small core, UVC image later.
   Shows one frame; the caller decides the rate */
void ShowMouth(uint8_t openness);

#endif
