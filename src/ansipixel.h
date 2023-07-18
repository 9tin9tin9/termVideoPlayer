#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AP_Buffer;
typedef uint8_t AP_Color;

struct AP_Buffer* AP_Buffer_new(size_t height, size_t width);
void AP_Buffer_del(struct AP_Buffer* buf);
AP_Color AP_Buffer_getPixel(struct AP_Buffer* buf, size_t y, size_t x);
void AP_Buffer_setPixel(
    struct AP_Buffer* buf, size_t y, size_t x, AP_Color color);
void AP_Buffer_draw(struct AP_Buffer* buf);

struct AP_BufferRgb;
typedef uint32_t AP_ColorRgb;
#define AP_ColorRgb(r, g, b) (*(AP_ColorRgb*)(uint8_t[4]){(r), (g), (b), 1})
#define AP_ColorRgb_r(p) (((uint8_t*)&p)[0])
#define AP_ColorRgb_g(p) (((uint8_t*)&p)[1])
#define AP_ColorRgb_b(p) (((uint8_t*)&p)[2])

struct AP_BufferRgb* AP_BufferRgb_new(size_t height, size_t width);
void AP_BufferRgb_del(struct AP_BufferRgb* buf);
AP_ColorRgb AP_BufferRgb_getPixel(struct AP_BufferRgb* buf, size_t y, size_t x);
void AP_BufferRgb_setPixel(
    struct AP_BufferRgb* buf, size_t y, size_t x, AP_ColorRgb color);
void AP_BufferRgb_draw(struct AP_BufferRgb* buf);

void AP_clearScreen(struct AP_Buffer* buf); // buf can be NULL
void AP_clearScreenRgb(struct AP_BufferRgb* buf); // buf can be NULL
void AP_resettextcolor();
void AP_showcursor(bool show);
void AP_move(size_t y, size_t x); // move to real text coordinate

AP_Color AP_rgbTo256(AP_ColorRgb rgb);
