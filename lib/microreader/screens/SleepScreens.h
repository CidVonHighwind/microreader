#pragma once

// Sleep screen renderers (Settings → Sleep Screen).
//
// Modes stored in the "sleep_image" setting:
//   ""            cycle through the sleep images in turn (original behaviour)
//   "random"      a random sleep image each time
//   "cover"       cover of the book being read, with its progress
//   "stats"       the Reading Stats calendar with today selected
//   anything else a single pinned image: "embedded:N", "bmp:<path>",
//                 "img:<path>" (JPEG/PNG) or a .mgr path
//
// Sleep images are read from both /sdcard/.sleep and /sdcard/sleep (the
// browser manager uploads to the latter).
//
// The draw_* functions render into the DrawBuffer (portrait, Deg90) without
// refreshing the panel; the caller does the full refresh and deep sleep.

#include <string>
#include <vector>

namespace microreader {

class DrawBuffer;
class Epub;
class IZipFile;
struct ZipEntry;

namespace sleep_screens {

constexpr const char* kModeCycle = "";
constexpr const char* kModeRandom = "random";
constexpr const char* kModeCover = "cover";
constexpr const char* kModeStats = "stats";

// Sleep image entries from the SD card folders, sorted by name:
// "<path>.mgr", "bmp:<path>" and "img:<path>" (jpg/jpeg/png). BMP and
// JPEG/PNG need a data dir for their cache, so they're only listed when
// with_converted is true.
std::vector<std::string> list_sd_images(bool with_converted);

// Decode a JPEG/PNG image file and draw it centred, scaled to fit the screen.
// Landscape images are rotated to fill the portrait screen. The result is
// cached in <data_dir>/cache/sleep so later sleeps are instant.
bool draw_image_file(DrawBuffer& buf, const std::string& path, const char* data_dir);

// Draw the book's cover (cached in the book's cache folder) with its title
// and reading progress underneath. False if the book has no usable cover.
bool draw_book_cover(DrawBuffer& buf, const std::string& book_path, const std::string& title, int progress_pct,
                     const char* data_dir);

// Decode an image ZIP entry (or a plain file wrapped as one) into the box
// (x, y, w, h), scaled to fit up or down and centred. allow_rotate turns a
// landscape image 90° when the box is portrait. work/work_size: >= 45 KB.
// placed (optional, 4 ints): receives the rectangle the image covers within
// the box (x, y, w, h). crop_pct > 0: when the image's shape is within that
// many percent of the box's, it fills the box and the overflow is cut off
// (so near-2:3 book covers all come out the same size).
bool draw_image_entry(DrawBuffer& buf, IZipFile& file, const ZipEntry& entry, int x, int y, int w, int h,
                      bool allow_rotate, unsigned char* work, size_t work_size, int* placed = nullptr,
                      int crop_pct = 0);

// ZIP index of the book's cover image: the declared cover, or else an image
// whose name contains "cover". -1 when there is none.
int find_cover_entry(const Epub& epub);

}  // namespace sleep_screens
}  // namespace microreader
