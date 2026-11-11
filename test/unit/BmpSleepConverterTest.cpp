#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "microreader/content/BmpSleepConverter.h"
#include "microreader/display/DrawBuffer.h"
// The converter targets the device panel: 800x480 on the X4, 792x528 on the X3.
static constexpr int kOutW = microreader::DisplayFrame::kPanelWidth;
static constexpr int kOutH = microreader::DisplayFrame::kPhysicalHeight;
static constexpr int kOutStride = (kOutW + 3) / 4;


// ---------------------------------------------------------------------------
// BMP writing helpers
// ---------------------------------------------------------------------------

static void write_le16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x));
    v.push_back((uint8_t)(x >> 8));
}
static void write_le32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 24));
}

// Build a minimal BITMAPINFOHEADER BMP.
// fill_r/g/b: uniform fill colour.
static std::vector<uint8_t> make_bmp_24(int w, int h, uint8_t fill_r,
                                         uint8_t fill_g, uint8_t fill_b,
                                         bool top_down = false) {
    const int row_stride = ((w * 3 + 3) / 4) * 4;
    const int pixel_data_size = row_stride * h;
    const int data_offset = 14 + 40;

    std::vector<uint8_t> bmp;
    // File header
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);  // reserved
    write_le32(bmp, (uint32_t)data_offset);
    // DIB header (BITMAPINFOHEADER, 40 bytes)
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, top_down ? (uint32_t)(uint32_t(-h)) : (uint32_t)h);
    write_le16(bmp, 1);    // planes
    write_le16(bmp, 24);   // bpp
    write_le32(bmp, 0);    // BI_RGB
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);  // ppm
    write_le32(bmp, 0); write_le32(bmp, 0);
    // Pixel data (BGR; bottom-up by default)
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            bmp.push_back(fill_b);
            bmp.push_back(fill_g);
            bmp.push_back(fill_r);
        }
        for (int pad = w * 3; pad % 4 != 0; ++pad)
            bmp.push_back(0);
    }
    return bmp;
}

static std::vector<uint8_t> make_bmp_32_rgb(int w, int h, uint8_t fill_r,
                                              uint8_t fill_g, uint8_t fill_b) {
    const int data_offset = 14 + 40;
    const int pixel_data_size = w * h * 4;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 32);
    write_le32(bmp, 0);  // BI_RGB
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    for (int i = 0; i < w * h; ++i) {
        bmp.push_back(fill_b);
        bmp.push_back(fill_g);
        bmp.push_back(fill_r);
        bmp.push_back(0xFF);  // alpha
    }
    return bmp;
}

// 32-bit with BI_BITFIELDS (compr=3) — common output from Windows snipping tools.
static std::vector<uint8_t> make_bmp_32_bitfields(int w, int h, uint8_t fill_r,
                                                    uint8_t fill_g, uint8_t fill_b) {
    const int data_offset = 14 + 40 + 16;  // +16 for RGBAX masks
    const int pixel_data_size = w * h * 4;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 32);
    write_le32(bmp, 3);  // BI_BITFIELDS
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    // Standard BGRA masks: red=0x00FF0000, green=0x0000FF00, blue=0x000000FF, alpha=0xFF000000
    write_le32(bmp, 0x00FF0000u);
    write_le32(bmp, 0x0000FF00u);
    write_le32(bmp, 0x000000FFu);
    write_le32(bmp, 0xFF000000u);
    for (int i = 0; i < w * h; ++i) {
        bmp.push_back(fill_b);
        bmp.push_back(fill_g);
        bmp.push_back(fill_r);
        bmp.push_back(0xFF);
    }
    return bmp;
}

static std::vector<uint8_t> make_bmp_8(int w, int h, uint8_t fill_idx,
                                         uint8_t pal_r, uint8_t pal_g, uint8_t pal_b) {
    const int row_stride = ((w + 3) / 4) * 4;
    const int data_offset = 14 + 40 + 256 * 4;
    const int pixel_data_size = row_stride * h;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 8);
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 256); write_le32(bmp, 256);
    // Palette: 256 entries, only entry fill_idx is non-zero
    for (int i = 0; i < 256; ++i) {
        if (i == fill_idx) { bmp.push_back(pal_b); bmp.push_back(pal_g); bmp.push_back(pal_r); }
        else { bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); }
        bmp.push_back(0);
    }
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) bmp.push_back(fill_idx);
        for (int pad = w; pad % 4 != 0; ++pad) bmp.push_back(0);
    }
    return bmp;
}

// 16-bit RGB565 with BI_BITFIELDS
static std::vector<uint8_t> make_bmp_16_rgb565(int w, int h, uint8_t fill_r,
                                                 uint8_t fill_g, uint8_t fill_b) {
    const int row_stride = ((w * 2 + 3) / 4) * 4;
    const int data_offset = 14 + 40 + 12;  // + 3 masks (no alpha mask)
    const int pixel_data_size = row_stride * h;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 16);
    write_le32(bmp, 3);  // BI_BITFIELDS
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    write_le32(bmp, 0xF800u);   // red mask
    write_le32(bmp, 0x07E0u);   // green mask
    write_le32(bmp, 0x001Fu);   // blue mask
    // Pack RGB565 pixel
    const uint16_t px = (uint16_t)(((fill_r >> 3) << 11) | ((fill_g >> 2) << 5) | (fill_b >> 3));
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            bmp.push_back((uint8_t)(px));
            bmp.push_back((uint8_t)(px >> 8));
        }
        for (int pad = w * 2; pad % 4 != 0; ++pad) bmp.push_back(0);
    }
    return bmp;
}

// ---------------------------------------------------------------------------
// Helper: write bytes to a temp file, return path.
// ---------------------------------------------------------------------------
static std::string write_tmp(const std::vector<uint8_t>& data, const std::string& name) {
    std::string path = std::string(TEST_FIXTURES_DIR) + "/" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
    return path;
}

// ---------------------------------------------------------------------------
// Helper: read MGR2 file, check header, return pixel data.
// ---------------------------------------------------------------------------
struct Mgr2 {
    bool     valid = false;
    uint16_t w = 0, h = 0;
    std::vector<uint8_t> pixels;  // packed 2bpp rows
};
static Mgr2 read_mgr2(const std::string& path) {
    Mgr2 m;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return m;
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MGR2", 4) != 0) {
        std::fclose(f); return m;
    }
    if (std::fread(&m.w, 2, 1, f) != 1 || std::fread(&m.h, 2, 1, f) != 1) {
        std::fclose(f); return m;
    }
    const size_t stride = ((size_t)m.w + 3) / 4;
    m.pixels.resize(stride * m.h);
    std::fread(m.pixels.data(), 1, m.pixels.size(), f);
    std::fclose(f);
    m.valid = true;
    return m;
}

// Decode one pixel from packed 2bpp MGR2 data.
static int mgr2_pixel(const Mgr2& m, int x, int y) {
    const size_t stride = ((size_t)m.w + 3) / 4;
    const uint8_t byte  = m.pixels[(size_t)y * stride + x / 4];
    return (byte >> (6 - (x % 4) * 2)) & 0x3;
}

static std::string out_path(const std::string& name) {
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class BmpConverterTest : public ::testing::Test {
protected:
    // Clean up temp files after each test.
    std::vector<std::string> tmp_files_;
    void TearDown() override {
        for (const auto& p : tmp_files_)
            std::remove(p.c_str());
    }
    std::string bmp(const std::string& name, const std::vector<uint8_t>& data) {
        auto p = write_tmp(data, name);
        tmp_files_.push_back(p);
        return p;
    }
    std::string mgr(const std::string& name) {
        auto p = out_path(name);
        tmp_files_.push_back(p);
        return p;
    }
};

// ── Output dimensions ──────────────────────────────────────────────────────

TEST_F(BmpConverterTest, Output800x480ForLandscapeSource) {
    auto src = bmp("ls_24.bmp", make_bmp_24(640, 480, 200, 200, 200));
    auto dst = mgr("ls_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, kOutW);
    EXPECT_EQ(m.h, kOutH);
    EXPECT_EQ((int)m.pixels.size(), kOutStride * kOutH);
}

TEST_F(BmpConverterTest, Output800x480ForPortraitSource) {
    auto src = bmp("pt_24.bmp", make_bmp_24(480, 800, 200, 200, 200));
    auto dst = mgr("pt_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, kOutW);
    EXPECT_EQ(m.h, kOutH);
}

// ── Format coverage ────────────────────────────────────────────────────────

TEST_F(BmpConverterTest, Format24bppRgb) {
    // White fill → all pixels should be state 0 (white)
    auto src = bmp("fmt_24.bmp", make_bmp_24(200, 100, 255, 255, 255));
    auto dst = mgr("fmt_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Sample a pixel far from the dither noise boundary
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format32bppBiRgb) {
    auto src = bmp("fmt_32rgb.bmp", make_bmp_32_rgb(200, 100, 0, 0, 0));
    auto dst = mgr("fmt_32rgb.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, Format32bppBiTbitfields) {
    // This format is common output from Windows screenshot tools.
    auto src = bmp("fmt_32bf.bmp", make_bmp_32_bitfields(200, 100, 255, 255, 255));
    auto dst = mgr("fmt_32bf.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format8bppIndexed) {
    // Palette entry 42 = mid-gray (128,128,128)
    auto src = bmp("fmt_8.bmp", make_bmp_8(200, 100, 42, 128, 128, 128));
    auto dst = mgr("fmt_8.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Mid-gray should land at state 1 or 2 (dither may vary, just not 0 or 3 at center)
    int px = mgr2_pixel(m, 400, 240);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

TEST_F(BmpConverterTest, Format1bppMonochrome) {
    // 1bpp BMP: palette entry 0 = white, entry 1 = black; all pixels = 1 (black).
    // This is the format used by pokemon-ditto-v2.bmp.
    const int w = 200, h = 100;
    const int row_stride = ((w + 31) / 32) * 4;
    const int pal_size   = 2 * 4;
    const int data_offset = 14 + 40 + pal_size;
    std::vector<uint8_t> bmp_data;
    bmp_data.push_back('B'); bmp_data.push_back('M');
    write_le32(bmp_data, (uint32_t)(data_offset + row_stride * h));
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)data_offset);
    write_le32(bmp_data, 40);
    write_le32(bmp_data, (uint32_t)w);
    write_le32(bmp_data, (uint32_t)h);
    write_le16(bmp_data, 1); write_le16(bmp_data, 1);  // 1bpp
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)(row_stride * h));
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    write_le32(bmp_data, 2); write_le32(bmp_data, 2);
    // Palette: entry 0 = white, entry 1 = black
    bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(0);
    bmp_data.push_back(0);   bmp_data.push_back(0);   bmp_data.push_back(0);   bmp_data.push_back(0);
    // Pixel data: all bits = 1 (black)
    for (int row = 0; row < h; ++row)
        for (int b = 0; b < row_stride; ++b)
            bmp_data.push_back(0xFF);
    auto src = bmp("fmt_1bpp.bmp", bmp_data);
    auto dst = mgr("fmt_1bpp.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, Format1bppPortraitMonochrome) {
    // Portrait 1bpp BMP — same format as pokemon-ditto-v2.bmp (480×800).
    const int w = 60, h = 80;  // small portrait stand-in
    const int row_stride = ((w + 31) / 32) * 4;
    const int pal_size = 8;
    const int data_offset = 14 + 40 + pal_size;
    std::vector<uint8_t> bmp_data;
    bmp_data.push_back('B'); bmp_data.push_back('M');
    write_le32(bmp_data, (uint32_t)(data_offset + row_stride * h));
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)data_offset);
    write_le32(bmp_data, 40);
    write_le32(bmp_data, (uint32_t)w);
    write_le32(bmp_data, (uint32_t)h);
    write_le16(bmp_data, 1); write_le16(bmp_data, 1);
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)(row_stride * h));
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    write_le32(bmp_data, 2); write_le32(bmp_data, 2);
    // entry 0 = black, entry 1 = white
    bmp_data.push_back(0); bmp_data.push_back(0); bmp_data.push_back(0); bmp_data.push_back(0);
    bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(0);
    // all pixels = 1 (white)
    for (int row = 0; row < h; ++row)
        for (int b = 0; b < row_stride; ++b)
            bmp_data.push_back(0xFF);
    auto src = bmp("fmt_1bpp_pt.bmp", bmp_data);
    auto dst = mgr("fmt_1bpp_pt.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, kOutW);
    EXPECT_EQ(m.h, kOutH);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format16bppRgb565) {
    auto src = bmp("fmt_16.bmp", make_bmp_16_rgb565(200, 100, 0, 0, 0));
    auto dst = mgr("fmt_16.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, TopDownBmp) {
    auto src = bmp("topdown.bmp", make_bmp_24(200, 100, 200, 200, 200, /*top_down=*/true));
    auto dst = mgr("topdown.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, kOutW);
    EXPECT_EQ(m.h, kOutH);
}

// ── Error cases ─────────────────────────────────────────────────────────────

TEST_F(BmpConverterTest, NonExistentInputReturnsFalse) {
    EXPECT_FALSE(microreader::convert_bmp_to_mgr2(
        "/nonexistent/path/img.bmp", "/tmp/out.mgr"));
}

TEST_F(BmpConverterTest, InvalidMagicReturnsFalse) {
    std::vector<uint8_t> bad = {0x42, 0x4D + 1};  // 'B' but not 'M'
    auto src = bmp("bad_magic.bmp", bad);
    auto dst = mgr("bad_magic.mgr");
    EXPECT_FALSE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
}

// ── Portrait rotation correctness ──────────────────────────────────────────
// A portrait 2×4 image with distinct top/bottom halves, after CCW 90°
// the left half of the output should come from the top of the source.
TEST_F(BmpConverterTest, PortraitRotationDirectionIsCorrect) {
    // Source: portrait 1×2 (width=1, height=2), bottom half black, top half white.
    // After CCW 90° + scale to 800×480:
    //   - left side of output (out_x≈0) → top of source = white
    //   - right side of output (out_x≈799) → bottom of source = black
    //
    // In BMP bottom-up storage: row 0 in file = BOTTOM of image.
    //   row 0 (file) = logical row 1 = black
    //   row 1 (file) = logical row 0 = white
    //
    // CCW rotation: new[out_y][out_x] = old[out_x * H / 800][W-1 - out_y * W / 480]
    // Since W=1: src_col = 0 for all out_y (only one column).
    // src_log_row = out_x * 2 / 800
    //   out_x=0   → src_log_row=0 → white
    //   out_x=799 → src_log_row=1 → black
    //
    std::vector<uint8_t> src_data;
    // Build a 1×2 24-bit BMP. Row 0 in file = bottom = black (0,0,0).
    // Row 1 in file = top = white (255,255,255).
    // File header (14) + DIB header (40) + 2 rows × 4 bytes stride
    {
        const int w = 1, h = 2, row_stride = 4;  // 1 px × 3 bytes, padded to 4
        const int data_offset = 54;
        src_data.push_back('B'); src_data.push_back('M');
        write_le32(src_data, (uint32_t)(data_offset + row_stride * h));
        write_le32(src_data, 0);
        write_le32(src_data, (uint32_t)data_offset);
        write_le32(src_data, 40);
        write_le32(src_data, (uint32_t)w);
        write_le32(src_data, (uint32_t)h);
        write_le16(src_data, 1); write_le16(src_data, 24);
        write_le32(src_data, 0);
        write_le32(src_data, (uint32_t)(row_stride * h));
        write_le32(src_data, 0); write_le32(src_data, 0);
        write_le32(src_data, 0); write_le32(src_data, 0);
        // Row 0 (bottom of image) = black
        src_data.push_back(0); src_data.push_back(0); src_data.push_back(0);
        src_data.push_back(0);  // padding
        // Row 1 (top of image) = white
        src_data.push_back(255); src_data.push_back(255); src_data.push_back(255);
        src_data.push_back(0);  // padding
    }
    auto src = bmp("rot_dir.bmp", src_data);
    auto dst = mgr("rot_dir.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Left edge of output (out_x=0) → white (state 0)
    // Right edge of output (out_x=799) → black (state 3)
    EXPECT_EQ(mgr2_pixel(m, 0, kOutH / 2), 0);           // white
    EXPECT_EQ(mgr2_pixel(m, kOutW - 1, kOutH / 2), 3);    // black
}
