#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "printf.h"

#include "ansipixel.h"

// MORE DECLARATIONS

typedef uint16_t AP_CharPixel;
#define AP_CharPixel(up, down) (*(AP_CharPixel*)(AP_Color[2]){(up), (down)})
#define AP_CharPixel_data(p) ((AP_Color*)&p)

typedef uint64_t AP_CharPixelRgb;
#define AP_CharPixelRgb(up, down) \
    (*(AP_CharPixelRgb*)(AP_ColorRgb[2]){(up), (down)})
#define AP_CharPixelRgb_data(p) ((AP_ColorRgb*)&p)

typedef struct {
    bool updated;
    size_t height, width;
    size_t termheight, termwidth;
    AP_CharPixel* oldBuffer;
    AP_CharPixel* buffer;
} AP_Buffer;
#define AP_Buffer(b) ((AP_Buffer*)(b))
static void AP_Buffer_updateOldBuffer(AP_Buffer* buf);

typedef struct {
    bool updated;
    size_t height, width;
    size_t termheight, termwidth;
    AP_CharPixelRgb* oldBuffer;
    AP_CharPixelRgb* buffer;
} AP_BufferRgb;
#define AP_BufferRgb(b) ((AP_BufferRgb*)(b))
static void AP_BufferRgb_updateOldBuffer(AP_BufferRgb* buf);

typedef struct {
    enum {
        RESETCOLOR,
        MOVE, DRAW, DRAWRGB, PRINT, CLEAR, NL,
        SHOWCURSOR,
        SKIP, END,
    } type;
    union {
        int RESETCOLOR;
        struct { size_t y, x; } MOVE;
        AP_CharPixel DRAW;
        AP_CharPixelRgb DRAWRGB;
        int PRINT; // value not used
        int CLEAR; // value not used
        int NL; // value not used
        bool SHOWCURSOR; // show or hide cursor
        int SKIP; // value not used
        int END; // value not used
    };
} AP_DrawCommand;
#define AP_DrawCommand(t, ...) ((AP_DrawCommand){.type = t, .t = __VA_ARGS__})
// unowned string buffer
// return NULL if strbuf size not large enough
static char* AP_DrawCommand_ansiSequence(
    AP_DrawCommand* command, char* strbuf, size_t size);
#define CSI "\e["
#define HALFBLOCK "▀"

// array has to be freed
// end of array indicated by AP_DrawCommand.type == END
static AP_DrawCommand* AP_DrawCommand_compileCommand(AP_Buffer* buf);
static AP_DrawCommand* AP_DrawCommand_compileCommandRgb(AP_BufferRgb* buf);
static AP_DrawCommand* AP_DrawCommand_optimizeCommand(AP_DrawCommand* commands);

#define flushprint(str, len) \
    do { \
        char* s = (str); \
        long long l = (len); \
        long long i = 0; \
        while (l - i > 0) { \
            int a = write(STDOUT_FILENO, s + i, l - i); \
            if (a == -1) { \
                continue; \
            } \
            i += a; \
        } \
    } while (0)

// IMPLEMENTATIONS

struct AP_Buffer* AP_Buffer_new(size_t height, size_t width) {
    AP_Buffer* b = malloc(sizeof(*b));
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    size_t l = (height/2 + height%2)*width;
    AP_CharPixel* oldBuffer = malloc(l * sizeof(AP_CharPixel));
    memset(oldBuffer, UINT8_MAX, l * sizeof(AP_CharPixel));
    (*b) = (AP_Buffer){
        .updated = true,
        .height = height,
        .width = width,
        .termheight = w.ws_row,
        .termwidth = w.ws_col,
        .oldBuffer = oldBuffer,
        .buffer = calloc(l, sizeof(AP_CharPixel)),
    };

    return (struct AP_Buffer*)b;
}

void AP_Buffer_del(struct AP_Buffer* buf) {
    AP_Buffer* buffer = AP_Buffer(buf);
    free(buffer->oldBuffer);
    free(buffer->buffer);
    free(buffer);
}

AP_Color AP_Buffer_getPixel(struct AP_Buffer* buf, size_t y, size_t x) {
    AP_Buffer* buffer = AP_Buffer(buf);
    size_t index = (y / 2) * buffer->width + x;
    bool subpixel = y % 2;
    AP_CharPixel* charPixel = &buffer->buffer[index];
    return AP_CharPixel_data(*charPixel)[subpixel];
}

void AP_Buffer_setPixel(
    struct AP_Buffer* buf,
    size_t y,
    size_t x,
    AP_Color color)
{
    AP_Buffer* buffer = AP_Buffer(buf);
    if (y >= buffer->height || x >= buffer->width) {
        return;
    }

    size_t index = (y / 2) * buffer->width + x;
    bool subpixel = y % 2;
    AP_CharPixel* charPixel = &buffer->buffer[index];
    AP_Color originalColor = AP_CharPixel_data(*charPixel)[subpixel];
    AP_CharPixel_data(*charPixel)[subpixel] = color;
    buffer->updated = buffer->updated || originalColor != color;
}

void AP_Buffer_draw(struct AP_Buffer* buf) {
    AP_Buffer* buffer = AP_Buffer(buf);
    if (!buffer->updated) {
        return;
    }

    AP_DrawCommand* commands = AP_DrawCommand_compileCommand(buffer);
    commands = AP_DrawCommand_optimizeCommand(commands);

    const size_t deltaCapacity = 1024;
    size_t capacity = 1024;
    size_t len = 0;
    char* strbuf = calloc(capacity, 1);
    char* strbuf_toAppend = strbuf;
    for (AP_DrawCommand* c = commands; c->type != END; c++) {
        strbuf_toAppend = AP_DrawCommand_ansiSequence(
            c, strbuf_toAppend, capacity - len);
        if (!strbuf_toAppend) {
            strbuf = realloc(strbuf, capacity += deltaCapacity);
            strbuf_toAppend = strbuf + len;
            c--;
            continue;
        }
        len = strbuf_toAppend - strbuf;
    }

    flushprint(strbuf, len);

    free(strbuf);
    free(commands);
    AP_Buffer_updateOldBuffer(buffer);
}

static void AP_Buffer_updateOldBuffer(AP_Buffer* buf) {
    if (buf->updated) {
        memcpy(
            buf->oldBuffer,
            buf->buffer,
            (buf->height/2 + buf->height%2) * buf->width *
                sizeof(*buf->oldBuffer));
    }
}

struct AP_BufferRgb* AP_BufferRgb_new(size_t height, size_t width) {
    AP_BufferRgb* b = malloc(sizeof(*b));
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    (*b) = (AP_BufferRgb){
        .updated = false,
        .height = height,
        .width = width,
        .termheight = w.ws_row,
        .termwidth = w.ws_col,
        .oldBuffer = calloc(
            (height/2 + height%2)*width, sizeof(AP_CharPixelRgb)),
        .buffer = calloc(
            (height/2 + height%2)*width, sizeof(AP_CharPixelRgb)),
    };

    return (struct AP_BufferRgb*)b;
}

void AP_BufferRgb_del(struct AP_BufferRgb* buf) {
    AP_BufferRgb* buffer = AP_BufferRgb(buf);
    free(buffer->oldBuffer);
    free(buffer->buffer);
    free(buffer);
}

AP_ColorRgb AP_BufferRgb_getPixel(
    struct AP_BufferRgb* buf,
    size_t y,
    size_t x)
{
    AP_BufferRgb* buffer = AP_BufferRgb(buf);
    size_t index = (y / 2) * buffer->width + x;
    bool subpixel = y % 2;
    AP_CharPixelRgb* charPixel = &buffer->buffer[index];
    return AP_CharPixelRgb_data(*charPixel)[subpixel];
}

void AP_BufferRgb_setPixel(
    struct AP_BufferRgb* buf,
    size_t y,
    size_t x,
    AP_ColorRgb color)
{
    AP_BufferRgb* buffer = AP_BufferRgb(buf);
    if (y >= buffer->height || x >= buffer->width) {
        return;
    }

    size_t index = (y / 2) * buffer->width + x;
    bool subpixel = y % 2;
    AP_CharPixelRgb* charPixel = &buffer->buffer[index];
    AP_ColorRgb originalColor = AP_CharPixelRgb_data(*charPixel)[subpixel];
    AP_CharPixelRgb_data(*charPixel)[subpixel] = color;
    buffer->updated = buffer->updated || originalColor != color;
}

void AP_BufferRgb_draw(struct AP_BufferRgb* buf) {
    AP_BufferRgb* buffer = AP_BufferRgb(buf);
    if (!buffer->updated) {
        return;
    }

    AP_DrawCommand* commands = AP_DrawCommand_compileCommandRgb(buffer);
    commands = AP_DrawCommand_optimizeCommand(commands);

    const size_t deltaCapacity = 1024;
    size_t capacity = 1024;
    size_t len = 0;
    char* strbuf = calloc(capacity, 1);
    char* strbuf_toAppend = strbuf;
    for (AP_DrawCommand* c = commands; c->type != END; c++) {
        strbuf_toAppend = AP_DrawCommand_ansiSequence(
            c, strbuf_toAppend, capacity - len);
        if (!strbuf_toAppend) {
            strbuf = realloc(strbuf, capacity += deltaCapacity);
            strbuf_toAppend = strbuf + len;
            c--;
        }
        len = strbuf_toAppend - strbuf;
    }

    flushprint(strbuf, len);

    free(strbuf);
    free(commands);
    AP_BufferRgb_updateOldBuffer(buffer);
}

static void AP_BufferRgb_updateOldBuffer(AP_BufferRgb* buf) {
    if (buf->updated) {
        memcpy(
            buf->oldBuffer,
            buf->buffer,
            (buf->height/2 + buf->height%2) * buf->width *
                sizeof(*buf->oldBuffer));
    }
}

static int size_t_digits (size_t n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    if (n < 10000) return 4;
    if (n < 100000) return 5;
    if (n < 1000000) return 6;
    if (n < 10000000) return 7;
    if (n < 100000000) return 8;
    if (n < 1000000000) return 9;
    if (n < 10000000000) return 10;
    if (n < 100000000000) return 11;
    if (n < 1000000000000) return 12;
    if (n < 10000000000000) return 13;
    if (n < 100000000000000) return 14;
    if (n < 1000000000000000) return 15;
    if (n < 10000000000000000) return 16;
    if (n < 100000000000000000) return 17;
    if (n < 1000000000000000000) return 18;
    if (n < 10000000000000000000U) return 19;
    return 20;
}

static char* AP_DrawCommand_ansiSequence(
    AP_DrawCommand* command,
    char* strbuf,
    size_t size)
{
    switch (command->type) {
        case RESETCOLOR: {
            // CSI 0m
            size_t len = sizeof(CSI) + 2;
            if (len > size) {
                return NULL;
            }
            strcpy(strbuf, CSI "0m");
            return strbuf + len - 1;
        }
        case MOVE: {
            // CSI {y};{x}H
            size_t len = sizeof(CSI) +
                size_t_digits(command->MOVE.y + 1) +
                1 +
                size_t_digits(command->MOVE.x + 1) +
                1;
            if (len > size) {
                return NULL;
            }
            sprintf(strbuf, CSI "%zu;%zuH",
                command->MOVE.y + 1, command->MOVE.x + 1);
            return strbuf + len - 1;
        }
        case DRAW: {
            // foreground: CSI 38;5;{n}m
            // background: CSI 48;5;{n}m
            size_t len = (sizeof(CSI) - 1 + 6) * 2 +
                size_t_digits(AP_CharPixel_data(command->DRAW)[0]) +
                size_t_digits(AP_CharPixel_data(command->DRAW)[1]) +
                sizeof(HALFBLOCK);
            if (len > size) {
                return NULL;
            }
            sprintf(strbuf, CSI "38;5;%um" CSI "48;5;%um" HALFBLOCK,
                AP_CharPixel_data(command->DRAW)[0],
                AP_CharPixel_data(command->DRAW)[1]);
            return strbuf + len - 1;
        }
        case DRAWRGB: {
            // foreground: CSI 38;2;{r};{g};{b}m
            // background: CSI 48;2;{r};{g};{b}m
            AP_ColorRgb front = AP_CharPixelRgb_data(command->DRAWRGB)[0];
            AP_ColorRgb back = AP_CharPixelRgb_data(command->DRAWRGB)[1];
            size_t len = (sizeof(CSI) - 1 + 8) * 2 +
                size_t_digits(AP_ColorRgb_r(front)) +
                size_t_digits(AP_ColorRgb_g(front)) +
                size_t_digits(AP_ColorRgb_b(front)) +
                size_t_digits(AP_ColorRgb_r(back)) +
                size_t_digits(AP_ColorRgb_g(back)) +
                size_t_digits(AP_ColorRgb_b(back)) +
                sizeof(HALFBLOCK);
            if (len > size) {
                return NULL;
            }
            sprintf(strbuf, CSI "38;2;%u;%u;%um" CSI "48;2;%u;%u;%um" HALFBLOCK,
                AP_ColorRgb_r(front),
                AP_ColorRgb_g(front),
                AP_ColorRgb_b(front),
                AP_ColorRgb_r(back),
                AP_ColorRgb_g(back),
                AP_ColorRgb_b(back));
            return strbuf + len - 1;
        }
        case PRINT: {
            size_t len = sizeof(HALFBLOCK);
            if (len > size) {
                return NULL;
            }
            strcpy(strbuf, HALFBLOCK);
            return strbuf + len - 1;
        }
        case CLEAR: {
            // clear whole screen: CSI 2J 
            size_t len = sizeof(CSI) + 2;
            if (len > size) {
                return NULL;
            }
            strcpy(strbuf, CSI "2J");
            return strbuf + len - 1;
        }
        case NL: {
            // cursor go to new line first column: CSI E
            size_t len = sizeof(CSI) + 1;
            if (len > size) {
                return NULL;
            }
            strcpy(strbuf, CSI "E");
            return strbuf + len - 1;
        }
        case SHOWCURSOR: {
            // show: CSI ?25h
            // hide: CSI ?25l
            size_t len = sizeof(CSI) + 4;
            if (len > size) {
                return NULL;
            }
            sprintf(strbuf, CSI "?25%c", command->SHOWCURSOR ? 'h' : 'l');
            return strbuf + len - 1;
        }
        case SKIP:
        case END: {
            return strbuf;
        }
    }
    return strbuf;
}

static AP_DrawCommand* AP_DrawCommand_compileCommand(AP_Buffer* buf) {
    #define resize() \
        do { \
            if (len >= capacity) { \
                data = realloc( \
                    data, (capacity += deltaCapacity) * sizeof(*data)); \
            } \
        } while(0)

    const size_t deltaCapacity = 100;
    size_t capacity = deltaCapacity;
    size_t len = 0;
    AP_DrawCommand* data = malloc(capacity * sizeof(*data));

    data[len++] = AP_DrawCommand(MOVE, { 0, 0 });
    for (int i = 0; i < buf->height / 2 + buf->height % 2; i++) {
        for (int j = 0; j < buf->width; j++) {
            if (i >= buf->termheight || j >= buf->termwidth) {
                break;
            }

            size_t index = i * buf->width + j;
            // This conditional causes initial black screen not to be drawn
            // if (buf->oldBuffer[index] == buf->buffer[index]) {
            //     continue;
            // }
            if (buf->oldBuffer[index] == buf->buffer[index] &&
                buf->oldBuffer[index] != UINT16_MAX) {
                continue;
            }
            resize();
            data[len++] = AP_DrawCommand(MOVE, { i, j });
            resize();
            data[len++] = AP_DrawCommand(DRAW, buf->buffer[index]);
        }
    }

    data[len] = AP_DrawCommand(END, 0);
    return data;

    #undef resize
}

static AP_DrawCommand* AP_DrawCommand_compileCommandRgb(AP_BufferRgb* buf) {
    #define resize() \
        do { \
            if (len >= capacity) { \
                data = realloc( \
                    data, (capacity += deltaCapacity) * sizeof(*data)); \
            } \
        } while(0)

    const size_t deltaCapacity = 100;
    size_t capacity = deltaCapacity;
    size_t len = 0;
    AP_DrawCommand* data = malloc(capacity * sizeof(*data));

    data[len++] = AP_DrawCommand(MOVE, { 0, 0 });
    for (int i = 0; i < buf->height / 2 + buf->height % 2; i++) {
        for (int j = 0; j < buf->width; j++) {
            if (i >= buf->termheight || j >= buf->termwidth) {
                break;
            }

            size_t index = i * buf->width + j;
            if (buf->oldBuffer[index] == buf->buffer[index]) {
                continue;
            }
            resize();
            data[len++] = AP_DrawCommand(MOVE, { i, j });
            resize();
            data[len++] = AP_DrawCommand(DRAWRGB, buf->buffer[index]);
        }
    }

    data[len] = AP_DrawCommand(END, 0);
    return data;

    #undef resize
}

// Rules:
// 1. Consecutive draws on the same line remove MOVE
// 2. Replace DRAW with PRINT if same color
// 
// TODO: More rules?
// FIXME: Is this really useful? Benchmarks shows seems not that useful
static AP_DrawCommand* AP_DrawCommand_optimizeCommand(
    AP_DrawCommand* commands)
{
    struct Pos {
        size_t y, x;
    } lastMovPos = { 0, 0 };
    struct LastCharPixel {
        bool init;
        AP_CharPixel color;
    } lastCharPixel = { .init = false };
    struct LastCharPixelRgb {
        bool init;
        AP_CharPixelRgb color;
    } lastCharPixelRgb = { .init = false };

    for (AP_DrawCommand* c = commands; c->type != END; c++) {
        switch (c->type) {
            case MOVE: {
                if (c->MOVE.y == lastMovPos.y &&
                    c->MOVE.x == lastMovPos.x + 1)
                {
                    lastMovPos = *(struct Pos*)&c->MOVE;
                    *c = AP_DrawCommand(SKIP, 0);
                    break;
                }
                if (c->MOVE.x == 0 && c->MOVE.y - lastMovPos.y == 1) {
                    lastMovPos = *(struct Pos*)&c->MOVE;
                    *c = AP_DrawCommand(NL, 0);
                    break;
                }
                break;
            }
            case DRAW: {
                if (!lastCharPixel.init) {
                    lastCharPixel.color = c->DRAW;
                    lastCharPixelRgb.init = true;
                    break;
                }
                if (lastCharPixel.color == c->DRAW) {
                    *c = AP_DrawCommand(PRINT, 0);
                    break;
                }
                lastCharPixel.color = c->DRAW;
                break;
            }
            case DRAWRGB: {
                if (!lastCharPixelRgb.init) {
                    lastCharPixelRgb.color = c->DRAWRGB;
                    lastCharPixelRgb.init = true;
                    break;
                }
                if (lastCharPixelRgb.color == c->DRAWRGB) {
                    *c = AP_DrawCommand(PRINT, 0);
                    break;
                }
                lastCharPixelRgb.color = c->DRAWRGB;
                break;
            }
            default:
                continue;
        }
    }
    return commands;
}

void AP_clearScreen(struct AP_Buffer* buf) {
    char sequence[5];
    AP_DrawCommand_ansiSequence(&AP_DrawCommand(CLEAR, 0), sequence, 5);
    flushprint(sequence, sizeof(sequence));

    if (!buf) { return; }

    AP_Buffer* b = AP_Buffer(buf);
    b->updated = true;
    free(b->oldBuffer);
    size_t h = b->height;
    size_t w = b->width;
    b->oldBuffer = calloc((h/2 + h%2)*w, sizeof(AP_CharPixel));
}

void AP_clearScreenRgb(struct AP_BufferRgb* buf) {
    char sequence[5];
    AP_DrawCommand_ansiSequence(&AP_DrawCommand(CLEAR, 0), sequence, 5);
    flushprint(sequence, sizeof(sequence));

    if (!buf) { return; }

    AP_BufferRgb* b = AP_BufferRgb(buf);
    b->updated = true;
    free(b->oldBuffer);
    size_t h = b->height;
    size_t w = b->width;
    b->oldBuffer = calloc((h/2 + h%2)*w, sizeof(AP_CharPixelRgb));
}

void AP_resettextcolor() {
    char sequence[5];
    AP_DrawCommand_ansiSequence(&AP_DrawCommand(RESETCOLOR, 0), sequence, 5);
    flushprint(sequence, sizeof(sequence));
}

void AP_showcursor(bool show) {
    char sequence[7];
    AP_DrawCommand_ansiSequence(&AP_DrawCommand(SHOWCURSOR, show), sequence, 7);
    flushprint(sequence, sizeof(sequence));
}

void AP_move(size_t y, size_t x) {
    size_t len = sizeof(CSI) +
        size_t_digits(y + 1) + 1 +
        size_t_digits(x + 1) + 1;
    char sequence[len];
    AP_DrawCommand_ansiSequence(&AP_DrawCommand(MOVE, { y, x }), sequence, len);
    flushprint(sequence, sizeof(sequence));
}

// algorithm from tmux
#define COLOUR_FLAG_256 0x01000000

static int colour_dist_sq(int R, int G, int B, int r, int g, int b) {
    return ((R - r) * (R - r) + (G - g) * (G - g) + (B - b) * (B - b));
}

static int colour_to_6cube(int v) {
    if (v < 48) return (0);
    if (v < 114) return (1);
    return ((v - 35) / 40);
}

AP_Color AP_rgbTo256(AP_ColorRgb rgb) {
    uint8_t r = AP_ColorRgb_r(rgb);
    uint8_t g = AP_ColorRgb_g(rgb);
    uint8_t b = AP_ColorRgb_b(rgb);

    static const int q2c[6] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };
    int qr, qg, qb, cr, cg, cb, d, idx;
    int grey_avg, grey_idx, grey;

    /* Map RGB to 6x6x6 cube. */
    qr = colour_to_6cube(r); cr = q2c[qr];
    qg = colour_to_6cube(g); cg = q2c[qg];
    qb = colour_to_6cube(b); cb = q2c[qb];

    /* If we have hit the colour exactly, return early. */
    if (cr == r && cg == g && cb == b) {
        return ((16 + (36 * qr) + (6 * qg) + qb) | COLOUR_FLAG_256);
    }

    /* Work out the closest grey (average of RGB). */
    grey_avg = (r + g + b) / 3;
    if (grey_avg > 238) grey_idx = 23;
    else grey_idx = (grey_avg - 3) / 10;
    grey = 8 + (10 * grey_idx);

    /* Is grey or 6x6x6 colour closest? */
    d = colour_dist_sq(cr, cg, cb, r, g, b);
    if (colour_dist_sq(grey, grey, grey, r, g, b) < d) idx = 232 + grey_idx;
    else idx = 16 + (36 * qr) + (6 * qg) + qb;
    return (idx | COLOUR_FLAG_256);
}
