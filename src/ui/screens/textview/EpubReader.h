#ifndef EPUB_READER_H
#define EPUB_READER_H

#include <Arduino.h>
#include <SD.h>

extern "C" {
#include "../../../miniz/epub_parser.h"
}

/**
 * EpubReader - Handles EPUB file operations including extraction and caching
 *
 * This class manages:
 * - Opening EPUB files
 * - Extracting files to cache directory (only once)
 * - Providing access to extracted files
 */
class EpubReader {
 public:
  EpubReader(const char* epubPath);
  ~EpubReader();

  bool isValid() const {
    return valid_;
  }
  String getExtractDir() const {
    return extractDir_;
  }

  /**
   * Get a file from the EPUB - either from cache or extract it first
   * Returns the full path to the extracted file on SD card
   * Returns empty string if file not found or extraction failed
   */
  String getFile(const char* filename);

 private:
  bool openEpub();
  void closeEpub();
  bool ensureExtractDirExists();
  String getExtractedPath(const char* filename);
  bool isFileExtracted(const char* filename);
  bool extractFile(const char* filename);

  String epubPath_;
  String extractDir_;
  bool valid_;

  epub_reader* reader_;
};

#endif
