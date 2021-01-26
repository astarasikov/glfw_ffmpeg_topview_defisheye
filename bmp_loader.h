#ifndef __BMP_LOADER__H__
#define __BMP_LOADER__H__

#include <stdint.h>
#include <stddef.h>
#include "error_handling.h"

/******************************************************************************
 * Bitmap manipulation routines.
 * struct BmpInfoHeader is modeled after
 * https://msdn.microsoft.com/en-us/library/windows/desktop/dd183376(v=vs.85).aspx
 *****************************************************************************/
struct BmpInfoHeader {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    int16_t biPlanes;
    int16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} __attribute__((packed));

struct BmpHeader {
    int8_t signature[2];
    uint32_t bmpSize;
    uint32_t reserved;
    uint32_t imageStartOffset;
    struct BmpInfoHeader headerInfo;
	uint8_t padding_v4[0x54];
} __attribute__((packed));

#define SZ_BMP_HEADER_V4 0x8a

typedef enum {
    BMP_RGB565,
    BMP_RGBA8888,
} BmpFormat;

Retcode BmpRead(const char* filename,
    BmpFormat format,
    struct BmpHeader* out_hdr,
    uint8_t* data,
    size_t data_size);

Retcode BmpWrite(const char* filename,
    BmpFormat format,
    struct BmpHeader header,
    uint8_t* data,
    size_t data_size);

#endif //__BMP_LOADER__H__

