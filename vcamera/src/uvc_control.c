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
/* AI cut-outs leave faint specks around; they must not stretch the mouth's box */
#define MOUTH_OPAQUE_ALPHA 16
/* Closed, the outline squashed to this many rows reads as a line */
#define MOUTH_CLOSED_HEIGHT 3
/* Share of the remaining distance covered per 30fps frame: about 3 frames to settle */
#define EASE_PERCENT 50

#define ASCII_CELL_SIZE 8
#define ASCII_DARK 16

/* " .:-=+*%#@", 7x7 in an 8x8 cell so glyphs stay apart, MSB left.
   Lit pixel counts rise strictly so brighter cells look denser */
static const uint8_t ASCII_GLYPHS[][ASCII_CELL_SIZE] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00},
    {0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x7C, 0x7C, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0xFE, 0x00, 0x00, 0xFE, 0x00, 0x00},
    {0x30, 0x30, 0xFE, 0xFE, 0x30, 0x30, 0x00, 0x00},
    {0x92, 0x54, 0x38, 0xFE, 0x38, 0x54, 0x92, 0x00},
    {0xE6, 0xEC, 0x1C, 0x38, 0x70, 0x6E, 0xCE, 0x00},
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    {0x7C, 0xFE, 0xDE, 0xDE, 0xDE, 0xC0, 0x7E, 0x00},
};
#define ASCII_GLYPH_COUNT ((int)(sizeof ASCII_GLYPHS / sizeof ASCII_GLYPHS[0]))

/* Fully open size per shape, in percent of the drawn mouth: I spread and low, U narrow */
static const struct {
    int width_percent;
    int height_percent;
} MOUTH_SCALES[MOUTH_SHAPE_COUNT] = {
    [MOUTH_A] = {100, 100},
    [MOUTH_I] = {140, 55},
    [MOUTH_U] = {70, 90},
};

typedef struct {
    int left;
    int top;
    int width;
    int height;
} Area;

/* Straight alpha */
typedef struct {
    int red;
    int green;
    int blue;
    int alpha;
} Color;

static const Color DEFAULT_BACKGROUND = {70, 130, 180, 255};  /* steelblue */

static struct {
    uint8_t face_frame[UVC_FRAME_BYTES];
    /* Y, U, V and alpha per pixel of mouth_area */
    uint8_t mouth[UVC_FRAME_WIDTH * UVC_FRAME_HEIGHT * 4];
    Area mouth_area;
    bool ascii_face_ready;
} scene;

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

static int mix(int under, int over, int alpha)
{
    return (under * (255 - alpha) + over * alpha + 127) / 255;
}

static int lumaOf(Color color)
{
    return (int)(16.5 + (65.481 * color.red + 128.553 * color.green + 24.966 * color.blue) / 255);
}

static int blueDifferenceOf(Color color)
{
    return (int)(128.5 + (-37.797 * color.red - 74.203 * color.green + 112.0 * color.blue) / 255);
}

static int redDifferenceOf(Color color)
{
    return (int)(128.5 + (112.0 * color.red - 93.786 * color.green - 18.214 * color.blue) / 255);
}

static bool inside(Area area, int x, int y)
{
    return x >= area.left && x < area.left + area.width && y >= area.top && y < area.top + area.height;
}

/* Mean of the source pixels that destination pixel (x, y) covers when source is stretched over destination */
static Color averageArea(const RgbaImage *image, Area source, Area destination, int x, int y)
{
    const int left = source.left + (x - destination.left) * source.width / destination.width;
    const int top = source.top + (y - destination.top) * source.height / destination.height;
    const int right = source.left + (x - destination.left + 1) * source.width / destination.width;
    const int bottom = source.top + (y - destination.top + 1) * source.height / destination.height;
    const int end_column = right > left ? right : left + 1;
    const int end_row = bottom > top ? bottom : top + 1;
    int64_t red = 0;
    int64_t green = 0;
    int64_t blue = 0;
    int64_t alpha = 0;
    int row = top;
    while (row < end_row) {
        const uint8_t *pixel = image->pixels + (row * image->width + left) * 4;
        int column = left;
        while (column < end_column) {
            red += pixel[0] * pixel[3];
            green += pixel[1] * pixel[3];
            blue += pixel[2] * pixel[3];
            alpha += pixel[3];
            pixel += 4;
            column++;
        }
        row++;
    }
    Color color = {.alpha = alpha / ((end_column - left) * (end_row - top))};
    if (alpha > 0) {
        color.red = red / alpha;
        color.green = green / alpha;
        color.blue = blue / alpha;
    }
    return color;
}

/* Where a width x height canvas lands when scaled to fit the frame, centered */
static Area fitArea(int width, int height)
{
    Area area = {.width = UVC_FRAME_WIDTH, .height = UVC_FRAME_HEIGHT};
    if (width * UVC_FRAME_HEIGHT > height * UVC_FRAME_WIDTH) {
        area.height = height * UVC_FRAME_WIDTH / width;
    } else {
        area.width = width * UVC_FRAME_HEIGHT / height;
    }
    area.left = (UVC_FRAME_WIDTH - area.width) / 2;
    area.top = (UVC_FRAME_HEIGHT - area.height) / 2;
    return area;
}

/* The centered part of a width x height image that, scaled, covers the frame */
static Area coverArea(int width, int height)
{
    Area area = {.width = width, .height = height};
    if (width * UVC_FRAME_HEIGHT > height * UVC_FRAME_WIDTH) {
        area.width = height * UVC_FRAME_WIDTH / UVC_FRAME_HEIGHT;
    } else {
        area.height = width * UVC_FRAME_HEIGHT / UVC_FRAME_WIDTH;
    }
    area.left = (width - area.width) / 2;
    area.top = (height - area.height) / 2;
    return area;
}

/* YUYV shares one U and V between two pixels */
static void putPair(uint8_t *pixel, Color left, Color right)
{
    const Color mean = {
        .red = (left.red + right.red) / 2,
        .green = (left.green + right.green) / 2,
        .blue = (left.blue + right.blue) / 2,
    };
    pixel[0] = lumaOf(left);
    pixel[1] = blueDifferenceOf(mean);
    pixel[2] = lumaOf(right);
    pixel[3] = redDifferenceOf(mean);
}

static void fillFaceFrame(const RgbaImage *face, Area face_area, const RgbaImage *background)
{
    const Area canvas = {.width = face->width, .height = face->height};
    const Area frame = {.width = UVC_FRAME_WIDTH, .height = UVC_FRAME_HEIGHT};
    const Area crop = background == NULL ? frame : coverArea(background->width, background->height);
    Color left = DEFAULT_BACKGROUND;
    int y = 0;
    while (y < UVC_FRAME_HEIGHT) {
        int x = 0;
        while (x < UVC_FRAME_WIDTH) {
            Color color = background == NULL ? DEFAULT_BACKGROUND : averageArea(background, crop, frame, x, y);
            if (inside(face_area, x, y)) {
                const Color over = averageArea(face, canvas, face_area, x, y);
                color.red = mix(color.red, over.red, over.alpha);
                color.green = mix(color.green, over.green, over.alpha);
                color.blue = mix(color.blue, over.blue, over.alpha);
            }
            if (x % 2 == 0) {
                left = color;
            } else {
                putPair(scene.face_frame + (y * UVC_FRAME_WIDTH + x - 1) * 2, left, color);
            }
            x++;
        }
        y++;
    }
}

/* Bounding box of the pixels at least MOUTH_OPAQUE_ALPHA */
static bool findOpaqueBox(const RgbaImage *image, Area *box)
{
    int left = image->width;
    int top = image->height;
    int right = -1;
    int bottom = -1;
    int y = 0;
    while (y < image->height) {
        int x = 0;
        while (x < image->width) {
            if (image->pixels[(y * image->width + x) * 4 + 3] >= MOUTH_OPAQUE_ALPHA) {
                left = x < left ? x : left;
                right = x > right ? x : right;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
            x++;
        }
        y++;
    }
    box->left = left;
    box->top = top;
    box->width = right - left + 1;
    box->height = bottom - top + 1;
    return right >= 0;
}

/* The mouth as Y, U, V and alpha per pixel, cut to the frame pixels its box lands on */
static void fillMouth(const RgbaImage *mouth, Area face_area, Area box)
{
    const Area canvas = {.width = mouth->width, .height = mouth->height};
    const int left = face_area.left + box.left * face_area.width / canvas.width;
    const int top = face_area.top + box.top * face_area.height / canvas.height;
    const int right = face_area.left + ((box.left + box.width) * face_area.width + canvas.width - 1) / canvas.width;
    const int bottom = face_area.top + ((box.top + box.height) * face_area.height + canvas.height - 1) / canvas.height;
    const Area area = {left, top, right - left, bottom - top};
    int y = 0;
    while (y < area.height) {
        int x = 0;
        while (x < area.width) {
            const Color color = averageArea(mouth, canvas, face_area, area.left + x, area.top + y);
            uint8_t *const pixel = scene.mouth + (y * area.width + x) * 4;
            pixel[0] = lumaOf(color);
            pixel[1] = blueDifferenceOf(color);
            pixel[2] = redDifferenceOf(color);
            pixel[3] = color.alpha;
            x++;
        }
        y++;
    }
    scene.mouth_area = area;
}

bool BuildScene(const RgbaImage *face, const RgbaImage *mouth, const RgbaImage *background)
{
    Area box;
    if (mouth->width != face->width || mouth->height != face->height || !findOpaqueBox(mouth, &box)) {
        return false;
    }
    const Area face_area = fitArea(face->width, face->height);
    fillFaceFrame(face, face_area, background);
    fillMouth(mouth, face_area, box);
    scene.ascii_face_ready = false;
    return true;
}

/* Pixel for (x, y) of the mouth stretched to size. Squashing keeps the most opaque of the merged rows,
   so the outline merges into a line instead of breaking into dots */
static const uint8_t *mouthPixelAt(int x, int y, MouthSize size)
{
    const int width = scene.mouth_area.width;
    const int height = scene.mouth_area.height;
    const int source_x = x * width / size.width;
    int source_y = y * height / size.height;
    const int source_end = (y + 1) * height / size.height;
    const uint8_t *most_opaque = scene.mouth + (source_y * width + source_x) * 4;
    source_y++;
    while (source_y < source_end) {
        const uint8_t *const pixel = scene.mouth + (source_y * width + source_x) * 4;
        if (pixel[3] > most_opaque[3]) {
            most_opaque = pixel;
        }
        source_y++;
    }
    return most_opaque;
}

MouthSize CalcMouthSize(uint8_t openness, MouthShape shape)
{
    const int open_height = scene.mouth_area.height * MOUTH_SCALES[shape].height_percent / 100;
    const MouthSize size = {
        .width = scene.mouth_area.width * MOUTH_SCALES[shape].width_percent / 100,
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

static int mouthTop(MouthSize size)
{
    return scene.mouth_area.top + scene.mouth_area.height / 2 - size.height / 2;
}

void FillMouthFrame(uint8_t *frame, MouthSize size)
{
    memcpy(frame, scene.face_frame, UVC_FRAME_BYTES);
    const int left = scene.mouth_area.left + scene.mouth_area.width / 2 - size.width / 2;
    const int top = mouthTop(size);
    const int right = clamp(left + size.width, 0, UVC_FRAME_WIDTH);
    const int bottom = clamp(top + size.height, 0, UVC_FRAME_HEIGHT);
    int y = clamp(top, 0, UVC_FRAME_HEIGHT);
    while (y < bottom) {
        int x = clamp(left, 0, UVC_FRAME_WIDTH);
        while (x < right) {
            const uint8_t *const mouth = mouthPixelAt(x - left, y - top, size);
            if (mouth[3] > 0) {
                uint8_t *const pixel = frame + (y * UVC_FRAME_WIDTH + x) * 2;
                pixel[0] = mix(pixel[0], mouth[0], mouth[3]);
                /* Even pixels carry U, odd ones V */
                pixel[1] = mix(pixel[1], mouth[1 + x % 2], mouth[3]);
            }
            x++;
        }
        y++;
    }
}

static void renderAsciiCell(uint8_t *ascii, const uint8_t *frame, int left, int top)
{
    int luma = 0;
    int u = 0;
    int v = 0;
    int row = 0;
    while (row < ASCII_CELL_SIZE) {
        const uint8_t *pixel = frame + ((top + row) * UVC_FRAME_WIDTH + left) * 2;
        int column = 0;
        while (column < ASCII_CELL_SIZE) {
            luma += pixel[0] + pixel[2];
            u += pixel[1];
            v += pixel[3];
            pixel += 4;
            column += 2;
        }
        row++;
    }
    luma /= ASCII_CELL_SIZE * ASCII_CELL_SIZE;
    u /= ASCII_CELL_SIZE * ASCII_CELL_SIZE / 2;
    v /= ASCII_CELL_SIZE * ASCII_CELL_SIZE / 2;
    const uint8_t *const glyph = ASCII_GLYPHS[luma * ASCII_GLYPH_COUNT / 256];
    /* Even sparse glyphs of dim cells stay visible on black */
    const uint8_t lit = 128 + luma / 2;
    row = 0;
    while (row < ASCII_CELL_SIZE) {
        uint8_t *out = ascii + ((top + row) * UVC_FRAME_WIDTH + left) * 2;
        int column = 0;
        while (column < ASCII_CELL_SIZE) {
            out[0] = glyph[row] & (0x80 >> column) ? lit : ASCII_DARK;
            out[1] = u;
            out[2] = glyph[row] & (0x40 >> column) ? lit : ASCII_DARK;
            out[3] = v;
            out += 4;
            column += 2;
        }
        row++;
    }
}

/* Every cell row that touches [top, bottom) */
static void renderAsciiRows(uint8_t *ascii, const uint8_t *frame, int top, int bottom)
{
    int cell_top = top - top % ASCII_CELL_SIZE;
    while (cell_top < bottom) {
        int left = 0;
        while (left < UVC_FRAME_WIDTH) {
            renderAsciiCell(ascii, frame, left, cell_top);
            left += ASCII_CELL_SIZE;
        }
        cell_top += ASCII_CELL_SIZE;
    }
}

void RenderAscii(uint8_t *ascii, const uint8_t *frame)
{
    renderAsciiRows(ascii, frame, 0, UVC_FRAME_HEIGHT);
}

void FillAsciiMouthFrame(uint8_t *frame, MouthSize size)
{
    static uint8_t composite[UVC_FRAME_BYTES];
    static uint8_t ascii_face[UVC_FRAME_BYTES];
    if (!scene.ascii_face_ready) {
        const MouthSize no_mouth = {0, 0};
        FillMouthFrame(composite, no_mouth);
        RenderAscii(ascii_face, composite);
        scene.ascii_face_ready = true;
    }
    FillMouthFrame(composite, size);
    memcpy(frame, ascii_face, UVC_FRAME_BYTES);
    const int top = mouthTop(size);
    renderAsciiRows(frame, composite, clamp(top, 0, UVC_FRAME_HEIGHT), clamp(top + size.height, 0, UVC_FRAME_HEIGHT));
}
