#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#define DEFAULT_FACE_SCALE_PERCENT 85
#define MAX_MOUTH_SCALE_PERCENT 300
#define MAX_SWAY_PIXELS 20
#define LINE_SIZE 512

/* A path too long to fit becomes empty, so it fails to load instead of naming another file */
static void joinPath(char *path, const char *directory, const char *name)
{
    const int length = name[0] == '/' ? snprintf(path, CONFIG_PATH_SIZE, "%s", name)
                                      : snprintf(path, CONFIG_PATH_SIZE, "%s/%s", directory, name);
    if (length < 0 || length >= CONFIG_PATH_SIZE) {
        path[0] = '\0';
    }
}

void SetDefaultConfig(Config *config, const char *directory)
{
    joinPath(config->face, directory, "face.png");
    joinPath(config->mouth, directory, "mouth.png");
    config->background[0] = '\0';
    config->face_scale_percent = DEFAULT_FACE_SCALE_PERCENT;
    config->ease_percent = DEFAULT_EASE_PERCENT;
    config->sway_pixels = DEFAULT_SWAY_PIXELS;
    config->ascii = false;
    memcpy(config->mouth_scales, DEFAULT_MOUTH_SCALES, sizeof config->mouth_scales);
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool parseNumber(const char *text, int low, int high, int *number)
{
    char *end;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < low || value > high) {
        return false;
    }
    *number = (int)value;
    return true;
}

/* "140x55": width and height percent */
static bool parseMouthScale(char *text, MouthScale *scale)
{
    char *const separator = strchr(text, 'x');
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    MouthScale parsed;
    if (!parseNumber(text, 1, MAX_MOUTH_SCALE_PERCENT, &parsed.width_percent) ||
        !parseNumber(separator + 1, 1, MAX_MOUTH_SCALE_PERCENT, &parsed.height_percent)) {
        return false;
    }
    *scale = parsed;
    return true;
}

bool ReadConfigLine(Config *config, const char *line, const char *directory)
{
    char buffer[LINE_SIZE];
    snprintf(buffer, sizeof buffer, "%s", line);
    char *const text = trim(buffer);
    if (text[0] == '\0' || text[0] == '#') {
        return true;
    }
    char *const separator = strchr(text, '=');
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    const char *const key = trim(text);
    char *const value = trim(separator + 1);
    if (strcmp(key, "face") == 0) {
        joinPath(config->face, directory, value);
        return true;
    }
    if (strcmp(key, "mouth") == 0) {
        joinPath(config->mouth, directory, value);
        return true;
    }
    if (strcmp(key, "background") == 0) {
        joinPath(config->background, directory, value);
        return true;
    }
    if (strcmp(key, "face_scale") == 0) {
        return parseNumber(value, 1, 100, &config->face_scale_percent);
    }
    if (strcmp(key, "ease_percent") == 0) {
        return parseNumber(value, 1, 100, &config->ease_percent);
    }
    if (strcmp(key, "sway") == 0) {
        return parseNumber(value, 0, MAX_SWAY_PIXELS, &config->sway_pixels);
    }
    if (strcmp(key, "ascii") == 0) {
        int ascii;
        if (!parseNumber(value, 0, 1, &ascii)) {
            return false;
        }
        config->ascii = ascii == 1;
        return true;
    }
    if (strcmp(key, "mouth_a") == 0) {
        return parseMouthScale(value, &config->mouth_scales[MOUTH_A]);
    }
    if (strcmp(key, "mouth_i") == 0) {
        return parseMouthScale(value, &config->mouth_scales[MOUTH_I]);
    }
    if (strcmp(key, "mouth_u") == 0) {
        return parseMouthScale(value, &config->mouth_scales[MOUTH_U]);
    }
    return false;
}

bool LoadConfig(Config *config, const char *path)
{
    char directory[CONFIG_PATH_SIZE];
    snprintf(directory, sizeof directory, "%s", path);
    char *const slash = strrchr(directory, '/');
    if (slash == NULL) {
        snprintf(directory, sizeof directory, ".");
    } else {
        *slash = '\0';
    }
    SetDefaultConfig(config, directory);
    FILE *const file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    char line[LINE_SIZE];
    int number = 1;
    while (fgets(line, sizeof line, file) != NULL) {
        if (!ReadConfigLine(config, line, directory)) {
            fprintf(stderr, "%s:%d: ignored: %s", path, number, line);
        }
        number++;
    }
    fclose(file);
    return true;
}
