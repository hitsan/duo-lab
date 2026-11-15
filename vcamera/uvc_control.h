#ifndef UVC_CONTROL_H
#define UVC_CONTROL_H

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

void AnswerUvcRequest(const UvcRequest *request, int streaming_interface, UvcResponse *response);
/* YUYV face whose mouth opens from a line (openness 0) to its full shape (255) */
void FillMouthFrame(uint8_t *frame, uint8_t openness, MouthShape shape);

#endif
