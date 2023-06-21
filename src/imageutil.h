#pragma once

#include "ansipixel.h"

// only for downscaling
AP_ColorRgb* resize_bicubic(
    AP_ColorRgb** src,
    size_t oldHeight, size_t oldWidth,
    size_t newHeight, size_t newWidth);
