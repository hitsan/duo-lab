#include <stdio.h>
#include "display.h"
#include "lipsync.h"
#include "mic.h"

#define FPS 30
#define MOUTH_SHAPES 5
#define BAR_CELLS 20

/* Mouth height grows with the shape. Fixed line count keeps the screen from shifting */
void ShowMouth(uint8_t openness)
{
    /* MIC_SAMPLE_RATE / FPS is not an integer; carry the remainder so frames don't drift */
    static int frame_timer;
    frame_timer += FPS;
    if (frame_timer < MIC_SAMPLE_RATE) {
        return;
    }
    frame_timer -= MIC_SAMPLE_RATE;

    static long frame_count;
    if (frame_count == 0) {
        printf("\033[2J");
    }
    int shape = SelectMouthShape(openness, MOUTH_SHAPES);
    printf("\033[H");
    printf("\n     ●       ●     \n\n");
    if (shape == 0) {
        printf("     ═════════     \n");
        for (int row = 0; row < MOUTH_SHAPES; row++)
            printf("                   \n");
    } else {
        printf("     ╭───────╮     \n");
        for (int row = 0; row < MOUTH_SHAPES - 1; row++)
            printf(row < shape ? "     │███████│     \n" : "                   \n");
        printf("     ╰───────╯     \n");
    }
    printf("\n %5.2fs  openness %3d ", (double)frame_count++ / FPS, openness);
    for (int cell = 0; cell < BAR_CELLS; cell++)
        fputs(cell < openness * BAR_CELLS / 256 ? "█" : "░", stdout);
    printf("\n");
    fflush(stdout);
}
