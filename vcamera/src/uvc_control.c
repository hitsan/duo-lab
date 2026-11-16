#include <stdbool.h>
#include <string.h>
#include "face_art.h"
#include "uvc_control.h"

#define REQUEST_TYPE_MASK 0x60
#define REQUEST_TYPE_CLASS 0x20
#define SET_CUR 0x01
#define GET_LEN 0x85
#define GET_INFO 0x86
#define PROBE_CONTROL 1
#define COMMIT_CONTROL 2
/* UVC 1.0 probe/commit size; the gadget advertises bcdUVC 1.00 */
#define PROBE_SIZE 26
#define FRAME_INTERVAL 333333
#define MAX_PAYLOAD_TRANSFER_SIZE 1024
#define LUMA_INK 16
#define LUMA_PAPER 235
#define CHROMA_NEUTRAL 128
#define ART_LEFT ((UVC_FRAME_WIDTH - FACE_ART_SIZE) / 2)
#define ART_STRIDE (FACE_ART_SIZE / 8)
#define MOUTH_STRIDE ((FACE_ART_MOUTH_WIDTH + 7) / 8)
#define MOUTH_CENTER_X (ART_LEFT + FACE_ART_MOUTH_LEFT + FACE_ART_MOUTH_WIDTH / 2)
#define MOUTH_CENTER_Y (FACE_ART_MOUTH_TOP + FACE_ART_MOUTH_HEIGHT / 2)
/* Closed, the outline squashed to this many rows reads as a line */
#define MOUTH_CLOSED_HEIGHT 3
/* Share of the remaining distance covered per 30fps frame: about 3 frames to settle */
#define EASE_PERCENT 50

/* Fully open size per shape, in percent of the drawn mouth: I spread and low, U narrow */
static const struct {
    int width_percent;
    int height_percent;
} MOUTH_SCALES[MOUTH_SHAPE_COUNT] = {
    [MOUTH_A] = {100, 100},
    [MOUTH_I] = {140, 55},
    [MOUTH_U] = {70, 90},
};

static void putLe16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = value & 0xff;
    bytes[1] = value >> 8;
}

static void putLe32(uint8_t *bytes, uint32_t value)
{
    putLe16(bytes, value & 0xffff);
    putLe16(bytes + 2, value >> 16);
}

static void fillProbe(uint8_t *probe)
{
    memset(probe, 0, PROBE_SIZE);
    probe[2] = 1;
    probe[3] = 1;
    putLe32(probe + 4, FRAME_INTERVAL);
    putLe32(probe + 18, UVC_FRAME_BYTES);
    putLe32(probe + 22, MAX_PAYLOAD_TRANSFER_SIZE);
}

static bool isProbeOrCommit(const UvcRequest *request, int streaming_interface)
{
    const int selector = request->value >> 8;
    const int entity = request->index >> 8;
    const int interface = request->index & 0xff;
    return (request->request_type & REQUEST_TYPE_MASK) == REQUEST_TYPE_CLASS && entity == 0 &&
           interface == streaming_interface && (selector == PROBE_CONTROL || selector == COMMIT_CONTROL);
}

void AnswerUvcRequest(const UvcRequest *request, int streaming_interface, UvcResponse *response)
{
    memset(response, 0, sizeof *response);
    if (!isProbeOrCommit(request, streaming_interface)) {
        response->length = -1;
        return;
    }
    if (request->request == SET_CUR) {
        response->length = request->length;
        return;
    }
    if (request->request == GET_INFO) {
        response->data[0] = 0x03;
        response->length = 1;
        return;
    }
    if (request->request == GET_LEN) {
        putLe16(response->data, PROBE_SIZE);
        response->length = 2;
        return;
    }
    fillProbe(response->data);
    response->length = request->length < PROBE_SIZE ? request->length : PROBE_SIZE;
}

static bool bitAt(const uint8_t *bitmap, int stride, int x, int y)
{
    return (bitmap[y * stride + x / 8] & (0x80 >> (x % 8))) != 0;
}

/* (x, y) inside the drawn mouth stretched to size. Squashing ORs the source rows
   so the outline merges into a line instead of breaking into dots */
static bool mouthInkAt(int x, int y, MouthSize size)
{
    const int source_x = x * FACE_ART_MOUTH_WIDTH / size.width;
    int source_y = y * FACE_ART_MOUTH_HEIGHT / size.height;
    const int source_end = (y + 1) * FACE_ART_MOUTH_HEIGHT / size.height;
    do {
        if (bitAt(FACE_ART_MOUTH, MOUTH_STRIDE, source_x, source_y)) {
            return true;
        }
        source_y++;
    } while (source_y < source_end);
    return false;
}

static void putPixel(uint8_t *frame, int x, int y, uint8_t luma)
{
    uint8_t *const pixel = frame + (y * UVC_FRAME_WIDTH + x) * 2;
    pixel[0] = luma;
    pixel[1] = CHROMA_NEUTRAL;
}

/* The face on paper over the background, without the mouth; built once */
static void fillFaceFrame(uint8_t *frame)
{
    memcpy(frame, FACE_ART_BACKGROUND, UVC_FRAME_BYTES);
    int y = 0;
    while (y < FACE_ART_SIZE) {
        int x = 0;
        while (x < FACE_ART_SIZE) {
            if (bitAt(FACE_ART_MASK, ART_STRIDE, x, y)) {
                putPixel(frame, ART_LEFT + x, y, bitAt(FACE_ART, ART_STRIDE, x, y) ? LUMA_INK : LUMA_PAPER);
            }
            x++;
        }
        y++;
    }
}

MouthSize CalcMouthSize(uint8_t openness, MouthShape shape)
{
    const int open_height = FACE_ART_MOUTH_HEIGHT * MOUTH_SCALES[shape].height_percent / 100;
    const MouthSize size = {
        .width = FACE_ART_MOUTH_WIDTH * MOUTH_SCALES[shape].width_percent / 100,
        .height = MOUTH_CLOSED_HEIGHT + ((open_height - MOUTH_CLOSED_HEIGHT) * openness + 127) / 255,
    };
    return size;
}

/* Rounds away from current so the last pixel is reached instead of stalling */
static int easeToward(int current, int target)
{
    const int step = (target - current) * EASE_PERCENT;
    return current + (step > 0 ? step + 99 : step - 99) / 100;
}

MouthSize EaseMouthSize(MouthSize current, MouthSize target)
{
    const MouthSize eased = {
        .width = easeToward(current.width, target.width),
        .height = easeToward(current.height, target.height),
    };
    return eased;
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

void FillMouthFrame(uint8_t *frame, MouthSize size)
{
    static uint8_t face_frame[UVC_FRAME_BYTES];
    static bool face_frame_ready = false;
    if (!face_frame_ready) {
        fillFaceFrame(face_frame);
        face_frame_ready = true;
    }
    memcpy(frame, face_frame, UVC_FRAME_BYTES);
    const int left = MOUTH_CENTER_X - size.width / 2;
    const int top = MOUTH_CENTER_Y - size.height / 2;
    const int right = clamp(left + size.width, 0, UVC_FRAME_WIDTH);
    const int bottom = clamp(top + size.height, 0, UVC_FRAME_HEIGHT);
    int y = clamp(top, 0, UVC_FRAME_HEIGHT);
    while (y < bottom) {
        int x = clamp(left, 0, UVC_FRAME_WIDTH);
        while (x < right) {
            if (mouthInkAt(x - left, y - top, size)) {
                putPixel(frame, x, y, LUMA_INK);
            }
            x++;
        }
        y++;
    }
}
