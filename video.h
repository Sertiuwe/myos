#ifndef VIDEO_H
#define VIDEO_H

#include "types.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  bpp;
    uint32_t frames;
    uint32_t lba;
} video_t;

int video_load(uint32_t lba, video_t *v);
int video_play(const video_t *v);

#endif
