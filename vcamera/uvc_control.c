#include <stdbool.h>
#include <string.h>
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
typedef struct {
    uint8_t luma;
    uint8_t blue;
    uint8_t red;
} YuvColor;

/* BT.601 limited range */
static const YuvColor BACKGROUND = {205, 140, 116};
static const YuvColor FACE = {213, 108, 144};
static const YuvColor EYE = {52, 127, 130};
static const YuvColor MOUTH = {94, 118, 184};

#define FACE_X 160
#define FACE_Y 120
#define FACE_RADIUS 100
#define EYE_OFFSET_X 35
#define EYE_Y 95
#define EYE_RADIUS 10
#define MOUTH_Y 160
#define MOUTH_HALF_WIDTH 35
#define MOUTH_CLOSED_HALF_HEIGHT 2
#define MOUTH_OPEN_HALF_HEIGHT 30

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

static bool insideEllipse(int x, int y, int center_x, int center_y, int half_width, int half_height)
{
    const int dx = x - center_x;
    const int dy = y - center_y;
    return dx * dx * half_height * half_height + dy * dy * half_width * half_width
        <= half_width * half_width * half_height * half_height;
}

static YuvColor colorAt(int x, int y, int mouth_half_height)
{
    if (insideEllipse(x, y, FACE_X, MOUTH_Y, MOUTH_HALF_WIDTH, mouth_half_height)) {
        return MOUTH;
    }
    if (insideEllipse(x, y, FACE_X - EYE_OFFSET_X, EYE_Y, EYE_RADIUS, EYE_RADIUS)
        || insideEllipse(x, y, FACE_X + EYE_OFFSET_X, EYE_Y, EYE_RADIUS, EYE_RADIUS)) {
        return EYE;
    }
    if (insideEllipse(x, y, FACE_X, FACE_Y, FACE_RADIUS, FACE_RADIUS)) {
        return FACE;
    }
    return BACKGROUND;
}

void FillMouthFrame(uint8_t *frame, uint8_t openness)
{
    const int mouth_half_height = MOUTH_CLOSED_HALF_HEIGHT
        + ((MOUTH_OPEN_HALF_HEIGHT - MOUTH_CLOSED_HALF_HEIGHT) * openness + 127) / 255;
    int y = 0;
    while (y < UVC_FRAME_HEIGHT) {
        int x = 0;
        while (x < UVC_FRAME_WIDTH) {
            /* YUYV: two pixels share one chroma pair, taken from the left pixel */
            const YuvColor left = colorAt(x, y, mouth_half_height);
            const YuvColor right = colorAt(x + 1, y, mouth_half_height);
            uint8_t *pair = frame + (y * UVC_FRAME_WIDTH + x) * 2;
            pair[0] = left.luma;
            pair[1] = left.blue;
            pair[2] = right.luma;
            pair[3] = left.red;
            x += 2;
        }
        y++;
    }
}
