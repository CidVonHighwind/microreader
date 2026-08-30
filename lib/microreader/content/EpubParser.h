#pragma once

#include <string>
#include <vector>

#include "ContentModel.h"
#include "CssParser.h"
#include "ZipReader.h"

namespace microreader {

// Callback type for streaming chapter parsing.
// Each paragraph is emitted as soon as it's parsed — no accumulation.
using ParagraphSink = void (*)(void* ctx, Paragraph&& para);

// Cache for per-file parsed CSS stylesheets.
// Uses a fixed-size array so entry addresses never move — callers may hold
// raw pointers to Entry::sheet for the duration of a chapter parse.
// Evicts the least-recently-used eligible entry when total cached bytes
// exceed kMaxCacheBytes, or when the platform reports low heap (ESP32 only).
// CSS files are loaded on demand — only those actually referenced by a
// chapter's <link rel="stylesheet"> tags are loaded.
class CssCache {
 public:
  static constexpr size_t kMaxCacheBytes = 64 * 1024;
  static constexpr size_t kMaxEntries = 16;

  // protect_gen: entries whose last_used_gen > protect_gen are not evicted.
  // Pass current_gen() before the chapter's head scan starts so that CSS
  // loaded earlier in the same chapter cannot be evicted mid-scan.
  //
  // work_buf/work_buf_size: caller-provided decompression buffer (from the
  // application's framebuffer). Required on cache miss; if null and there
  // is a cache miss the stylesheet is silently skipped rather than crashing.
  const CssStylesheet* get_or_load(IZipFile& file, const ZipReader& zip, const std::string& path,
                                   const CssConfig& config, uint32_t protect_gen = 0, uint8_t* work_buf = nullptr,
                                   size_t work_buf_size = 0);

  void clear();

  size_t entry_count() const {
    return count_;
  }
  size_t total_bytes() const {
    return total_bytes_;
  }
  uint32_t current_gen() const {
    return gen_;
  }

 private:
  struct Entry {
    std::string path;
    CssStylesheet sheet;
    size_t bytes = 0;
    uint32_t last_used_gen = 0;
  };

  Entry entries_[kMaxEntries];
  size_t count_ = 0;
  size_t total_bytes_ = 0;
  uint32_t gen_ = 0;

  static bool low_memory();
  // Returns the index of the best LRU candidate with last_used_gen <= protect_gen
  // (or any entry when protect_gen == 0). Returns kMaxEntries if none found.
  size_t find_evict_slot(uint32_t protect_gen) const;
};

// Callback for element id="" annotations encountered during streaming XHTML parsing.
// Called once per element that has an id attribute.
// para_idx = number of paragraphs already emitted before this element opens.
using IdSink = void (*)(void* ctx, const char* id, size_t id_len, uint32_t para_idx);

enum class EpubError {
  Ok = 0,
  ContainerMissing,
  ContentOpfMissing,
  InvalidData,
  ZipError,
  XmlError,
};

// EPUB book: parsed from an EPUB file.
// Stores the ZIP entries, spine, metadata, and global stylesheet.
// Chapters are parsed on-demand via parse_chapter().
class Epub {
 public:
  Epub() = default;

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    css_config_ = config;
  }
  const CssConfig& css_config() const {
    return css_config_;
  }

  // Open an EPUB file. work_buf (~45KB) and xml_buf (~4KB) are used during
  // OPF/NCX parsing. Caller must provide both; allocate them before calling.
  // If parse_css_ncx is false, skips extracting CSS and parsing NCX (fast for indexing).
  EpubError open(IZipFile& file, uint8_t* work_buf, uint8_t* xml_buf, bool parse_css_ncx = true);

  // Lightweight open: only parses the ZIP central directory.
  // No OPF/NCX/CSS parsing — only zip().entry() is available afterwards.
  // Sufficient for image decode operations.
  EpubError open_zip_only(IZipFile& file);

  // Release all parsed data (ZIP entries, spine, stylesheet, TOC, metadata).
  void close();

  // Number of chapters (spine items).
  size_t chapter_count() const {
    return spine_.size();
  }

  // Parse a specific chapter by index.
  EpubError parse_chapter(IZipFile& file, size_t index, Chapter& out) const;

  // Stream-parse a chapter: paragraphs are emitted one at a time via sink.
  // Uses ~37KB working memory instead of extracting the full XHTML.
  EpubError parse_chapter_streaming(IZipFile& file, size_t index, ParagraphSink sink, void* sink_ctx, uint8_t* work_buf,
                                    uint8_t* xml_buf, IdSink id_sink = nullptr, void* id_sink_ctx = nullptr) const;

  // Access metadata.
  const EpubMetadata& metadata() const {
    return metadata_;
  }

  // Access TOC.
  const TableOfContents& toc() const {
    return toc_;
  }
  TableOfContents& toc() {
    return toc_;
  }

  // Access the zip reader (for image extraction etc)
  const ZipReader& zip() const {
    return zip_;
  }
  const std::vector<SpineItem>& spine() const {
    return spine_;
  }
  const CssCache& css_cache() const {
    return css_cache_;
  }

  int cover_entry_index() const {
    return cover_idx_;
  }

  // Resolve a path relative to a content file's directory.
  // e.g. resolve_path("OEBPS/chapters/", "../images/test.jpg") → "OEBPS/images/test.jpg"
  static std::string resolve_path(const std::string& base_dir, const std::string& href);

  // Find an entry index by path.
  const ZipEntry* find_entry(const std::string& path) const {
    return zip_.find(path);
  }
  int find_entry_index(const std::string& path) const;

 private:
  ZipReader zip_;
  std::string root_dir_;  // e.g. "OEBPS/"
  EpubMetadata metadata_;
  std::vector<SpineItem> spine_;
  TableOfContents toc_;
  CssConfig css_config_;
  mutable CssCache css_cache_;
  int cover_idx_ = -1;

  // Internal parsing steps
  EpubError parse_container(IZipFile& file, std::string& rootfile_path, uint8_t* work_buf, size_t work_buf_size,
                            uint8_t* xml_buf, size_t xml_buf_size);
  EpubError parse_opf(IZipFile& file, const std::string& opf_path, uint8_t* work_buf, uint8_t* xml_buf,
                      bool parse_css_ncx);
};

// Parse XHTML body into paragraphs (used by Epub::parse_chapter, also
// usable standalone for testing).
EpubError parse_xhtml_body(const uint8_t* data, size_t size, const CssStylesheet* inline_css,
                           const CssStylesheet* extern_css, const std::string& base_dir, const ZipReader& zip,
                           std::vector<Paragraph>& out);

}  // namespace microreader
