#include <stdio.h>
#include "display.h"
#include "lipsync.h"

#define MOUTH_SHAPES 5
#define BAR_CELLS 20

/* Always print the same number of lines so the screen doesn't shift */
static void drawMouth(int shape)
{
    if (shape == 0) {
        printf("     ═════════     \n");
        int row = 0;
        while (row < MOUTH_SHAPES) {
            printf("                   \n");
            row++;
        }
        return;
    }
    printf("     ╭───────╮     \n");
    int row = 0;
    while (row < MOUTH_SHAPES - 1) {
        printf(row < shape ? "     │███████│     \n" : "                   \n");
        row++;
    }
    printf("     ╰───────╯     \n");
}

static void drawStatus(long frame_count, uint8_t openness, MouthShape shape)
{
    printf("\n %5.2fs  %c openness %3d ", (double)frame_count / DISPLAY_FPS, "AIU"[shape], openness);
    const int filled_cells = openness * BAR_CELLS / 256;
    int cell = 0;
    while (cell < BAR_CELLS) {
        fputs(cell < filled_cells ? "█" : "░", stdout);
        cell++;
    }
    printf("\n");
}

void ShowMouth(uint8_t openness, MouthShape shape)
{
    static long frame_count;
    if (frame_count == 0) {
        printf("\033[2J");
    }
    printf("\033[H");
    printf("\n     ●       ●     \n\n");
    drawMouth(SelectMouthShape(openness, MOUTH_SHAPES));
    drawStatus(frame_count, openness, shape);
    fflush(stdout);
    frame_count++;
}
