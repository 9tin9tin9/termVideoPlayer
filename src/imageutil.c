#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "ansipixel.h"
#include "imageutil.h"

#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))

// https://stackoverflow.com/questions/34622717/bicubic-interpolation-in-c

#define CLAMP(v, min, max) if(v < min) { v = min; } else if(v > max) { v = max; }
 
typedef struct {
     int x, y;
     AP_ColorRgb *data;
} Image;

float cubic_hermite(const float A, const float B, const float C, const float D, const float t) {

    const float a = -A / 2.0f + (3.0f*B) / 2.0f - (3.0f*C) / 2.0f + D / 2.0f;
    const float b = A - (5.0f*B) / 2.0f + 2.0f*C - D / 2.0f;
    const float c = -A / 2.0f + C / 2.0f;
    const float d = B;

    return a*t*t*t + b*t*t + c*t + d;
}

void get_pixel_clamped(const Image *source_image, int x, int y, const int W, uint8_t temp[])  {

    CLAMP(x, 0, source_image->x - 1);
    CLAMP(y, 0, source_image->y - 1);

    temp[0] = AP_ColorRgb_r(source_image->data[x+(W*y)]);
    temp[1] = AP_ColorRgb_g(source_image->data[x+(W*y)]);
    temp[2] = AP_ColorRgb_b(source_image->data[x+(W*y)]);
}

void sample_bicubic(const Image *source_image, const float u, const float v, const int W, uint8_t sample[]) {

    float x = (u * source_image->x)-0.5;
    int xint = (int)x;
    float xfract = x-floorf(x);

    float y = (v * source_image->y) - 0.5;
    int yint = (int)y;
    float yfract = y - floorf(y);

    int i;

    uint8_t p00[3];
    uint8_t p10[3];
    uint8_t p20[3];
    uint8_t p30[3];

    uint8_t p01[3];
    uint8_t p11[3];
    uint8_t p21[3];
    uint8_t p31[3];

    uint8_t p02[3];
    uint8_t p12[3];
    uint8_t p22[3];
    uint8_t p32[3];

    uint8_t p03[3];
    uint8_t p13[3];
    uint8_t p23[3];
    uint8_t p33[3];

    // 1st row
    get_pixel_clamped(source_image, xint - 1, yint - 1, W, p00);
    get_pixel_clamped(source_image, xint + 0, yint - 1, W, p10);
    get_pixel_clamped(source_image, xint + 1, yint - 1, W, p20);
    get_pixel_clamped(source_image, xint + 2, yint - 1, W, p30);

    // 2nd row
    get_pixel_clamped(source_image, xint - 1, yint + 0, W, p01);
    get_pixel_clamped(source_image, xint + 0, yint + 0, W, p11);
    get_pixel_clamped(source_image, xint + 1, yint + 0, W, p21);
    get_pixel_clamped(source_image, xint + 2, yint + 0, W, p31);

    // 3rd row
    get_pixel_clamped(source_image, xint - 1, yint + 1, W, p02);
    get_pixel_clamped(source_image, xint + 0, yint + 1, W, p12);
    get_pixel_clamped(source_image, xint + 1, yint + 1, W, p22);
    get_pixel_clamped(source_image, xint + 2, yint + 1, W, p32);

    // 4th row
    get_pixel_clamped(source_image, xint - 1, yint + 2, W, p03);
    get_pixel_clamped(source_image, xint + 0, yint + 2, W, p13);
    get_pixel_clamped(source_image, xint + 1, yint + 2, W, p23);
    get_pixel_clamped(source_image, xint + 2, yint + 2, W, p33);

    // interpolate bi-cubically!
    for (i = 0; i < 3; i++) {

        float col0 = cubic_hermite(p00[i], p10[i], p20[i], p30[i], xfract);
        float col1 = cubic_hermite(p01[i], p11[i], p21[i], p31[i], xfract);
        float col2 = cubic_hermite(p02[i], p12[i], p22[i], p32[i], xfract);
        float col3 = cubic_hermite(p03[i], p13[i], p23[i], p33[i], xfract);

        float value = cubic_hermite(col0, col1, col2, col3, yfract);

        CLAMP(value, 0.0f, 255.0f);

        sample[i] = (uint8_t)value;

    }

}


void _resize_bicubic(
    AP_ColorRgb *src,
    AP_ColorRgb *dest,
    const size_t oldHeight, const size_t oldWidth,
    const size_t newHeight, const size_t newWidth)
{
    Image destination_image = {
        .x = newWidth,
        .y = newHeight,
        .data = dest
    };
    Image source_image = {
        .x = oldWidth,
        .y = oldHeight,
        .data = src,
    };
    const float scale = (float)newHeight / oldHeight;

    uint8_t sample[3];
    int y, x;

    for (y = 0; y < destination_image.y; y++) {

        float v = (float)y / (float)(destination_image.y - 1);

        for (x = 0; x < destination_image.x; ++x) {

            float u = (float)x / (float)(destination_image.x - 1);
            sample_bicubic(&source_image, u, v, source_image.x, sample);

            AP_ColorRgb_r(destination_image.data[x+((destination_image.x)*y)]) = sample[0];
            AP_ColorRgb_g(destination_image.data[x+((destination_image.x)*y)]) = sample[1];
            AP_ColorRgb_b(destination_image.data[x+((destination_image.x)*y)]) = sample[2];
        }
    }
}

void buildGaussianKernel(float* kernel, const int pixel) {
    float sigma = max(pixel / 2.0, 1);
    int width = pixel * 2 + 1;
    float sum = 0;
    for (int i = 0; i < width; i++) {
        float x = i - pixel;
        float expNumerator = -(x*x);
        float expDenominatorInverse = 1 / (2 * sigma * sigma);
        float eExpression = expf(expNumerator * expDenominatorInverse);
        kernel[i] = eExpression / (sqrtf(2 *  3.14159265359) * sigma);
        sum += kernel[i];
    }
    for (int i = 0; i < width; i++) {
        kernel[i] /= sum;
    }
}

void blur_gaussian(AP_ColorRgb* src, const size_t h, const size_t w, const int pixel) {
    #define inrange(x, l, h) ((x) >= (l) && (x) < h)
    #define KERNEL_NOT_INIT 0
    #define KERNEL_BUILDING 2
    #define KERNEL_OK 3

    static float* kernel = NULL;
    int kernelW = pixel * 2 + 1;
    static _Atomic(int) kernel_init = KERNEL_NOT_INIT;
    AP_ColorRgb* tmp = malloc(h*w*sizeof(*tmp));

    while (kernel_init != KERNEL_OK) {
        if (kernel_init == KERNEL_NOT_INIT) {
            kernel_init = KERNEL_BUILDING;
            if (!kernel) {
                kernel = malloc(kernelW*kernelW*sizeof(*kernel));
                buildGaussianKernel(kernel, pixel);
            }
            kernel_init = KERNEL_OK;
        }
    }

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint8_t r = 0, g = 0, b = 0;
            for (int m = -pixel; m <= pixel; m++) {
                int x = j + m;
                CLAMP(x, 0, w-1);
                size_t srcIndex = i*w + x;
                size_t kernelIndex = m + pixel;
                r += AP_ColorRgb_r(src[srcIndex])*kernel[kernelIndex];
                g += AP_ColorRgb_g(src[srcIndex])*kernel[kernelIndex];
                b += AP_ColorRgb_b(src[srcIndex])*kernel[kernelIndex];
            }
            tmp[i*w + j] = AP_ColorRgb(r, g, b);
        }
    }
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint8_t r = 0, g = 0, b = 0;
            for (int m = -pixel; m <= pixel; m++) {
                int y = i + m;
                CLAMP(y, 0, h-1);
                size_t tmpIndex = y*w + j;
                size_t kernelIndex = m + pixel;
                r += AP_ColorRgb_r(tmp[tmpIndex])*kernel[kernelIndex];
                g += AP_ColorRgb_g(tmp[tmpIndex])*kernel[kernelIndex];
                b += AP_ColorRgb_b(tmp[tmpIndex])*kernel[kernelIndex];
            }
            src[i*w + j] = AP_ColorRgb(r, g, b);
        }
    }
    free(tmp);
}

float sigma_to_box_radius(int boxes[], const float sigma, const int n)  
{
    // ideal filter width
    float wi = sqrtf((12*sigma*sigma/n)+1); 
    int wl = wi; // no need std::floor  
    if(wl%2==0) wl--;
    int wu = wl+2;
                
    float mi = (12*sigma*sigma - n*wl*wl - 4*n*wl - 3*n)/(-4*wl - 4);
    int m = mi+0.5f; // avoid std::round by adding 0.5f and cast to integer type
                
    for(int i=0; i<n; i++) 
        boxes[i] = ((i < m ? wl : wu) - 1) / 2;

    return sqrtf((m*wl*wl+(n-m)*wu*wu-n)/12.f);
}

#define C 4
void flip_block(const uint8_t * in, uint8_t * out, const int w, const int h)
{
    const int block = 256/C;
    for(int x= 0; x < w; x+= block)
    for(int y= 0; y < h; y+= block)
    {
        const uint8_t * p = in + y*w*C + x*C;
        uint8_t * q = out + y*C + x*h*C;
        
        const int blockx= min(w, x+block) - x;
        const int blocky= min(h, y+block) - y;
        for(int xx= 0; xx < blockx; xx++)
        {
            for(int yy= 0; yy < blocky; yy++)
            {
                for(int k= 0; k < C; k++)
                    q[k]= p[k];
                p+= w*C;
                q+= C;                    
            }
            p+= -blocky*w*C + C;
            q+= -blocky*C + h*C;
        }
    }
}

void horizontal_blur_kernel_crop(const uint8_t * in, uint8_t * out, const int w, const int h, const int r)
{
    // change the local variable types depending on the template type for faster calculations
    // using calc_type = std::conditional_t<std::is_integral_v<T>, int, float>;
    typedef int calc_type;

    const float iarr = 1.f / (r+r+1);
    const float iwidth = 1.f / w;

    #pragma omp parallel for
    for(int i=0; i<h; i++) 
    {
        const int begin = i*w;
        const int end = begin+w; 
        calc_type acc[C] = { 0 };

        // current index, left index, right index
        int ti = begin, li = begin-r-1, ri = begin+r;

        // initial acucmulation
        for(int j=ti; j<ri; j++)
            for(int ch=0; ch<C; ++ch)
            {
                acc[ch] += in[j*C+ch]; 
            }

        // 1. left side out and right side in
        for(; li<begin; ri++, ti++, li++)
            for(int ch=0; ch<C; ++ch)
            { 
                acc[ch] += in[ri*C+ch];
                // assert(acc[ch] >= 0);
                const float inorm = 1.f / (float)(ri+1-begin);
                // out[ti*C+ch] = acc[ch]*inorm + (std::is_integral_v<T> ? 0.5f : 0.f); // fixes darkening with integer types
                out[ti*C+ch] = acc[ch]*inorm + 0.5f; // fixes darkening with integer types
            }

        // 2. left side in and right side in
        for(; ri<end; ri++, ti++, li++) 
            for(int ch=0; ch<C; ++ch)
            { 
                acc[ch] += in[ri*C+ch] - in[li*C+ch]; 
                // assert(acc[ch] >= 0);
                // out[ti*C+ch] = acc[ch]*iarr + (std::is_integral_v<T> ? 0.5f : 0.f); // fixes darkening with integer types
                out[ti*C+ch] = acc[ch]*iarr + 0.5f; // fixes darkening with integer types
            }

        // 3. left side in and right side out
        for(; ti<end; ti++, li++)
            for(int ch=0; ch<C; ++ch)
            { 
                acc[ch] -= in[li*C+ch]; 
                // assert(acc[ch] >= 0);
                const float inorm = 1.f / (float)(end-li-1);
                // out[ti*C+ch] = acc[ch]*inorm + (std::is_integral_v<T> ? 0.5f : 0.f); // fixes darkening with integer types
                out[ti*C+ch] = acc[ch]*inorm + 0.5f; // fixes darkening with integer types
            }
    }
}
#undef C

#define swap(a, b) \
    do { \
        __typeof__(a) tmp = (a); \
        (a) = (b); \
        (b) = tmp; \
    } while (0)

void blur_gaussian_fast(uint8_t** in, uint8_t** out, const int h, const int w, const float sigma) 
{
    // compute box kernel sizes
    int boxes[3];
    sigma_to_box_radius(boxes, sigma, 3);

    // perform 3 horizontal blur passes
    horizontal_blur_kernel_crop(*in, *out, w, h, boxes[0]);
    horizontal_blur_kernel_crop(*out, *in, w, h, boxes[1]);
    horizontal_blur_kernel_crop(*in, *out, w, h, boxes[2]);
    
    // flip buffer
    flip_block(*out, *in, w, h);
    
    // perform 3 horizontal blur passes on flippemage
    horizontal_blur_kernel_crop(*in, *out, h, w, boxes[0]);
    horizontal_blur_kernel_crop(*out, *in, h, w, boxes[1]);
    horizontal_blur_kernel_crop(*in, *out, h, w, boxes[2]);
    
    // flip buffer
    flip_block(*out, *in, h, w);
    
    // swap pointers to get result in the ouput buffer 
    swap(*in, *out);
}

AP_ColorRgb* resize_bicubic(
    AP_ColorRgb** src,
    size_t oldHeight, size_t oldWidth,
    const size_t newHeight, const size_t newWidth)
{
    // AP_ColorRgb* dest = malloc(newHeight*newWidth*sizeof(*dest));
    AP_ColorRgb* dest = malloc(oldHeight*oldWidth*sizeof(*dest));

    blur_gaussian_fast((uint8_t**)src, (uint8_t**)&dest, oldHeight, oldWidth, 5);
    swap(*src, dest);

    _resize_bicubic(
        *src, dest,
        oldHeight, oldWidth,
        newHeight, newWidth);

    return dest;
}

