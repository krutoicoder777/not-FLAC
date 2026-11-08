#ifndef STEREO_H__
#define STEREO_H__

#include <stdint.h>

typedef enum {
    STEREO_MODE_INDEPENDENT = 0,
    STEREO_MODE_LEFT_SIDE   = 1,
    STEREO_MODE_RIGHT_SIDE  = 2,
    STEREO_MODE_MID_SIDE    = 3,
} stereo_mode_t;

void stereo_encode(stereo_mode_t mode, const int32_t *lr, int32_t *primary, int32_t *secondary, int n);
void stereo_decode(stereo_mode_t mode, const int32_t *primary, const int32_t *secondary, int32_t *lr, int n);

#endif
