#pragma once

#include <stdint.h>


#pragma pack(push, 1)

typedef struct {
    uint8_t r, g, b;
} RGB24;

#pragma pack(pop)


static void DecompressDXT5(uint8_t* input, int width, int height, RGB24* output) {
    int blocksX = (width + 3) / 4;
    int blocksY = (height + 3) / 4;

    for (int blockY = 0; blockY < blocksY; blockY++) {
        for (int blockX = 0; blockX < blocksX; blockX++) {
            uint8_t* block = input + (blockY * blocksX + blockX) * 16;

            // Decompress color channels
            uint16_t color0 = *(uint16_t*)(block + 8);
            uint16_t color1 = *(uint16_t*)(block + 10);
            uint32_t colorBits = *(uint32_t*)(block + 12);

            RGB24 colorTable[4];
            colorTable[0].r = ((color0 >> 11) & 0x1F) * 255 / 31;
            colorTable[0].g = ((color0 >> 5) & 0x3F) * 255 / 63;
            colorTable[0].b = (color0 & 0x1F) * 255 / 31;

            colorTable[1].r = ((color1 >> 11) & 0x1F) * 255 / 31;
            colorTable[1].g = ((color1 >> 5) & 0x3F) * 255 / 63;
            colorTable[1].b = (color1 & 0x1F) * 255 / 31;

            if (color0 > color1) {
                colorTable[2].r = (2 * colorTable[0].r + colorTable[1].r) / 3;
                colorTable[2].g = (2 * colorTable[0].g + colorTable[1].g) / 3;
                colorTable[2].b = (2 * colorTable[0].b + colorTable[1].b) / 3;

                colorTable[3].r = (colorTable[0].r + 2 * colorTable[1].r) / 3;
                colorTable[3].g = (colorTable[0].g + 2 * colorTable[1].g) / 3;
                colorTable[3].b = (colorTable[0].b + 2 * colorTable[1].b) / 3;
            }
            else {
                colorTable[2].r = (colorTable[0].r + colorTable[1].r) / 2;
                colorTable[2].g = (colorTable[0].g + colorTable[1].g) / 2;
                colorTable[2].b = (colorTable[0].b + colorTable[1].b) / 2;

                colorTable[3].r = 0;
                colorTable[3].g = 0;
                colorTable[3].b = 0;
            }

            // Write to output
            for (int i = 0; i < 16; i++) {
                int pixelX = blockX * 4 + (i % 4);
                int pixelY = blockY * 4 + (i / 4);
                int pixelIndex = pixelY * width + pixelX;

                uint8_t code = (colorBits >> (2 * i)) & 0x03;
                output[pixelIndex] = colorTable[code];
            }
        }
    }
}
