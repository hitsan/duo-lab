#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "uvc_control.h"

#define CONFIG_PATH_SIZE 256
#define CONFIG_FILE_NAME "vcamera.conf"

typedef struct {
    char face[CONFIG_PATH_SIZE];
    char mouth[CONFIG_PATH_SIZE];
    char background[CONFIG_PATH_SIZE];  /* empty: a solid color */
    int face_scale_percent;
    int ease_percent;
    int sway_pixels;  /* how far the face drifts each way; 0: still */
    bool ascii;
    MouthScale mouth_scales[MOUTH_SHAPE_COUNT];
} Config;

/* Image paths default to face.png and mouth.png in directory */
void SetDefaultConfig(Config *config, const char *directory);
/* One key=value line; relative paths are taken from directory. true when the line is valid or blank.
   A bad line leaves config as it was */
bool ReadConfigLine(Config *config, const char *line, const char *directory);
/* Defaults, then the file's lines, with paths relative to the file. Bad lines are reported on stderr and
   skipped. false when the file cannot be opened; config then holds the defaults */
bool LoadConfig(Config *config, const char *path);

#endif
