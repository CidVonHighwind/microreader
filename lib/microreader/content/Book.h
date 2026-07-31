#pragma once

#include <memory>
#include <string>

#include "ContentModel.h"
#include "EpubParser.h"
#include "ImageDecoder.h"
#include "ZipReader.h"

namespace microreader {

// High-level Book interface — owns the file handle and EPUB parser.
// Provides convenient access to chapters, images, and metadata.
class Book {
 public:
  Book() = default;
  ~Book() {
    close();
  }

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    epub_.set_css_config(config);
  }

  // Open an EPUB file. work_buf (~45KB) and xml_buf (~4KB) are forwarded to
  // the EPUB open/parse pipeline. On ESP32 pass DisplayQueue scratch buffers;
  // on desktop/tests pass nullptr to have Book::open allocate them.
  EpubError open(const char* path, uint8_t* work_buf = nullptr, uint8_t* xml_buf = nullptr, bool parse_css_ncx = true);

  // Lightweight open: only parses the ZIP central directory (no OPF/NCX/CSS).
  // Sufficient for image decode operations that only need zip entry access.
  // Uses ~15-20KB less RAM than full open().
  bool open_zip_only(const char* path);

  // Release all resources (file handle, parsed EPUB data).
  void close();

  bool is_open() const {
    return file_open_;
  }

  const EpubMetadata& metadata() const {
    return epub_.metadata();
  }
  const TableOfContents& toc() const {
    return epub_.toc();
  }
  TableOfContents& toc() {
    return epub_.toc();
  }

  size_t chapter_count() const {
    return epub_.chapter_count();
  }

  int cover_entry_index() const {
    return epub_.cover_entry_index();
  }

  // Load a chapter by index (0-based).
  EpubError load_chapter(size_t index, Chapter& out);

  // Stream-parse a chapter: paragraphs emitted via callback, ~37KB working memory.
  // id_sink (optional): called for each element with id="" in the XHTML source.
  EpubError load_chapter_streaming(size_t index, ParagraphSink sink, void* sink_ctx, uint8_t* work_buf,
                                   uint8_t* xml_buf, IdSink id_sink = nullptr, void* id_sink_ctx = nullptr);

  // Extract and decode an image from the EPUB (by zip entry index).
  // Returns the 1-bit dithered bitmap. The caller owns the memory.
  // max_w/max_h: maximum output dimensions (0 = no limit).
  // work_buf: optional caller-provided scratch (>= ZipEntryInput::kMinWorkBufSize).
  //   On ESP32, pass queue.scratch_buf1() to avoid a 45 KB heap allocation.
  //   If nullptr (or too small), the buffer is heap-allocated internally.
  // Returns UnsupportedFormat if images_enabled is false.
  ImageError decode_image(uint16_t entry_index, DecodedImage& out, uint16_t max_w = 0, uint16_t max_h = 0,
                          uint8_t* work_buf = nullptr, size_t work_buf_size = 0);

  // Extract raw image data (for format detection, etc).
  ZipError extract_entry(uint16_t entry_index, std::vector<uint8_t>& out);

  // Read only the image dimensions (width/height) without decoding.
  // work_buf/work_size: optional pre-allocated scratch (must be >= ~45 KB, see kWorkSize).
  // Falls back to heap allocation if not provided.
  // Returns false if the entry is invalid or the format is unrecognised.
  bool read_image_size(uint16_t entry_index, uint16_t& w, uint16_t& h, uint8_t* work_buf = nullptr,
                       size_t work_size = 0);

  // Access the underlying EPUB for advanced queries.
  const Epub& epub() const {
    return epub_;
  }

  // Direct access to the underlying ZIP file handle (benchmark/tools only).
  IZipFile& file() {
    return file_;
  }

 private:
  StdioZipFile file_;
  Epub epub_;
  bool file_open_ = false;
};

}  // namespace microreader
