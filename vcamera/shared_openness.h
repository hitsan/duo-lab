#ifndef SHARED_OPENNESS_H
#define SHARED_OPENNESS_H

/* uint32 in the last page of the FreeRTOS region. The low 8 bits are the mouth openness.
   Written by the small core, read by Linux through /dev/mem */
#define SHARED_OPENNESS_ADDRESS 0x83FFF000u
#define SHARED_OPENNESS_PAGE_SIZE 4096u

#endif
