#include "EpubReader.h"

// File handle for extraction callback
static File g_extract_file;

// Callback to write extracted data to SD card file
static int extract_to_file_callback(const void* data, size_t size, void* user_data) {
  if (!g_extract_file) {
    return 0;  // File not open
  }

  size_t written = g_extract_file.write((const uint8_t*)data, size);
  return (written == size) ? 1 : 0;  // Return 1 for success, 0 for failure
}

EpubReader::EpubReader(const char* epubPath) : epubPath_(epubPath), valid_(false), reader_(nullptr) {
  Serial.printf("\n=== EpubReader: Opening %s ===\n", epubPath);

  // Verify file exists
  File testFile = SD.open(epubPath);
  if (!testFile) {
    Serial.println("ERROR: Cannot open EPUB file");
    return;
  }
  size_t fileSize = testFile.size();
  testFile.close();
  Serial.printf("EPUB file verified, size: %u bytes\n", fileSize);

  // Create extraction directory based on EPUB filename
  String epubFilename = String(epubPath);
  int lastSlash = epubFilename.lastIndexOf('/');
  if (lastSlash >= 0) {
    epubFilename = epubFilename.substring(lastSlash + 1);
  }
  int lastDot = epubFilename.lastIndexOf('.');
  if (lastDot >= 0) {
    epubFilename = epubFilename.substring(0, lastDot);
  }

  extractDir_ = "/epub_" + epubFilename;
  Serial.printf("Extract directory: %s\n", extractDir_.c_str());

  if (!ensureExtractDirExists()) {
    return;
  }

  valid_ = true;
  Serial.println("EpubReader initialized successfully\n");
}

EpubReader::~EpubReader() {
  closeEpub();
  Serial.println("EpubReader destroyed");
}

bool EpubReader::openEpub() {
  if (reader_) {
    return true;  // Already open
  }

  epub_error err = epub_open(epubPath_.c_str(), &reader_);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: Failed to open EPUB: %s\n", epub_get_error_string(err));
    reader_ = nullptr;
    return false;
  }

  Serial.println("EPUB opened for reading");
  return true;
}

void EpubReader::closeEpub() {
  if (reader_) {
    epub_close(reader_);
    reader_ = nullptr;
    Serial.println("EPUB closed");
  }
}

bool EpubReader::ensureExtractDirExists() {
  if (!SD.exists(extractDir_.c_str())) {
    if (!SD.mkdir(extractDir_.c_str())) {
      Serial.printf("ERROR: Failed to create directory %s\n", extractDir_.c_str());
      return false;
    }
    Serial.printf("Created directory: %s\n", extractDir_.c_str());
  }
  return true;
}

String EpubReader::getExtractedPath(const char* filename) {
  String path = extractDir_ + "/" + String(filename);
  return path;
}

bool EpubReader::isFileExtracted(const char* filename) {
  String path = getExtractedPath(filename);
  bool exists = SD.exists(path.c_str());
  if (exists) {
    Serial.printf("File already extracted: %s\n", filename);
  }
  return exists;
}

bool EpubReader::extractFile(const char* filename) {
  Serial.printf("\n=== Extracting %s ===\n", filename);

  // Open EPUB if not already open
  if (!openEpub()) {
    return false;
  }

  // Find the file in the EPUB
  uint32_t fileIndex;
  epub_error err = epub_locate_file(reader_, filename, &fileIndex);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: File not found in EPUB: %s\n", filename);
    return false;
  }

  // Get file info
  epub_file_info info;
  err = epub_get_file_info(reader_, fileIndex, &info);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: Failed to get file info: %s\n", epub_get_error_string(err));
    return false;
  }

  Serial.printf("Found file at index %d (size: %u bytes)\n", fileIndex, info.uncompressed_size);

  // Create subdirectories if needed
  String extractPath = getExtractedPath(filename);
  int lastSlash = extractPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String dirPath = extractPath.substring(0, lastSlash);

    // Create all parent directories
    int pos = 0;
    while (pos < dirPath.length()) {
      int nextSlash = dirPath.indexOf('/', pos + 1);
      if (nextSlash == -1) {
        nextSlash = dirPath.length();
      }

      String subDir = dirPath.substring(0, nextSlash);
      if (!SD.exists(subDir.c_str())) {
        if (!SD.mkdir(subDir.c_str())) {
          Serial.printf("ERROR: Failed to create directory %s\n", subDir.c_str());
          return false;
        }
      }

      pos = nextSlash;
    }
  }

  // Extract to file
  Serial.printf("Extracting to: %s\n", extractPath.c_str());

  g_extract_file = SD.open(extractPath.c_str(), FILE_WRITE);
  if (!g_extract_file) {
    Serial.printf("ERROR: Failed to open file for writing: %s\n", extractPath.c_str());
    return false;
  }

  err = epub_extract_streaming(reader_, fileIndex, extract_to_file_callback, nullptr, 4096);
  g_extract_file.close();

  if (err != EPUB_OK) {
    Serial.printf("ERROR: Extraction failed: %s\n", epub_get_error_string(err));
    return false;
  }

  Serial.printf("Successfully extracted %s\n", filename);
  return true;
}

String EpubReader::getFile(const char* filename) {
  if (!valid_) {
    Serial.println("ERROR: EpubReader not valid");
    return String("");
  }

  // Check if file is already extracted
  if (isFileExtracted(filename)) {
    return getExtractedPath(filename);
  }

  // Need to extract it
  if (!extractFile(filename)) {
    return String("");
  }

  return getExtractedPath(filename);
}
