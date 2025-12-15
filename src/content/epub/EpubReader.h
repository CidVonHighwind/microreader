#ifndef EPUB_READER_H
#define EPUB_READER_H

#include <Arduino.h>
#include <SD.h>

#include <vector>

#include "../css/CssParser.h"

extern "C" {
#include "epub_parser.h"
}

struct SpineItem {
  String idref;
  String href;
};

struct TocItem {
  String title;   // Chapter/section title (e.g., "Chapter 1", "Introduction")
  String href;    // XHTML filename (e.g., "chapter1.xhtml")
  String anchor;  // Optional anchor within file (e.g., "section-1")
};

/**
 * EpubReader - Handles EPUB file operations including extraction and caching
 *
 * This class manages:
 * - Opening EPUB files
 * - Extracting files to cache directory (only once)
 * - Parsing container.xml and content.opf
 * - Providing ordered list of content files (spine)
 */
class EpubReader {
 public:
  EpubReader(const char* epubPath, bool cleanCacheOnStart = false);
  ~EpubReader();

  bool isValid() const {
    return valid_;
  }
  String getExtractDir() const {
    return extractDir_;
  }
  String getContentOpfPath() const {
    return contentOpfPath_;
  }
  int getSpineCount() const {
    return spineCount_;
  }
  const SpineItem* getSpineItem(int index) const {
    if (index >= 0 && index < spineCount_) {
      return &spine_[index];
    }
    return nullptr;
  }
  int getTocCount() const {
    return (int)toc_.size();
  }
  const TocItem* getTocItem(int index) const {
    if (index >= 0 && index < (int)toc_.size()) {
      return &toc_[index];
    }
    return nullptr;
  }

  /**
   * Get a file from the EPUB - either from cache or extract it first
   * Returns the full path to the extracted file on SD card
   * Returns empty string if file not found or extraction failed
   */
  String getFile(const char* filename);

  /**
   * Start pull-based streaming extraction of a file
   * Returns streaming context or nullptr on error
   * chunk_size: internal buffer size (0 for default 8KB)
   */
  epub_stream_context* startStreaming(const char* filename, size_t chunk_size = 0);

  /**
   * Get the extract directory path (for building output paths)
   */
  String getExtractedPath(const char* filename);

  /**
   * Get the chapter/section name for a given spine index
   * Looks up the spine item's href in the TOC and returns the title
   * Returns empty string if no matching TOC entry found
   */
  String getChapterNameForSpine(int spineIndex) const;

  /**
   * Get the uncompressed file size for a spine item
   * Returns 0 if index is out of bounds
   */
  size_t getSpineItemSize(int spineIndex) const {
    if (spineIndex >= 0 && spineIndex < spineCount_) {
      return spineSizes_[spineIndex];
    }
    return 0;
  }

  /**
   * Get the cumulative offset (sum of all previous spine items' sizes)
   * Returns 0 for index 0, or if index is out of bounds
   */
  size_t getSpineItemOffset(int spineIndex) const {
    if (spineIndex >= 0 && spineIndex < spineCount_) {
      return spineOffsets_[spineIndex];
    }
    return 0;
  }

  /**
   * Get the total size of all spine items combined
   */
  size_t getTotalBookSize() const {
    return totalBookSize_;
  }

  /**
   * Get the CSS parser for style lookups
   * Returns nullptr if no CSS was loaded
   */
  const CssParser* getCssParser() const {
    return cssParser_;
  }

  /**
   * Get the underlying epub_reader handle (for debugging/testing)
   */
  epub_reader* getReader() const {
    return reader_;
  }

 private:
  bool openEpub();
  void closeEpub();
  bool ensureExtractDirExists();
  bool checkAndUpdateExtractMeta();
  bool isFileExtracted(const char* filename);
  bool extractFile(const char* filename);
  bool parseContainer();
  bool parseContentOpf();
  bool parseTocNcx();
  bool parseCssFiles();
  bool cleanExtractDir();
  bool extractAll();

  struct ManifestItem {
    String id;
    String href;
    String mediaType;
  };

  String epubPath_;
  String extractDir_;
  String contentOpfPath_;
  String tocNcxPath_;  // Path to toc.ncx file
  bool valid_;

  epub_reader* reader_;

  SpineItem* spine_;
  int spineCount_ = 0;
  size_t* spineSizes_ = nullptr;    // Uncompressed size of each spine item
  size_t* spineOffsets_ = nullptr;  // Cumulative offset for each spine item
  size_t totalBookSize_ = 0;        // Total size of all spine items

  std::vector<TocItem> toc_;

  CssParser* cssParser_ = nullptr;
  std::vector<String> cssFiles_;  // List of CSS file paths (relative to content.opf)
  bool cleanCacheOnStart_ = false;
};

#endif
