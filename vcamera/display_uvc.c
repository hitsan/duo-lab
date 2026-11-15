#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/usb/ch9.h>
#include <linux/usb/g_uvc.h>
#include <linux/videodev2.h>
#include "display.h"
#include "lipsync.h"
#include "uvc_control.h"

#define VIDEO_PATH "/dev/video0"
/* Created by usb-uvc-ncm.sh */
#define STREAMING_INTERFACE_PATH "/tmp/usb/usb_gadget/cvitek/functions/uvc.usb1/streaming/bInterfaceNumber"
#define BUFFER_COUNT 4

typedef struct {
    int video;
    int streaming_interface;
    bool streaming;
    void *buffers[BUFFER_COUNT];
    uint8_t openness;
} Camera;

static Camera camera = {.video = -1};

static bool readStreamingInterface(void)
{
    FILE *const file = fopen(STREAMING_INTERFACE_PATH, "r");
    if (file == NULL) {
        perror(STREAMING_INTERFACE_PATH);
        return false;
    }
    const bool read = fscanf(file, "%d", &camera.streaming_interface) == 1;
    fclose(file);
    return read;
}

static bool openCamera(void)
{
    if (!readStreamingInterface()) {
        return false;
    }
    camera.video = open(VIDEO_PATH, O_RDWR | O_NONBLOCK);
    if (camera.video < 0) {
        perror(VIDEO_PATH);
        return false;
    }
    const int event_types[] = {UVC_EVENT_SETUP, UVC_EVENT_DATA, UVC_EVENT_STREAMON, UVC_EVENT_STREAMOFF,
                               UVC_EVENT_DISCONNECT};
    unsigned int index = 0;
    while (index < sizeof event_types / sizeof event_types[0]) {
        struct v4l2_event_subscription subscription = {.type = event_types[index]};
        if (ioctl(camera.video, VIDIOC_SUBSCRIBE_EVENT, &subscription) < 0) {
            perror("VIDIOC_SUBSCRIBE_EVENT");
            return false;
        }
        index++;
    }
    return true;
}

static void answerSetup(const struct usb_ctrlrequest *control_request)
{
    const UvcRequest request = {
        .request_type = control_request->bRequestType,
        .request = control_request->bRequest,
        .value = control_request->wValue,
        .index = control_request->wIndex,
        .length = control_request->wLength,
    };
    UvcResponse response;
    AnswerUvcRequest(&request, camera.streaming_interface, &response);
    struct uvc_request_data data = {.length = response.length < 0 ? -EL2HLT : response.length};
    memcpy(data.data, response.data, sizeof data.data);
    if (ioctl(camera.video, UVCIOC_SEND_RESPONSE, &data) < 0) {
        perror("UVCIOC_SEND_RESPONSE");
    }
}

static bool queueFrame(unsigned int index)
{
    FillMouthFrame(camera.buffers[index], camera.openness);
    struct v4l2_buffer buffer = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
        .memory = V4L2_MEMORY_MMAP,
        .index = index,
        .bytesused = UVC_FRAME_BYTES,
    };
    if (ioctl(camera.video, VIDIOC_QBUF, &buffer) < 0) {
        perror("VIDIOC_QBUF");
        return false;
    }
    return true;
}

static void stopStreaming(void)
{
    if (!camera.streaming) {
        return;
    }
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(camera.video, VIDIOC_STREAMOFF, &type);
    unsigned int index = 0;
    while (index < BUFFER_COUNT) {
        munmap(camera.buffers[index], UVC_FRAME_BYTES);
        index++;
    }
    struct v4l2_requestbuffers release = {.count = 0, .type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_MMAP};
    ioctl(camera.video, VIDIOC_REQBUFS, &release);
    camera.streaming = false;
}

static bool mapBuffers(void)
{
    struct v4l2_requestbuffers request = {
        .count = BUFFER_COUNT, .type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(camera.video, VIDIOC_REQBUFS, &request) < 0 || request.count < BUFFER_COUNT) {
        perror("VIDIOC_REQBUFS");
        return false;
    }
    unsigned int index = 0;
    while (index < BUFFER_COUNT) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_MMAP, .index = index};
        if (ioctl(camera.video, VIDIOC_QUERYBUF, &buffer) < 0) {
            perror("VIDIOC_QUERYBUF");
            return false;
        }
        camera.buffers[index] =
            mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.video, buffer.m.offset);
        if (camera.buffers[index] == MAP_FAILED) {
            perror("mmap video buffer");
            return false;
        }
        index++;
    }
    return true;
}

/* The host committed to our only format; the gadget only needs the size before buffers are made */
static bool startStreaming(void)
{
    struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT};
    format.fmt.pix.width = UVC_FRAME_WIDTH;
    format.fmt.pix.height = UVC_FRAME_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.sizeimage = UVC_FRAME_BYTES;
    if (ioctl(camera.video, VIDIOC_S_FMT, &format) < 0) {
        perror("VIDIOC_S_FMT");
        return false;
    }
    if (!mapBuffers()) {
        return false;
    }
    unsigned int index = 0;
    while (index < BUFFER_COUNT) {
        if (!queueFrame(index)) {
            return false;
        }
        index++;
    }
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(camera.video, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }
    camera.streaming = true;
    return true;
}

static void handleEvents(void)
{
    struct v4l2_event event;
    while (ioctl(camera.video, VIDIOC_DQEVENT, &event) == 0) {
        const struct uvc_event *const uvc_event = (const struct uvc_event *)event.u.data;
        if (event.type == UVC_EVENT_SETUP) {
            answerSetup(&uvc_event->req);
        } else if (event.type == UVC_EVENT_STREAMON) {
            if (!startStreaming()) {
                stopStreaming();
            }
        } else if (event.type == UVC_EVENT_STREAMOFF || event.type == UVC_EVENT_DISCONNECT) {
            stopStreaming();
        }
    }
}

/* Requeue every buffer the gadget has sent, so the host always has a frame */
static void refillBuffers(void)
{
    struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_MMAP};
    while (ioctl(camera.video, VIDIOC_DQBUF, &buffer) == 0) {
        queueFrame(buffer.index);
    }
}

void ShowMouth(uint8_t openness)
{
    if (camera.video < 0 && !openCamera()) {
        exit(1);
    }
    camera.openness = openness;
    handleEvents();
    if (camera.streaming) {
        refillBuffers();
    }
}
