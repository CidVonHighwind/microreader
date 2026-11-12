#include "BmpSleepConverter.h"

#include "../display/DrawBuffer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace {

// 4×4 Bayer ordered dither matrix (values 0–15, mean = 7.5)
static const uint8_t kBayer[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// Quantize grayscale byte to 4-level MGR2 state: 0=white, 1=light, 2=dark, 3=black.
static uint8_t quantize(uint8_t gray, int x, int y) {
    int adj = (int)gray + kBayer[y & 3][x & 3] * 4 - 30;
    if (adj < 0)   adj = 0;
    if (adj > 255) adj = 255;
    int level = adj >> 6;
    if (level > 3) level = 3;
    return (uint8_t)(3 - level);
}

// Decode one pixel from a BMP row buffer to grayscale [0, 255].
static uint8_t decode_pixel(const uint8_t* row, int sx, int bpp,
                              const uint8_t* palette, bool is_rgb565) {
    uint8_t r, g, b;
    if (bpp == 1) {
        const uint8_t bit = (row[sx / 8] >> (7 - sx % 8)) & 1;
        b = palette[bit*4]; g = palette[bit*4+1]; r = palette[bit*4+2];
    } else if (bpp == 4) {
        const uint8_t nibble = (sx & 1) ? (row[sx/2] & 0x0F) : (row[sx/2] >> 4);
        b = palette[nibble*4]; g = palette[nibble*4+1]; r = palette[nibble*4+2];
    } else if (bpp == 24) {
        b = row[sx*3]; g = row[sx*3+1]; r = row[sx*3+2];
    } else if (bpp == 32) {
        b = row[sx*4]; g = row[sx*4+1]; r = row[sx*4+2];
    } else if (bpp == 16) {
        const uint16_t px = (uint16_t)row[sx*2] | ((uint16_t)row[sx*2+1] << 8);
        if (is_rgb565) {
            r = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);
            g = (uint8_t)(((px >>  5) & 0x3F) * 255 / 63);
            b = (uint8_t)( (px        & 0x1F) * 255 / 31);
        } else {  // BGR555
            b = (uint8_t)(((px >> 10) & 0x1F) * 255 / 31);
            g = (uint8_t)(((px >>  5) & 0x1F) * 255 / 31);
            r = (uint8_t)( (px        & 0x1F) * 255 / 31);
        }
    } else {  // 8bpp palette
        const uint8_t idx = row[sx];
        b = palette[idx*4]; g = palette[idx*4+1]; r = palette[idx*4+2];
    }
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int32_t  le32s(const uint8_t* p) { return (int32_t)le32(p); }
static uint16_t le16(const uint8_t* p)  { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }

}  // namespace

namespace microreader {

bool convert_bmp_to_mgr2(const char* bmp_path, const char* mgr_out_path) {
    static constexpr int OUT_W      = DisplayFrame::kPanelWidth;
    static constexpr int OUT_H      = DisplayFrame::kPhysicalHeight;
    static constexpr int OUT_STRIDE = (OUT_W + 3) / 4;  // 200 bytes per row

    FILE* f = std::fopen(bmp_path, "rb");
    if (!f) return false;

    // File header (14 bytes)
    uint8_t fhdr[14];
    if (std::fread(fhdr, 1, 14, f) != 14 || fhdr[0] != 'B' || fhdr[1] != 'M') {
        std::fclose(f); return false;
    }
    const uint32_t data_offset = le32(fhdr + 10);

    // DIB header (first 40 bytes; actual header may be larger for V4/V5)
    uint8_t dhdr[40];
    if (std::fread(dhdr, 1, 40, f) != 40) { std::fclose(f); return false; }

    const int32_t  width   = le32s(dhdr + 4);
    int32_t        height  = le32s(dhdr + 8);
    const uint16_t bpp     = le16(dhdr + 14);
    const uint32_t compr   = le32(dhdr + 16);
    const uint32_t dib_sz  = le32(dhdr);

    if (width <= 0 || width > 4096 || height == 0 || height < -4096 || height > 4096) {
        std::fclose(f); return false;
    }
    // Accept BI_RGB (0), BI_BITFIELDS (3), BI_ALPHABITFIELDS (6); reject RLE.
    if ((compr != 0 && compr != 3 && compr != 6) ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) {
        std::fclose(f); return false;
    }

    const bool top_down = (height < 0);
    if (top_down) height = -height;

    // 16bpp BI_BITFIELDS: detect RGB565 vs BGR555 by reading the red channel mask.
    bool is_rgb565 = false;
    if (bpp == 16 && (compr == 3 || compr == 6)) {
        uint8_t masks[12] = {};
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(masks, 1, 12, f);
        is_rgb565 = (le32(masks) == 0xF800u);
    }

    // Palette for indexed formats (1/4/8bpp), immediately after the DIB header.
    uint8_t palette[256 * 4] = {};
    if (bpp <= 8) {
        const size_t pal_entries = (size_t)1 << bpp;  // 2, 16, or 256
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(palette, 1, pal_entries * 4, f);
    }

    const int src_stride = ((width * bpp + 31) / 32) * 4;

    uint8_t* row_buf = (uint8_t*)std::malloc((size_t)src_stride);
    if (!row_buf) { std::fclose(f); return false; }

    FILE* out = std::fopen(mgr_out_path, "wb");
    if (!out) { std::free(row_buf); std::fclose(f); return false; }

    // MGR2 header
    const uint16_t ow = OUT_W, oh = OUT_H;
    std::fwrite("MGR2", 1, 4, out);
    std::fwrite(&ow, 2, 1, out);
    std::fwrite(&oh, 2, 1, out);

    bool ok = true;
    const bool portrait = (height > width);

    if (!portrait) {
        // ── Landscape path: row-by-row, O(1) extra memory ───────────────────
        uint8_t out_row[OUT_STRIDE];
        for (int out_y = 0; out_y < OUT_H && ok; ++out_y) {
            const int src_log_y  = out_y * (int)height / OUT_H;
            const int src_file_y = top_down ? src_log_y : ((int)height - 1 - src_log_y);
            const long row_pos   = (long)data_offset + (long)src_file_y * src_stride;
            if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) {
                ok = false; break;
            }
            std::memset(out_row, 0, OUT_STRIDE);
            for (int out_x = 0; out_x < OUT_W; ++out_x) {
                const int sx    = out_x * (int)width / OUT_W;
                const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                out_row[out_x / 4] |= (uint8_t)(quantize(g, out_x, out_y) << (6 - (out_x % 4) * 2));
            }
            if (std::fwrite(out_row, 1, OUT_STRIDE, out) != OUT_STRIDE)
                ok = false;
        }
    } else {
        // ── Portrait path: CCW 90° rotation (matches Python ROTATE_90) ───────
        // Derivation: new[out_y][out_x] = old[out_x * H / 800][W-1 - out_y * W / 480]
        // For a fixed out_x, the source row is constant → one seek per output column.
        // Accumulate the 96 KB output array, then write all rows.
        uint8_t* output = (uint8_t*)std::malloc(OUT_H * OUT_STRIDE);
        if (!output) {
            ok = false;
        } else {
            std::memset(output, 0, OUT_H * OUT_STRIDE);
            for (int out_x = 0; out_x < OUT_W && ok; ++out_x) {
                const int src_log_y  = out_x * (int)height / OUT_W;
                const int src_file_y = top_down ? src_log_y : ((int)height - 1 - src_log_y);
                const long row_pos   = (long)data_offset + (long)src_file_y * src_stride;
                if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                    std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) {
                    ok = false; break;
                }
                for (int out_y = 0; out_y < OUT_H; ++out_y) {
                    const int sx    = (int)width - 1 - out_y * (int)width / OUT_H;
                    const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                    const uint8_t s = quantize(g, out_x, out_y);
                    output[out_y * OUT_STRIDE + out_x / 4] |=
                        (uint8_t)(s << (6 - (out_x % 4) * 2));
                }
            }
            if (ok) {
                for (int out_y = 0; out_y < OUT_H && ok; ++out_y) {
                    if (std::fwrite(output + out_y * OUT_STRIDE, 1, OUT_STRIDE, out) != OUT_STRIDE)
                        ok = false;
                }
            }
            std::free(output);
        }
    }

    std::free(row_buf);
    std::fclose(f);
    std::fclose(out);

    if (!ok)
        std::remove(mgr_out_path);

    return ok;
}

}  // namespace microreader
