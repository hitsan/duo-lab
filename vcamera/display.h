#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Implementation chosen at link time: terminal now, UVC image later.
   Called for every mic sample; the display decides when to draw a frame */
void ShowMouth(uint8_t openness);

#endif
