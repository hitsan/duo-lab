#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adc_sample.h"
#include "config.h"
#include <math.h>
#include "vowel.h"
#include "lipsync.h"
#include "uvc_control.h"

/* A glide settles well within a second at 30fps */
#define SWAY_TEST_FRAMES 60
#define OPENNESS(level) CalcMouthOpenness((level), -45.0f, -12.0f)

static void testOpennessSilenceIsZeroFullScaleIsMax(void)
{
    assert(OPENNESS(0.0f) == 0);
    assert(OPENNESS(1.0f) == 255);
}

static void testOpennessBelowClosedDbIsZero(void)
{
    assert(OPENNESS(0.001f) == 0);  /* -60dB */
    assert(OPENNESS(0.05f) > 0);    /* -26dB */
}

static float smoothRepeatedly(float level, float sample, int count)
{
    while (count > 0) {
        level = SmoothVoiceLevel(level, sample);
        count--;
    }
    return level;
}

static void testVoiceLevelReaches63PercentAfterRiseTime(void)
{
    const float level = smoothRepeatedly(0.0f, 1.0f, 200);  /* RISE_MS = 25ms at 8kHz */
    assert(level > 0.60f && level < 0.66f);
}

static void testVoiceLevelFallsSlowerThanItRises(void)
{
    const float loud_level = smoothRepeatedly(0.0f, 1.0f, 8000);
    const float level = smoothRepeatedly(loud_level, 0.0f, 200);  /* 25ms */
    assert(level > 0.37f);  /* does not fall to 1 - 0.63 within 25ms */
}

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
    FILE *const file = fopen(path, "wb");
    fwrite(data, 1, size, file);
    fclose(file);
}

#define STREAMING_INTERFACE 3
#define CLASS_GET 0xa1
#define CLASS_SET 0x21
#define PROBE_CONTROL 1
#define COMMIT_CONTROL 2

static UvcResponse answerStreaming(uint8_t request_type, uint8_t request, int selector, uint16_t length)
{
    const UvcRequest uvc_request = {
        .request_type = request_type,
        .request = request,
        .value = (uint16_t)(selector << 8),
        .index = STREAMING_INTERFACE,
        .length = length,
    };
    UvcResponse response;
    AnswerUvcRequest(&uvc_request, STREAMING_INTERFACE, &response);
    return response;
}

static uint32_t readLe32(const uint8_t *bytes)
{
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void testProbeGetCurAnswersFormat(void)
{
    const UvcResponse response = answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 26);
    assert(response.length == 26);
    assert(response.data[2] == 1);                      /* bFormatIndex */
    assert(response.data[3] == 1);                      /* bFrameIndex */
    assert(readLe32(response.data + 4) == 333333);      /* dwFrameInterval: 30fps */
    assert(readLe32(response.data + 18) == 320 * 240 * 2);
    assert(readLe32(response.data + 22) == 1024);
}

static void testProbeMinMaxDefAnswerSameFormat(void)
{
    const UvcResponse current = answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 26);
    const uint8_t requests[] = {0x82, 0x83, 0x87};
    int index = 0;
    while (index < 3) {
        const UvcResponse response = answerStreaming(CLASS_GET, requests[index], COMMIT_CONTROL, 26);
        assert(response.length == 26);
        assert(memcmp(response.data, current.data, 26) == 0);
        index++;
    }
}

static void testProbeInfoAndLength(void)
{
    const UvcResponse info = answerStreaming(CLASS_GET, 0x86, PROBE_CONTROL, 1);
    assert(info.length == 1);
    assert(info.data[0] == 0x03);
    const UvcResponse length = answerStreaming(CLASS_GET, 0x85, PROBE_CONTROL, 2);
    assert(length.length == 2);
    assert(length.data[0] == 26 && length.data[1] == 0);
}

static void testProbeSetCurAcceptsData(void)
{
    assert(answerStreaming(CLASS_SET, 0x01, COMMIT_CONTROL, 26).length == 26);
}

static void testProbeShortRequestIsTruncated(void)
{
    assert(answerStreaming(CLASS_GET, 0x81, PROBE_CONTROL, 8).length == 8);
}

static void testRequestsOutsideStreamingInterfaceStall(void)
{
    const UvcRequest unit_request = {.request_type = CLASS_GET, .request = 0x86, .value = 0x0200, .index = 0x0202, .length = 1};
    const UvcRequest control_interface_request = {.request_type = CLASS_GET, .request = 0x81, .value = 0x0200, .index = 2, .length = 1};
    UvcResponse response;
    AnswerUvcRequest(&unit_request, STREAMING_INTERFACE, &response);
    assert(response.length < 0);
    AnswerUvcRequest(&control_interface_request, STREAMING_INTERFACE, &response);
    assert(response.length < 0);
}

/* An 80x60 canvas, fitted 4x onto the 320x240 frame */
#define CANVAS_WIDTH 80
#define CANVAS_HEIGHT 60
#define CANVAS_SCALE 4
#define FACE_LEFT 20
#define FACE_TOP 10
#define FACE_SIZE 40
#define MOUTH_LEFT 30
#define MOUTH_TOP 35
#define MOUTH_WIDTH 20
#define MOUTH_HEIGHT 10
#define MOUTH_CENTER_X ((MOUTH_LEFT + MOUTH_WIDTH / 2) * CANVAS_SCALE)
#define MOUTH_CENTER_Y ((MOUTH_TOP + MOUTH_HEIGHT / 2) * CANVAS_SCALE)

static const uint8_t WHITE[4] = {255, 255, 255, 255};
static const uint8_t BLACK[4] = {0, 0, 0, 255};
static const uint8_t RED[4] = {255, 0, 0, 255};
static const uint8_t BLUE[4] = {0, 0, 255, 255};
/* BT.601 limited range Y, U, Y, V of a pixel pair */
static const uint8_t WHITE_YUYV[4] = {235, 128, 235, 128};
static const uint8_t BLACK_YUYV[4] = {16, 128, 16, 128};
static const uint8_t RED_YUYV[4] = {81, 90, 81, 240};
static const uint8_t BLUE_YUYV[4] = {41, 240, 41, 110};
static const uint8_t STEELBLUE_YUYV[4] = {117, 159, 117, 98};

static uint8_t face_pixels[CANVAS_WIDTH * CANVAS_HEIGHT * 4];
static uint8_t mouth_pixels[CANVAS_WIDTH * CANVAS_HEIGHT * 4];
static RgbaImage face = {CANVAS_WIDTH, CANVAS_HEIGHT, face_pixels};
static RgbaImage mouth = {CANVAS_WIDTH, CANVAS_HEIGHT, mouth_pixels};

static uint8_t closed_frame[UVC_FRAME_BYTES];
static uint8_t half_frame[UVC_FRAME_BYTES];
static uint8_t open_frame[UVC_FRAME_BYTES];

static void paint(RgbaImage *image, int left, int top, int width, int height, const uint8_t rgba[4])
{
    int y = top;
    while (y < top + height) {
        int x = left;
        while (x < left + width) {
            memcpy(image->pixels + (y * image->width + x) * 4, rgba, 4);
            x++;
        }
        y++;
    }
}

/* A white face with a black mouth box, on transparent canvases */
static void drawTestCanvases(void)
{
    memset(face_pixels, 0, sizeof face_pixels);
    memset(mouth_pixels, 0, sizeof mouth_pixels);
    paint(&face, FACE_LEFT, FACE_TOP, FACE_SIZE, FACE_SIZE, WHITE);
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP, MOUTH_WIDTH, MOUTH_HEIGHT, BLACK);
}

static void fillTestFrames(void)
{
    assert(BuildScene(&face, &mouth, NULL, 100));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    FillMouthFrame(half_frame, CalcMouthSize(128, MOUTH_A));
    FillMouthFrame(open_frame, CalcMouthSize(255, MOUTH_A));
}

/* The pair holding even x */
static bool pairIs(const uint8_t *frame, int x, int y, const uint8_t yuyv[4])
{
    return memcmp(frame + (y * UVC_FRAME_WIDTH + x) * 2, yuyv, 4) == 0;
}

static uint8_t lumaAt(const uint8_t *frame, int x, int y)
{
    return frame[(y * UVC_FRAME_WIDTH + x) * 2];
}

static int countDark(const uint8_t *frame)
{
    int count = 0;
    int offset = 0;
    while (offset < UVC_FRAME_BYTES) {
        count += frame[offset] < 64;
        offset += 2;
    }
    return count;
}

static int darkRows(const uint8_t *frame)
{
    int rows = 0;
    int y = 0;
    while (y < UVC_FRAME_HEIGHT) {
        bool dark = false;
        int x = 0;
        while (x < UVC_FRAME_WIDTH) {
            dark = dark || lumaAt(frame, x, y) < 64;
            x++;
        }
        rows += dark;
        y++;
    }
    return rows;
}

static void testSceneRejectsMouthOfAnotherSize(void)
{
    static uint8_t small_pixels[40 * 30 * 4];
    RgbaImage small = {40, 30, small_pixels};
    drawTestCanvases();
    paint(&small, 0, 0, 10, 10, BLACK);
    assert(!BuildScene(&face, &small, NULL, 100));
}

static void testSceneRejectsTransparentMouth(void)
{
    drawTestCanvases();
    memset(mouth_pixels, 0, sizeof mouth_pixels);
    assert(!BuildScene(&face, &mouth, NULL, 100));
}

static void testPlainSceneIsSolidBackgroundAtAnyOpenness(void)
{
    BuildPlainScene();
    FillMouthFrame(open_frame, CalcMouthSize(255, MOUTH_I));
    int x = 0;
    while (x < UVC_FRAME_WIDTH) {
        int y = 0;
        while (y < UVC_FRAME_HEIGHT) {
            assert(pairIs(open_frame, x, y, STEELBLUE_YUYV));
            y++;
        }
        x += 2;
    }
}

static void testFaceFitsFrameOverDefaultBackground(void)
{
    drawTestCanvases();
    fillTestFrames();
    const int left = FACE_LEFT * CANVAS_SCALE;
    const int top = FACE_TOP * CANVAS_SCALE;
    const int right = left + FACE_SIZE * CANVAS_SCALE;
    const int bottom = top + FACE_SIZE * CANVAS_SCALE;
    assert(pairIs(closed_frame, left, top, WHITE_YUYV));
    assert(pairIs(closed_frame, right - 2, bottom - 1, WHITE_YUYV));
    assert(pairIs(closed_frame, left - 2, top, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, right, top, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, left, top - 1, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, left, bottom, STEELBLUE_YUYV));
}

static void testSquareFaceIsCenteredKeepingAspect(void)
{
    static uint8_t square_face_pixels[60 * 60 * 4];
    static uint8_t square_mouth_pixels[60 * 60 * 4];
    RgbaImage square_face = {60, 60, square_face_pixels};
    RgbaImage square_mouth = {60, 60, square_mouth_pixels};
    paint(&square_face, 0, 0, 60, 60, WHITE);
    paint(&square_mouth, 20, 40, 20, 5, BLACK);
    assert(BuildScene(&square_face, &square_mouth, NULL, 100));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    assert(pairIs(closed_frame, 38, 0, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, 40, 0, WHITE_YUYV));
    assert(pairIs(closed_frame, 278, UVC_FRAME_HEIGHT - 1, WHITE_YUYV));
    assert(pairIs(closed_frame, 280, 0, STEELBLUE_YUYV));
}

static void testSquareFaceShrinksAroundFrameCenter(void)
{
    static uint8_t square_face_pixels[60 * 60 * 4];
    static uint8_t square_mouth_pixels[60 * 60 * 4];
    RgbaImage square_face = {60, 60, square_face_pixels};
    RgbaImage square_mouth = {60, 60, square_mouth_pixels};
    paint(&square_face, 0, 0, 60, 60, WHITE);
    paint(&square_mouth, 20, 40, 20, 5, BLACK);
    assert(BuildScene(&square_face, &square_mouth, NULL, 50));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    assert(pairIs(closed_frame, 98, 60, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, 100, 60, WHITE_YUYV));
    assert(pairIs(closed_frame, 100, 59, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, 218, 179, WHITE_YUYV));
    assert(pairIs(closed_frame, 220, 179, STEELBLUE_YUYV));
    assert(pairIs(closed_frame, 218, 180, STEELBLUE_YUYV));
}

static void testFaceKeepsItsColor(void)
{
    drawTestCanvases();
    paint(&face, FACE_LEFT, FACE_TOP, 10, 10, RED);
    fillTestFrames();
    assert(pairIs(closed_frame, FACE_LEFT * CANVAS_SCALE, FACE_TOP * CANVAS_SCALE, RED_YUYV));
}

static void testHalfTransparentFaceBlendsWithBackground(void)
{
    static uint8_t black_pixels[4 * 3 * 4];
    RgbaImage black = {4, 3, black_pixels};
    const uint8_t half_white[4] = {255, 255, 255, 128};
    paint(&black, 0, 0, 4, 3, BLACK);
    drawTestCanvases();
    paint(&face, FACE_LEFT, FACE_TOP, 10, 10, half_white);
    assert(BuildScene(&face, &mouth, &black, 100));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    const uint8_t luma = lumaAt(closed_frame, FACE_LEFT * CANVAS_SCALE, FACE_TOP * CANVAS_SCALE);
    assert(luma > 120 && luma < 132);
}

/* 8x2 is wider than 4:3: the middle 2x2 is kept, red on the left, blue on the right */
static void testBackgroundImageCoversFrameCenterCropped(void)
{
    static uint8_t wide_pixels[8 * 2 * 4];
    RgbaImage wide = {8, 2, wide_pixels};
    paint(&wide, 0, 0, 4, 2, RED);
    paint(&wide, 4, 0, 4, 2, BLUE);
    drawTestCanvases();
    assert(BuildScene(&face, &mouth, &wide, 100));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    assert(pairIs(closed_frame, 0, 0, RED_YUYV));
    assert(pairIs(closed_frame, 158, UVC_FRAME_HEIGHT - 1, RED_YUYV));
    assert(pairIs(closed_frame, 160, 0, BLUE_YUYV));
    assert(pairIs(closed_frame, UVC_FRAME_WIDTH - 2, UVC_FRAME_HEIGHT - 1, BLUE_YUYV));
}

static void testMouthSizeSpansClosedLineToDrawnSize(void)
{
    drawTestCanvases();
    fillTestFrames();
    const MouthSize closed = CalcMouthSize(0, MOUTH_A);
    const MouthSize open = CalcMouthSize(255, MOUTH_A);
    assert(closed.width == MOUTH_WIDTH * CANVAS_SCALE && closed.height == 3);
    assert(open.width == MOUTH_WIDTH * CANVAS_SCALE && open.height == MOUTH_HEIGHT * CANVAS_SCALE);
}

static void testMouthShapeScalesSize(void)
{
    drawTestCanvases();
    fillTestFrames();
    const MouthSize spread = CalcMouthSize(255, MOUTH_I);
    const MouthSize rounded = CalcMouthSize(255, MOUTH_U);
    assert(spread.width == 112 && spread.height == 22);
    assert(rounded.width == 56 && rounded.height == 36);
}

static void testOpenMouthIsDrawnAtItsPlace(void)
{
    drawTestCanvases();
    fillTestFrames();
    const int left = MOUTH_LEFT * CANVAS_SCALE;
    const int top = MOUTH_TOP * CANVAS_SCALE;
    assert(pairIs(open_frame, MOUTH_CENTER_X, MOUTH_CENTER_Y, BLACK_YUYV));
    assert(pairIs(open_frame, left, top, BLACK_YUYV));
    assert(pairIs(open_frame, left - 2, top, WHITE_YUYV));
    assert(pairIs(open_frame, left + MOUTH_WIDTH * CANVAS_SCALE, top, WHITE_YUYV));
    assert(pairIs(open_frame, left, top - 1, WHITE_YUYV));
    assert(countDark(open_frame) == MOUTH_WIDTH * MOUTH_HEIGHT * CANVAS_SCALE * CANVAS_SCALE);
}

/* Faint lips around a thin dark line between them: squashed, the line must survive */
static void testClosedMouthKeepsItsDarkLine(void)
{
    const uint8_t faint[4] = {0, 0, 0, 40};
    drawTestCanvases();
    memset(mouth_pixels, 0, sizeof mouth_pixels);
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP, MOUTH_WIDTH, 1, faint);
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP + MOUTH_HEIGHT / 2, MOUTH_WIDTH, 1, BLACK);
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP + MOUTH_HEIGHT - 1, MOUTH_WIDTH, 1, faint);
    fillTestFrames();
    assert(darkRows(closed_frame) == 1);
    assert(darkRows(open_frame) == CANVAS_SCALE);
}

static void testMouthKeepsItsColor(void)
{
    drawTestCanvases();
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP, MOUTH_WIDTH, MOUTH_HEIGHT, RED);
    fillTestFrames();
    assert(pairIs(open_frame, MOUTH_CENTER_X, MOUTH_CENTER_Y, RED_YUYV));
}

static void testMouthGrowsWithOpenness(void)
{
    drawTestCanvases();
    fillTestFrames();
    assert(countDark(closed_frame) < countDark(half_frame));
    assert(countDark(half_frame) < countDark(open_frame));
}

static void testHalfTransparentMouthBlendsOverFace(void)
{
    const uint8_t half_black[4] = {0, 0, 0, 128};
    drawTestCanvases();
    paint(&mouth, MOUTH_LEFT, MOUTH_TOP, MOUTH_WIDTH, MOUTH_HEIGHT, half_black);
    fillTestFrames();
    const uint8_t luma = lumaAt(open_frame, MOUTH_CENTER_X, MOUTH_CENTER_Y);
    assert(luma > 120 && luma < 132);
}

static void testEasedMouthMovesPartWayToTarget(void)
{
    const MouthSize current = {.width = 10, .height = 10};
    const MouthSize target = {.width = 50, .height = 3};
    const MouthSize eased = EaseMouthSize(current, target);
    assert(eased.width > 10 && eased.width < 50);
    assert(eased.height < 10 && eased.height > 3);
}

static void testEasedMouthReachesTarget(void)
{
    MouthSize size = {.width = 10, .height = 10};
    const MouthSize target = {.width = 50, .height = 3};
    int frame = 0;
    while (frame < 15) {
        size = EaseMouthSize(size, target);
        frame++;
    }
    assert(size.width == target.width && size.height == target.height);
}

static void fillUniform(uint8_t *frame, uint8_t luma, uint8_t chroma)
{
    int offset = 0;
    while (offset < UVC_FRAME_BYTES) {
        frame[offset] = luma;
        frame[offset + 1] = chroma;
        offset += 2;
    }
}

static int countLit(const uint8_t *frame)
{
    int count = 0;
    int offset = 0;
    while (offset < UVC_FRAME_BYTES) {
        count += frame[offset] > 64;
        offset += 2;
    }
    return count;
}

static int litOfUniform(uint8_t luma)
{
    static uint8_t frame[UVC_FRAME_BYTES];
    static uint8_t ascii[UVC_FRAME_BYTES];
    fillUniform(frame, luma, 128);
    RenderAscii(ascii, frame);
    return countLit(ascii);
}

static void testAsciiDarkFrameIsBlank(void)
{
    assert(litOfUniform(16) == 0);
}

static void testAsciiBrighterCellsUseDenserGlyphs(void)
{
    assert(litOfUniform(80) > 0);
    assert(litOfUniform(80) < litOfUniform(160));
    assert(litOfUniform(160) < litOfUniform(235));
}

static void testAsciiGlyphsKeepCellColor(void)
{
    static uint8_t frame[UVC_FRAME_BYTES];
    static uint8_t ascii[UVC_FRAME_BYTES];
    fillUniform(frame, 200, 60);
    RenderAscii(ascii, frame);
    assert(ascii[1] == 60 && ascii[UVC_FRAME_BYTES - 1] == 60);
}

static void testAsciiMouthOpensAndBackgroundStays(void)
{
    static uint8_t closed[UVC_FRAME_BYTES];
    static uint8_t open[UVC_FRAME_BYTES];
    static uint8_t ascii[UVC_FRAME_BYTES];
    drawTestCanvases();
    assert(BuildScene(&face, &mouth, NULL, 100));
    FillAsciiMouthFrame(closed, CalcMouthSize(0, MOUTH_A));
    FillAsciiMouthFrame(open, CalcMouthSize(255, MOUTH_A));
    assert(memcmp(closed, open, UVC_FRAME_BYTES) != 0);
    FillMouthFrame(open_frame, CalcMouthSize(255, MOUTH_A));
    RenderAscii(ascii, open_frame);
    assert(memcmp(ascii, open, UVC_FRAME_BYTES) == 0);
}

static void testAsciiFollowsANewScene(void)
{
    static uint8_t frame[UVC_FRAME_BYTES];
    static uint8_t ascii[UVC_FRAME_BYTES];
    drawTestCanvases();
    assert(BuildScene(&face, &mouth, NULL, 100));
    FillAsciiMouthFrame(frame, CalcMouthSize(0, MOUTH_A));
    paint(&face, FACE_LEFT, FACE_TOP, FACE_SIZE, FACE_SIZE, RED);
    assert(BuildScene(&face, &mouth, NULL, 100));
    FillAsciiMouthFrame(frame, CalcMouthSize(0, MOUTH_A));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    RenderAscii(ascii, closed_frame);
    assert(memcmp(ascii, frame, UVC_FRAME_BYTES) == 0);
}

/* The face and its mouth move together; the background shows where the face was */
static void testMovedFaceCarriesItsMouth(void)
{
    static uint8_t moved_frame[UVC_FRAME_BYTES];
    drawTestCanvases();
    fillTestFrames();
    const FaceOffset offset = {4, 2};
    MoveFace(offset);
    FillMouthFrame(moved_frame, CalcMouthSize(255, MOUTH_A));
    const FaceOffset home = {0, 0};
    MoveFace(home);
    const int left = FACE_LEFT * CANVAS_SCALE;
    const int top = FACE_TOP * CANVAS_SCALE;
    const int size = FACE_SIZE * CANVAS_SCALE;
    int y = top;
    while (y < top + size) {
        const int row = (y * UVC_FRAME_WIDTH + left) * 2;
        const int moved_row = ((y + offset.y) * UVC_FRAME_WIDTH + left + offset.x) * 2;
        assert(memcmp(moved_frame + moved_row, open_frame + row, size * 2) == 0);
        y++;
    }
    assert(pairIs(moved_frame, left, top, STEELBLUE_YUYV));
    assert(pairIs(moved_frame, left + offset.x, top + offset.y, WHITE_YUYV));
}

static void testAsciiFollowsAMovedFace(void)
{
    static uint8_t frame[UVC_FRAME_BYTES];
    static uint8_t ascii[UVC_FRAME_BYTES];
    drawTestCanvases();
    assert(BuildScene(&face, &mouth, NULL, 100));
    FillAsciiMouthFrame(frame, CalcMouthSize(0, MOUTH_A));
    const FaceOffset offset = {-8, 8};
    MoveFace(offset);
    FillAsciiMouthFrame(frame, CalcMouthSize(0, MOUTH_A));
    FillMouthFrame(closed_frame, CalcMouthSize(0, MOUTH_A));
    const FaceOffset home = {0, 0};
    MoveFace(home);
    RenderAscii(ascii, closed_frame);
    assert(memcmp(ascii, frame, UVC_FRAME_BYTES) == 0);
}

static void testSwayGlidesToItsSpot(void)
{
    FaceSway sway = {0};
    /* 6 % 7 = 6 and (6 >> 8) % 7 = 0: the spot (3, -3) */
    FaceOffset offset = StepFaceSway(&sway, 3, 6);
    assert(abs(offset.x) <= 1 && abs(offset.y) <= 1);
    int frame = 1;
    while (frame < SWAY_TEST_FRAMES) {
        offset = StepFaceSway(&sway, 3, 6);
        frame++;
    }
    assert(offset.x == 3 && offset.y == -3);
}

static void testSwayStaysWithinAmplitude(void)
{
    FaceSway sway = {0};
    uint32_t random = 1;
    bool moved = false;
    int frame = 0;
    while (frame < 3000) {
        random = random * 1103515245u + 12345u;
        const FaceOffset offset = StepFaceSway(&sway, 2, random >> 1);
        assert(abs(offset.x) <= 2 && abs(offset.y) <= 2);
        moved = moved || offset.x != 0 || offset.y != 0;
        frame++;
    }
    assert(moved);
}

static void testZeroSwayKeepsFaceStill(void)
{
    FaceSway sway = {0};
    int frame = 0;
    while (frame < 300) {
        const FaceOffset offset = StepFaceSway(&sway, 0, (uint32_t)frame * 2654435761u);
        assert(offset.x == 0 && offset.y == 0);
        frame++;
    }
}

static void testAdcAtBiasIsSilence(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1500) == 0);
}

static void testAdcAboveBiasIsScaledTo16bit(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1600) == 1600);
    StartAdcBias(&bias, 1500);
    assert(ConvertAdcSample(&bias, 1400) == -1600);
}

static void testAdcBiasFollowsDrift(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 1500);
    int count = 0;
    while (count < 8000) {
        ConvertAdcSample(&bias, 1550);
        count++;
    }
    assert(ConvertAdcSample(&bias, 1550) == 0);
}

static void testAdcClampsToInt16(void)
{
    AdcBias bias;
    StartAdcBias(&bias, 0);
    assert(ConvertAdcSample(&bias, 4095) == 32767);
    StartAdcBias(&bias, 4095);
    assert(ConvertAdcSample(&bias, 0) == -32768);
}

/* Harmonics of f0 shaped by two formant resonances, like a sustained vowel */
static void synthesizeVowel(int16_t *frame, float f0, float f1, float f2, float amplitude)
{
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        float value = 0.0f;
        float harmonic = f0;
        while (harmonic < 4000.0f) {
            const float d1 = (harmonic - f1) / 100.0f;
            const float d2 = (harmonic - f2) / 120.0f;
            const float gain = (1.0f / (1.0f + d1 * d1) + 0.7f / (1.0f + d2 * d2)) * f0 / harmonic;
            value += gain * sinf(2.0f * 3.14159265f * harmonic * n / 8000.0f);
            harmonic += f0;
        }
        frame[n] = (int16_t)(amplitude * value);
        n++;
    }
}

/* Japanese adult formants (F1, F2) */
static const float FORMANTS[5][2] = {
    {750.0f, 1200.0f},  /* a */
    {300.0f, 2300.0f},  /* i */
    {330.0f, 1300.0f},  /* u */
    {500.0f, 1900.0f},  /* e */
    {500.0f, 850.0f},   /* o */
};

static Vowel classifySynthetic(const VowelFeature templates[VOWEL_COUNT], int vowel, float f0, float amplitude)
{
    int16_t frame[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(frame, f0, FORMANTS[vowel][0], FORMANTS[vowel][1], amplitude);
    VowelFeature feature;
    ExtractVowelFeature(frame, &feature);
    return ClassifyVowel(&feature, templates);
}

static void learnSyntheticTemplates(VowelFeature templates[VOWEL_COUNT])
{
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        int16_t frame[VOWEL_FRAME_SAMPLES];
        synthesizeVowel(frame, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 3000.0f);
        ExtractVowelFeature(frame, &templates[vowel]);
        vowel++;
    }
}

static int loudestBand(const VowelFeature *feature)
{
    int loudest = 0;
    int band = 1;
    while (band < VOWEL_BANDS) {
        if (feature->bands[band] > feature->bands[loudest]) {
            loudest = band;
        }
        band++;
    }
    return loudest;
}

static void testVowelFeaturePeaksAtToneBand(void)
{
    int16_t low[VOWEL_FRAME_SAMPLES];
    int16_t high[VOWEL_FRAME_SAMPLES];
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES) {
        low[n] = (int16_t)(8000.0f * sinf(2.0f * 3.14159265f * 400.0f * n / 8000.0f));
        high[n] = (int16_t)(8000.0f * sinf(2.0f * 3.14159265f * 2300.0f * n / 8000.0f));
        n++;
    }
    VowelFeature low_feature;
    VowelFeature high_feature;
    ExtractVowelFeature(low, &low_feature);
    ExtractVowelFeature(high, &high_feature);
    assert(loudestBand(&low_feature) < loudestBand(&high_feature));
}

static void testVowelFeatureIgnoresLoudness(void)
{
    int16_t quiet[VOWEL_FRAME_SAMPLES];
    int16_t loud[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(quiet, 120.0f, 750.0f, 1200.0f, 4000.0f);
    synthesizeVowel(loud, 120.0f, 750.0f, 1200.0f, 16000.0f);
    VowelFeature quiet_feature;
    VowelFeature loud_feature;
    ExtractVowelFeature(quiet, &quiet_feature);
    ExtractVowelFeature(loud, &loud_feature);
    int band = 0;
    while (band < VOWEL_BANDS) {
        assert(fabsf(quiet_feature.bands[band] - loud_feature.bands[band]) < 0.05f);
        band++;
    }
}

static void testVowelClassifiesAllFiveAtAnotherPitch(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        assert(classifySynthetic(templates, vowel, 150.0f, 2000.0f) == (Vowel)vowel);
        assert(classifySynthetic(templates, vowel, 100.0f, 6000.0f) == (Vowel)vowel);
        vowel++;
    }
}

static void testMouthShapeGroupsEWithIAndOWithU(void)
{
    assert(SelectMouthShapeOfVowel(VOWEL_A) == MOUTH_A);
    assert(SelectMouthShapeOfVowel(VOWEL_I) == MOUTH_I);
    assert(SelectMouthShapeOfVowel(VOWEL_E) == MOUTH_I);
    assert(SelectMouthShapeOfVowel(VOWEL_U) == MOUTH_U);
    assert(SelectMouthShapeOfVowel(VOWEL_O) == MOUTH_U);
}

static void feedSyntheticFrame(VowelTracker *tracker, int vowel, bool voiced, const VowelFeature templates[VOWEL_COUNT], int count, bool *completed)
{
    int16_t frame[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(frame, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 3000.0f);
    int n = 0;
    while (n < count) {
        *completed = TrackVowel(tracker, frame[n % VOWEL_FRAME_SAMPLES], voiced, templates);
        n++;
    }
}

static void testTrackerUpdatesShapeOncePerFrame(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_A};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES - 1, &completed);
    assert(!completed && tracker.shape == MOUTH_A);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, 1, &completed);
    assert(completed);
}

static void testTrackerSwitchesWithinFourFrames(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    assert(tracker.shape == MOUTH_I);
}

static void testTrackerIgnoresOneStrayFrame(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES * 4, &completed);
    feedSyntheticFrame(&tracker, VOWEL_I, true, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(tracker.shape == MOUTH_U);
    feedSyntheticFrame(&tracker, VOWEL_U, true, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(tracker.shape == MOUTH_U);
}

static void testTrackerKeepsShapeWhenSilent(void)
{
    VowelFeature templates[VOWEL_COUNT];
    learnSyntheticTemplates(templates);
    VowelTracker tracker = {.shape = MOUTH_U};
    bool completed;
    feedSyntheticFrame(&tracker, VOWEL_A, false, templates, VOWEL_FRAME_SAMPLES, &completed);
    assert(completed && tracker.shape == MOUTH_U);
}


/* Deterministic white noise */
static int16_t nextNoise(uint32_t *state, int amplitude)
{
    *state = *state * 1664525u + 1013904223u;
    return (int16_t)((int)(*state >> 16) % (2 * amplitude + 1) - amplitude);
}

#define ROOM_NOISE 600

static void feedSilence(VowelTracker *tracker, const VowelFeature templates[VOWEL_COUNT], uint32_t *state)
{
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES * 40) {
        TrackVowel(tracker, nextNoise(state, ROOM_NOISE), false, templates);
        n++;
    }
}

/* Like vcamera-calibrate: loud vowels in room noise, averaged over the tracker's features */
static void calibrateInNoise(VowelFeature templates[VOWEL_COUNT])
{
    static const VowelFeature none[VOWEL_COUNT];
    uint32_t state = 7;
    int vowel = 0;
    while (vowel < VOWEL_COUNT) {
        VowelTracker tracker = {.shape = MOUTH_A};
        feedSilence(&tracker, none, &state);
        int16_t voice[VOWEL_FRAME_SAMPLES];
        synthesizeVowel(voice, 120.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 4000.0f);
        VowelFeature sum = {{0}};
        int frame = 0;
        while (frame < 10) {
            int n = 0;
            while (n < VOWEL_FRAME_SAMPLES) {
                TrackVowel(&tracker, voice[n] + nextNoise(&state, ROOM_NOISE), true, none);
                n++;
            }
            int band = 0;
            while (band < VOWEL_BANDS) {
                sum.bands[band] += tracker.feature.bands[band] / 10.0f;
                band++;
            }
            frame++;
        }
        templates[vowel] = sum;
        vowel++;
    }
}

static MouthShape trackNormalVoice(const VowelFeature templates[VOWEL_COUNT], int vowel, MouthShape start)
{
    VowelTracker tracker = {.shape = start};
    uint32_t state = 1;
    feedSilence(&tracker, templates, &state);
    int16_t voice[VOWEL_FRAME_SAMPLES];
    synthesizeVowel(voice, 140.0f, FORMANTS[vowel][0], FORMANTS[vowel][1], 1350.0f);
    int n = 0;
    while (n < VOWEL_FRAME_SAMPLES * 10) {
        TrackVowel(&tracker, voice[n % VOWEL_FRAME_SAMPLES] + nextNoise(&state, ROOM_NOISE), true, templates);
        n++;
    }
    return tracker.shape;
}

/* Registered loud, spoken at a third of that: the high bands drown in noise and U looked like I */
static void testTrackerKeepsUAndIApartAtNormalVolume(void)
{
    VowelFeature templates[VOWEL_COUNT];
    calibrateInNoise(templates);
    assert(trackNormalVoice(templates, VOWEL_U, MOUTH_I) == MOUTH_U);
    assert(trackNormalVoice(templates, VOWEL_I, MOUTH_U) == MOUTH_I);
}

static void testConfigDefaults(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(strcmp(config.face, "/root/face.png") == 0);
    assert(strcmp(config.mouth, "/root/mouth.png") == 0);
    assert(config.background[0] == '\0');
    assert(config.face_scale_percent == 85);
    assert(config.ease_percent == DEFAULT_EASE_PERCENT);
    assert(!config.ascii);
    assert(config.mouth_scales[MOUTH_I].width_percent == 140 && config.mouth_scales[MOUTH_I].height_percent == 55);
}

static void testConfigSetsSway(void)
{
    Config config;
    SetDefaultConfig(&config, ".");
    assert(config.sway_pixels == DEFAULT_SWAY_PIXELS);
    assert(ReadConfigLine(&config, "sway=0", "."));
    assert(config.sway_pixels == 0);
    assert(!ReadConfigLine(&config, "sway=-1", "."));
    assert(config.sway_pixels == 0);
}

static void testConfigLineSetsNumber(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(ReadConfigLine(&config, "ease_percent=30\n", "/root"));
    assert(config.ease_percent == 30);
    assert(ReadConfigLine(&config, " face_scale = 70 ", "/root"));
    assert(config.face_scale_percent == 70);
    assert(ReadConfigLine(&config, "ascii=1", "/root"));
    assert(config.ascii);
}

static void testConfigSkipsCommentsAndBlankLines(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(ReadConfigLine(&config, "# ease_percent=30\n", "/root"));
    assert(ReadConfigLine(&config, "\n", "/root"));
    assert(ReadConfigLine(&config, "", "/root"));
    assert(config.ease_percent == DEFAULT_EASE_PERCENT);
}

static void testConfigRejectsUnknownKeyAndBadValue(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(!ReadConfigLine(&config, "speed=3", "/root"));
    assert(!ReadConfigLine(&config, "ease_percent", "/root"));
    assert(!ReadConfigLine(&config, "ease_percent=0", "/root"));
    assert(!ReadConfigLine(&config, "ease_percent=101", "/root"));
    assert(!ReadConfigLine(&config, "ease_percent=fast", "/root"));
    assert(!ReadConfigLine(&config, "face_scale=0", "/root"));
    assert(!ReadConfigLine(&config, "mouth_i=0x55", "/root"));
    assert(!ReadConfigLine(&config, "mouth_i=140", "/root"));
    assert(config.ease_percent == DEFAULT_EASE_PERCENT);
    assert(config.face_scale_percent == 85);
    assert(config.mouth_scales[MOUTH_I].width_percent == 140);
}

static void testConfigSetsMouthScale(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(ReadConfigLine(&config, "mouth_u=60x120", "/root"));
    assert(config.mouth_scales[MOUTH_U].width_percent == 60 && config.mouth_scales[MOUTH_U].height_percent == 120);
}

static void testConfigPathsAreRelativeToItsDirectory(void)
{
    Config config;
    SetDefaultConfig(&config, "/root");
    assert(ReadConfigLine(&config, "background=ajito.jpg", "/root"));
    assert(strcmp(config.background, "/root/ajito.jpg") == 0);
    assert(ReadConfigLine(&config, "face=/tmp/girl.png", "/root"));
    assert(strcmp(config.face, "/tmp/girl.png") == 0);
}

static void testConfigFileIsRead(void)
{
    const char text[] = "# trial\nease_percent=40\nbogus=1\nmouth=lips.png\n";
    writeFile("test.conf", (const uint8_t *)text, sizeof text - 1);
    Config config;
    assert(LoadConfig(&config, "test.conf"));
    assert(config.ease_percent == 40);
    assert(strcmp(config.mouth, "./lips.png") == 0);
    assert(strcmp(config.face, "./face.png") == 0);
    remove("test.conf");
}

static void testMissingConfigFileKeepsDefaults(void)
{
    Config config;
    assert(!LoadConfig(&config, "/nonexistent/vcamera.conf"));
    assert(config.ease_percent == DEFAULT_EASE_PERCENT);
    assert(strcmp(config.face, "/nonexistent/face.png") == 0);
}

static void testEasePercentSetsStep(void)
{
    const MouthSize current = {.width = 10, .height = 10};
    const MouthSize target = {.width = 50, .height = 3};
    SetMouthMotion(100, DEFAULT_MOUTH_SCALES);
    const MouthSize eased = EaseMouthSize(current, target);
    SetMouthMotion(DEFAULT_EASE_PERCENT, DEFAULT_MOUTH_SCALES);
    assert(eased.width == 50 && eased.height == 3);
}

static void testMouthScalesSetSize(void)
{
    drawTestCanvases();
    assert(BuildScene(&face, &mouth, NULL, 100));
    const MouthSize wide = CalcMouthSize(255, MOUTH_I);
    MouthScale scales[MOUTH_SHAPE_COUNT];
    memcpy(scales, DEFAULT_MOUTH_SCALES, sizeof scales);
    scales[MOUTH_I].width_percent = 70;
    SetMouthMotion(DEFAULT_EASE_PERCENT, scales);
    const MouthSize narrow = CalcMouthSize(255, MOUTH_I);
    SetMouthMotion(DEFAULT_EASE_PERCENT, DEFAULT_MOUTH_SCALES);
    assert(narrow.width * 2 == wide.width || narrow.width * 2 + 1 == wide.width);
    assert(narrow.height == wide.height);
}

int main(void)
{
    testTrackerKeepsUAndIApartAtNormalVolume();
    testTrackerUpdatesShapeOncePerFrame();
    testTrackerSwitchesWithinFourFrames();
    testTrackerIgnoresOneStrayFrame();
    testTrackerKeepsShapeWhenSilent();
    testVowelFeaturePeaksAtToneBand();
    testVowelFeatureIgnoresLoudness();
    testVowelClassifiesAllFiveAtAnotherPitch();
    testMouthShapeGroupsEWithIAndOWithU();
    testAdcAtBiasIsSilence();
    testAdcAboveBiasIsScaledTo16bit();
    testAdcBiasFollowsDrift();
    testAdcClampsToInt16();
    testOpennessSilenceIsZeroFullScaleIsMax();
    testOpennessBelowClosedDbIsZero();
    testVoiceLevelReaches63PercentAfterRiseTime();
    testVoiceLevelFallsSlowerThanItRises();
    testProbeGetCurAnswersFormat();
    testProbeMinMaxDefAnswerSameFormat();
    testProbeInfoAndLength();
    testProbeSetCurAcceptsData();
    testProbeShortRequestIsTruncated();
    testRequestsOutsideStreamingInterfaceStall();
    testSceneRejectsMouthOfAnotherSize();
    testSceneRejectsTransparentMouth();
    testPlainSceneIsSolidBackgroundAtAnyOpenness();
    testFaceFitsFrameOverDefaultBackground();
    testSquareFaceIsCenteredKeepingAspect();
    testSquareFaceShrinksAroundFrameCenter();
    testFaceKeepsItsColor();
    testHalfTransparentFaceBlendsWithBackground();
    testBackgroundImageCoversFrameCenterCropped();
    testMouthSizeSpansClosedLineToDrawnSize();
    testMouthShapeScalesSize();
    testOpenMouthIsDrawnAtItsPlace();
    testClosedMouthKeepsItsDarkLine();
    testMouthKeepsItsColor();
    testMouthGrowsWithOpenness();
    testHalfTransparentMouthBlendsOverFace();
    testAsciiDarkFrameIsBlank();
    testAsciiBrighterCellsUseDenserGlyphs();
    testAsciiGlyphsKeepCellColor();
    testAsciiMouthOpensAndBackgroundStays();
    testAsciiFollowsANewScene();
    testMovedFaceCarriesItsMouth();
    testAsciiFollowsAMovedFace();
    testSwayGlidesToItsSpot();
    testSwayStaysWithinAmplitude();
    testZeroSwayKeepsFaceStill();
    testEasedMouthMovesPartWayToTarget();
    testEasedMouthReachesTarget();
    testConfigDefaults();
    testConfigSetsSway();
    testConfigLineSetsNumber();
    testConfigSkipsCommentsAndBlankLines();
    testConfigRejectsUnknownKeyAndBadValue();
    testConfigSetsMouthScale();
    testConfigPathsAreRelativeToItsDirectory();
    testConfigFileIsRead();
    testMissingConfigFileKeepsDefaults();
    testEasePercentSetsStep();
    testMouthScalesSetSize();
    puts("ok");
    return 0;
}
