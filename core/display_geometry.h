#pragma once
#include <stddef.h>
#include <stdint.h>
// A full RGB565 frame rotated 180 degrees is its pixel sequence reversed.
static inline void panel_rotate_180(uint16_t *pixels, size_t count) {
    for (size_t i = 0; i < count / 2; ++i) {
        uint16_t value = pixels[i];
        pixels[i] = pixels[count - 1 - i];
        pixels[count - 1 - i] = value;
    }
}
static inline uint16_t panel_mirror_coordinate(uint16_t position, uint16_t extent) {
    return position < extent ? extent - 1 - position : 0;
}
