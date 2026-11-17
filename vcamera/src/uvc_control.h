#ifndef UVC_CONTROL_H
#define UVC_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "vowel.h"

#define UVC_FRAME_WIDTH 320
#define UVC_FRAME_HEIGHT 240
#define UVC_FRAME_BYTES (UVC_FRAME_WIDTH * UVC_FRAME_HEIGHT * 2)
#define UVC_RESPONSE_CAPACITY 60

/* Mirrors struct usb_ctrlrequest without the kernel headers, so it builds and tests on the host */
typedef struct {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} UvcRequest;

/* Mirrors struct uvc_request_data. A negative length stalls the request */
typedef struct {
    int32_t length;
    uint8_t data[UVC_RESPONSE_CAPACITY];
} UvcResponse;

/* Drawn mouth size in pixels */
typedef struct {
    int width;
    int height;
} MouthSize;

/* Fully open mouth size per shape, in percent of the drawn mouth */
typedef struct {
    int width_percent;
    int height_percent;
} MouthScale;

/* Share of the remaining distance covered per 30fps frame: about 3 frames to settle */
#define DEFAULT_EASE_PERCENT 50
extern const MouthScale DEFAULT_MOUTH_SCALES[MOUTH_SHAPE_COUNT];

/* 4 bytes per pixel, straight alpha */
typedef struct {
    int width;
    int height;
    uint8_t *pixels;
} RgbaImage;

void AnswerUvcRequest(const UvcRequest *request, int streaming_interface, UvcResponse *response);
/* Fits the face canvas into the frame, shrunk to scale_percent and centered, over background (NULL: a solid
   color) and keeps the mouth, which must share the face's canvas size and have opaque pixels.
   Call before drawing frames */
bool BuildScene(const RgbaImage *face, const RgbaImage *mouth, const RgbaImage *background, int scale_percent);
/* The default background alone, with no mouth: what to show when the images cannot be used */
void BuildPlainScene(void);
/* ease_percent of the remaining distance per frame, and the fully open size per shape */
void SetMouthMotion(int ease_percent, const MouthScale scales[MOUTH_SHAPE_COUNT]);
/* From a line (openness 0) to the shape's full size (255) */
MouthSize CalcMouthSize(uint8_t openness, MouthShape shape);
/* One frame's step toward target, so openness and shape changes blend over a few frames */
MouthSize EaseMouthSize(MouthSize current, MouthSize target);
/* YUYV face with the mouth drawn at size */
void FillMouthFrame(uint8_t *frame, MouthSize size);
/* frame redrawn as colored ASCII glyphs on black, one glyph per 8x8 cell, denser where brighter */
void RenderAscii(uint8_t *ascii, const uint8_t *frame);
/* FillMouthFrame in ASCII; only the cells around the mouth are redrawn each frame */
void FillAsciiMouthFrame(uint8_t *frame, MouthSize size);

#endif
