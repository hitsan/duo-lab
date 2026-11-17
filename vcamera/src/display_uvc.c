#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stb_image.h>
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
#include "config.h"
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
    MouthSize mouth;
    bool ascii;
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

static bool loadImage(const char *path, RgbaImage *image)
{
    int channels;
    image->pixels = stbi_load(path, &image->width, &image->height, &channels, 4);
    if (image->pixels == NULL) {
        fprintf(stderr, "%s: %s\n", path, stbi_failure_reason());
        return false;
    }
    return true;
}

/* vcamera.conf next to the executable: usb-uvc-ncm.sh starts us from wherever ssh landed */
static void loadConfig(Config *config)
{
    char path[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
    if (length < 0) {
        snprintf(path, sizeof path, "./%s", CONFIG_FILE_NAME);
    } else {
        path[length] = '\0';
        char *const slash = strrchr(path, '/');
        snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "%s", CONFIG_FILE_NAME);
    }
    if (!LoadConfig(config, path)) {
        fprintf(stderr, "%s not found; using the defaults\n", path);
    }
}

static bool loadScene(const Config *config)
{
    const bool has_background = config->background[0] != '\0';
    RgbaImage face = {0};
    RgbaImage mouth = {0};
    RgbaImage background = {0};
    bool built = loadImage(config->face, &face) && loadImage(config->mouth, &mouth) &&
                 (!has_background || loadImage(config->background, &background));
    if (built) {
        built = BuildScene(&face, &mouth, has_background ? &background : NULL, config->face_scale_percent);
        if (!built) {
            fprintf(stderr, "%s (%dx%d) and %s (%dx%d) must be the same size, and the mouth must not be transparent\n",
                    config->face, face.width, face.height, config->mouth, mouth.width, mouth.height);
        }
    }
    stbi_image_free(face.pixels);
    stbi_image_free(mouth.pixels);
    stbi_image_free(background.pixels);
    return built;
}

static bool openCamera(void)
{
    Config config;
    loadConfig(&config);
    SetMouthMotion(config.ease_percent, config.mouth_scales);
    camera.ascii = config.ascii;
    /* Exiting would leave the UVC requests unanswered and take NCM (ssh) down with them */
    if (!loadScene(&config)) {
        fputs("showing the plain background instead\n", stderr);
        BuildPlainScene();
    }
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
    if (camera.ascii) {
        FillAsciiMouthFrame(camera.buffers[index], camera.mouth);
    } else {
        FillMouthFrame(camera.buffers[index], camera.mouth);
    }
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

void ShowMouth(uint8_t openness, MouthShape shape)
{
    if (camera.video < 0 && !openCamera()) {
        exit(1);
    }
    camera.mouth = EaseMouthSize(camera.mouth, CalcMouthSize(openness, shape));
    handleEvents();
    if (camera.streaming) {
        refillBuffers();
    }
}
