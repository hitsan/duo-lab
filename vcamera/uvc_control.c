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
#define LUMA_BLACK 16
#define LUMA_WHITE 235
#define CHROMA_NEUTRAL 128

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

void FillMouthFrame(uint8_t *frame, int shape, int shape_count)
{
    const uint8_t luma = LUMA_BLACK + (LUMA_WHITE - LUMA_BLACK) * shape / (shape_count - 1);
    int offset = 0;
    while (offset < UVC_FRAME_BYTES) {
        frame[offset] = luma;
        frame[offset + 1] = CHROMA_NEUTRAL;
        offset += 2;
    }
}
