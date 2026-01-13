#include "EpubReader.h"

#include <cstring>
#include <vector>
#ifdef TEST_BUILD
#include <filesystem>
#endif

#include "../xml/SimpleXmlParser.h"

// Helper function for case-insensitive string comparison
static bool strcasecmp_helper(const String& str1, const char* str2) {
  if (str1.length() != strlen(str2))
    return false;
  for (size_t i = 0; i < str1.length(); i++) {
    char c1 = str1.charAt(i);
    char c2 = str2[i];
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return false;
  }
  return true;
}

// Helper function to find next element with given name
static bool findNextElement(SimpleXmlParser* parser, const char* elementName) {
  while (parser->read()) {
    if (parser->getNodeType() == SimpleXmlParser::Element && strcasecmp_helper(parser->getName(), elementName)) {
      return true;
    }
  }
  return false;
}

// Memory logging helper
static void log_memory(const char* where) {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t heapSize = ESP.getHeapSize();
  uint32_t minFree = ESP.getMinFreeHeap();
  Serial.printf("  [MEM] %s: Free=%u, Total=%u, MinFree=%u\n", where, freeHeap, heapSize, minFree);
}

// File handle for extraction callback
static File g_extract_file;

// Metadata filename and current extract version. Update `CURRENT_EXTRACT_VERSION`
// whenever conversion/extraction format changes to force a cache reset.
static const char* EXTRACT_META_FILENAME = "epub_meta.txt";
static const char* CURRENT_EXTRACT_VERSION = "7";

// Callback to write extracted data to SD card file
static int extract_to_file_callback(const void* data, size_t size, void* user_data) {
  if (!g_extract_file) {
    return 0;  // File not open
  }

  size_t written = g_extract_file.write((const uint8_t*)data, size);
  return (written == size) ? 1 : 0;  // Return 1 for success, 0 for failure
}

// Callback to append extracted data into a String in memory
static int append_to_string_callback(const void* data, size_t size, void* user_data) {
  if (!user_data)
    return 0;
  String* dest = (String*)user_data;
  // Append binary-safe using existing String interface available in unit tests
  dest->reserve(dest->length() + (int)size);
  const char* bytes = (const char*)data;
  for (size_t i = 0; i < size; ++i) {
    *dest += bytes[i];
  }
  return 1;
}

EpubReader::EpubReader(const char* epubPath, bool cleanCacheOnStart)
    : epubPath_(epubPath),
      valid_(false),
      reader_(nullptr),
      spine_(nullptr),
      spineCount_(0),
      cleanCacheOnStart_(cleanCacheOnStart),
      language_("en") {
  Serial.printf("\n=== EpubReader: Opening %s ===\n", epubPath);

  // measure start time
  unsigned long startTime = millis();

  // Verify file exists
  File testFile = SD.open(epubPath);
  if (!testFile) {
    Serial.println("ERROR: Cannot open EPUB file");
    return;
  }
  size_t fileSize = testFile.size();
  testFile.close();
  Serial.printf("  EPUB file verified, size: %u bytes\n", fileSize);
  epubFileSize_ = fileSize;

  Serial.printf("  Time taken to verify EPUB file:  %lu ms\n", millis() - startTime);

  log_memory("constructor: after verify");

  // Create extraction directory based on EPUB filename
  String epubFilename = String(epubPath);
  int lastSlash = epubFilename.lastIndexOf('/');
  if (lastSlash < 0) {
    lastSlash = epubFilename.lastIndexOf('\\');
  }
  if (lastSlash >= 0) {
    epubFilename = epubFilename.substring(lastSlash + 1);
  }
  int lastDot = epubFilename.lastIndexOf('.');
  if (lastDot >= 0) {
    epubFilename = epubFilename.substring(0, lastDot);
  }

#ifdef TEST_BUILD
  extractDir_ = "test/output/epub_" + epubFilename;
#else
  extractDir_ = "/microreader/epub_" + epubFilename;
#endif
  Serial.printf("  Extract directory: %s\n", extractDir_.c_str());

  // Clean cache if requested
  if (cleanCacheOnStart_) {
    Serial.println("  Cleaning extract directory on startup...");
    cleanExtractDir();
  }

  if (!ensureExtractDirExists()) {
    return;
  }
  log_memory("constructor: after ensureExtractDirExists");

  // Check extract metadata and clear cache if needed (moved from ensureExtractDirExists)
  if (!checkAndUpdateExtractMeta()) {
    Serial.println("WARNING: Failed to check/update extract metadata");
  }

  // // Extract entire EPUB into extractDir_ and close the zip afterwards
  // Serial.println("  Extracting entire EPUB to cache (this may take a while)...");
  // if (!extractAll()) {
  //   Serial.println("WARNING: extractAll encountered errors or skipped some files");
  // }
  // // Close the zip to release memory used by central directory parsing
  // closeEpub();
  // log_memory("constructor: after ensureExtractDirExists");

  // Parse container.xml to get content.opf path
  if (!parseContainer()) {
    Serial.println("ERROR: Failed to parse container.xml");
    return;
  }
  log_memory("constructor: after parseContainer");

  // Parse content.opf to get spine items
  if (!parseContentOpf()) {
    Serial.println("ERROR: Failed to parse content.opf");
    return;
  }
  log_memory("constructor: after parseContentOpf");

  // Parse toc.ncx to get table of contents (optional - don't fail if missing)
  if (!tocNcxPath_.isEmpty()) {
    if (!parseTocNcx()) {
      Serial.println("WARNING: Failed to parse toc.ncx - TOC will be unavailable");
    }
  } else {
    Serial.println("INFO: No toc.ncx found in this EPUB");
  }
  log_memory("constructor: after parseTocNcx");

  // Parse CSS files for styling information (optional - don't fail if missing)
  if (!cssFiles_.empty()) {
    if (!parseCssFiles()) {
      Serial.println("WARNING: Failed to parse CSS files - styles will be unavailable");
    }
  } else {
    Serial.println("INFO: No CSS files found in this EPUB");
  }

  valid_ = true;
  unsigned long initMs = millis() - startTime;
  Serial.printf("  EpubReader init took  %lu ms\n", initMs);
  Serial.println("EpubReader initialized successfully");
}

EpubReader::~EpubReader() {
  closeEpub();
  if (spine_) {
    delete[] spine_;
    spine_ = nullptr;
  }
  if (spineSizes_) {
    delete[] spineSizes_;
    spineSizes_ = nullptr;
  }
  if (spineOffsets_) {
    delete[] spineOffsets_;
    spineOffsets_ = nullptr;
  }
  // TOC stored in std::vector now - automatic cleanup
  if (cssParser_) {
    delete cssParser_;
    cssParser_ = nullptr;
  }
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

  Serial.println("  EPUB opened for reading");
  return true;
}

void EpubReader::closeEpub() {
  if (reader_) {
    epub_close(reader_);
    reader_ = nullptr;
    Serial.println("  EPUB closed");
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

bool EpubReader::checkAndUpdateExtractMeta() {
  String metaPath = getExtractedPath(EXTRACT_META_FILENAME);

  // If meta exists, read version and compare
  if (SD.exists(metaPath.c_str())) {
    File f = SD.open(metaPath.c_str());
    if (f) {
      String contents = "";
      while (f.available()) {
        contents += (char)f.read();
      }
      f.close();

      int pos = contents.indexOf("version=");
      if (pos >= 0) {
        int eol = contents.indexOf('\n', pos);
        String ver;
        if (eol >= 0)
          ver = contents.substring(pos + 8, eol);
        else
          ver = contents.substring(pos + 8);
        ver.trim();

        // Parse filesize
        bool filesizeMatches = false;
        size_t metaFileSize = 0;
        int posSize = contents.indexOf("filesize=");
        if (posSize >= 0) {
          int eolSize = contents.indexOf('\n', posSize);
          String szStr;
          if (eolSize >= 0)
            szStr = contents.substring(posSize + 9, eolSize);
          else
            szStr = contents.substring(posSize + 9);
          szStr.trim();
          // Check if string is empty or invalid before parsing
          if (szStr.length() > 0 && szStr.charAt(0) >= '0' && szStr.charAt(0) <= '9') {
            metaFileSize = (size_t)szStr.toInt();
            filesizeMatches = (metaFileSize == epubFileSize_);
          } else {
            Serial.println("  Extract meta has invalid 'filesize' value - clearing cache");
          }
        } else {
          Serial.println("  Extract meta missing 'filesize' entry - clearing cache");
        }

        if (ver == CURRENT_EXTRACT_VERSION && filesizeMatches) {
          // Meta matches; nothing to do
          return true;
        }
        if (ver != CURRENT_EXTRACT_VERSION) {
          Serial.printf("  Extract meta version mismatch: found=%s expected=%s - clearing cache\n", ver.c_str(),
                        CURRENT_EXTRACT_VERSION);
        } else {
          Serial.printf("  Extract meta filesize mismatch: found=%u expected=%u - clearing cache\n",
                        (unsigned)metaFileSize, (unsigned)epubFileSize_);
        }
        // Remove entire extract dir to ensure clean state
        cleanExtractDir();
        // Recreate directory
        if (!SD.mkdir(extractDir_.c_str())) {
          Serial.printf("ERROR: Failed to recreate extract directory %s after cleaning\n", extractDir_.c_str());
          return false;
        }
      } else {
        Serial.println("  Extract meta missing 'version' entry - clearing cache");
        cleanExtractDir();
        if (!SD.mkdir(extractDir_.c_str())) {
          Serial.printf("ERROR: Failed to recreate extract directory %s after cleaning\n", extractDir_.c_str());
          return false;
        }
      }
    } else {
      Serial.printf("  WARNING: Could not open meta file %s for reading - clearing cache\n", metaPath.c_str());
      // If we couldn't open the meta file, treat as missing and clear cache
      cleanExtractDir();
      if (!SD.mkdir(extractDir_.c_str())) {
        Serial.printf("ERROR: Failed to recreate extract directory %s after cleaning\n", extractDir_.c_str());
        return false;
      }
    }
  } else {
    // Meta file missing - clear cache to start fresh
    Serial.printf("  Extract meta file not found (%s) - clearing cache\n", metaPath.c_str());
    cleanExtractDir();
    if (!SD.mkdir(extractDir_.c_str())) {
      Serial.printf("ERROR: Failed to recreate extract directory %s after cleaning\n", extractDir_.c_str());
      return false;
    }
  }

  // Write meta file with current version and filesize
  File out = SD.open(metaPath.c_str(), FILE_WRITE);
  if (!out) {
    Serial.printf("ERROR: Failed to write extract meta file %s\n", metaPath.c_str());
    return false;
  }
  out.print("version=");
  out.print(CURRENT_EXTRACT_VERSION);
  out.print("\n");
  out.print("filesize=");
  out.print(epubFileSize_);
  out.print("\n");
  out.close();
  Serial.printf("  Wrote extract metadata: %s\n", metaPath.c_str());
  return true;
}

// Recursively remove a directory using SD/File API on embedded target
static void removeDirRecursive(const String& path) {
  File dir = SD.open(path.c_str());
  if (!dir)
    return;
  if (dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      String name = String(file.name());
      String fullPath = path + "/" + name;
      if (file.isDirectory()) {
        removeDirRecursive(fullPath);
      } else {
        SD.remove(fullPath.c_str());
      }
      file.close();
      file = dir.openNextFile();
    }
  }
  // Try to remove directory itself (may not be supported on all SD implementations)
  SD.remove(path.c_str());
}

bool EpubReader::cleanExtractDir() {
  if (extractDir_.isEmpty())
    return true;
  removeDirRecursive(extractDir_);
  Serial.printf("  Removed extract directory (device): %s\n", extractDir_.c_str());
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
    Serial.printf("  File already extracted: %s\n", filename);
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

  unsigned long t0 = millis();
  // Log memory state before extraction
  uint32_t heapBefore = ESP.getFreeHeap();
  uint32_t heapSize = ESP.getHeapSize();
  uint32_t minFree = ESP.getMinFreeHeap();
  Serial.printf("  Memory before extraction: Free=%u, Total=%u, MinFree=%u\n", heapBefore, heapSize, minFree);

  err = epub_extract_streaming(reader_, fileIndex, extract_to_file_callback, nullptr, 4096);

  // Log memory state after extraction and show delta
  uint32_t heapAfter = ESP.getFreeHeap();
  int32_t heapDelta = (int32_t)heapAfter - (int32_t)heapBefore;
  Serial.printf("  Memory after extraction:  Free=%u (delta: %d)\n", heapAfter, heapDelta);
  g_extract_file.close();
  unsigned long extractMs = millis() - t0;
  Serial.printf("  Extraction took  %lu ms\n", extractMs);

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

epub_stream_context* EpubReader::startStreaming(const char* filename, size_t chunk_size) {
  // Open EPUB if not already open
  if (!openEpub()) {
    return nullptr;
  }

  // Find the file in the EPUB
  uint32_t fileIndex;
  epub_error err = epub_locate_file(reader_, filename, &fileIndex);
  if (err != EPUB_OK) {
    return nullptr;
  }

  // Start pull-based streaming
  return epub_start_streaming(reader_, fileIndex, chunk_size);
}

String EpubReader::getChapterNameForSpine(int spineIndex) const {
  // Get the spine item
  const SpineItem* spineItem = getSpineItem(spineIndex);
  if (spineItem == nullptr) {
    return String("");
  }

  // Search TOC for matching href
  // The spine href and TOC href should match (both are relative to content.opf)
  for (size_t i = 0; i < toc_.size(); i++) {
    if (toc_[i].href == spineItem->href) {
      return toc_[i].title;
    }
  }

  // No exact match found - return empty string
  return String("");
}

bool EpubReader::parseContainer() {
  // measure time taken
  unsigned long startTime = millis();

  // Get container.xml (will extract if needed)
  // Check if file is already extracted
  const char* filename = "META-INF/container.xml";
  String containerPath;

  if (isFileExtracted(filename)) {
    containerPath = getExtractedPath(filename);
  } else {
    // Need to extract it
    if (!extractFile(filename)) {
      Serial.println("ERROR: Failed to extract container.xml");
      return false;
    }
    containerPath = getExtractedPath(filename);
  }

  if (containerPath.isEmpty()) {
    Serial.println("ERROR: Failed to get container.xml path");
    return false;
  }

  Serial.printf("  Parsing container: %s\n", containerPath.c_str());

  // Parse container.xml to get content.opf path
  // Allocate parser on heap to avoid stack overflow (parser has 8KB buffer)
  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(containerPath.c_str())) {
    Serial.println("ERROR: Failed to open container.xml for parsing");
    delete parser;
    return false;
  }

  // Find <rootfile> element and get full-path attribute
  if (findNextElement(parser, "rootfile")) {
    contentOpfPath_ = parser->getAttribute("full-path");
  }

  parser->close();
  delete parser;

  if (contentOpfPath_.isEmpty()) {
    Serial.println("ERROR: Could not find content.opf path in container.xml");
    return false;
  }

  Serial.printf("    Found content.opf: %s\n", contentOpfPath_.c_str());
  unsigned long endTime = millis();
  Serial.printf("    Container parsing took  %lu ms\n", endTime - startTime);

  return true;
}

bool EpubReader::parseContentOpf() {
  unsigned long startTime = millis();

  // Memory debug: record memory before parsing
  uint32_t heapStart = ESP.getFreeHeap();
  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapMin = ESP.getMinFreeHeap();
  Serial.printf("  [MEM] parseContentOpf start: Free=%u, Total=%u, MinFree=%u\n", heapStart, heapTotal, heapMin);

  // Step 1: Locate and extract content.opf
  String opfPath;
  const char* filename = contentOpfPath_.c_str();
  if (isFileExtracted(filename)) {
    opfPath = getExtractedPath(filename);
  } else {
    if (!extractFile(filename)) {
      Serial.println("ERROR: Failed to extract content.opf");
      return false;
    }
    opfPath = getExtractedPath(filename);
  }
  if (opfPath.isEmpty()) {
    Serial.println("ERROR: Failed to get content.opf path");
    return false;
  }
  Serial.printf("  Parsing content.opf: %s\n", opfPath.c_str());

  // Open parser once for the entire parsing
  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(opfPath.c_str())) {
    Serial.println("ERROR: Failed to open content.opf for parsing");
    delete parser;
    return false;
  }

  // First pass: collect spine idrefs and toc id only. This avoids building a huge manifest
  unsigned long manifestStart = millis();
  String tocId = "";
  const size_t MAX_MANIFEST_ENTRIES = 100;  // Safety cap to limit RAM usage when manifest is huge
  std::vector<String> spineIdrefs;
  const size_t MAX_SPINE_ENTRIES = 100;  // Safety cap on spine idrefs too
  bool capMessagePrinted = false;

  while (parser->read()) {
    SimpleXmlParser::NodeType nodeType = parser->getNodeType();
    String name = parser->getName();
    if (nodeType == SimpleXmlParser::Element) {
      if (strcasecmp_helper(name, "spine")) {
        tocId = parser->getAttribute("toc");
      } else if (strcasecmp_helper(name, "itemref")) {
        String idref = parser->getAttribute("idref");
        if (!idref.isEmpty()) {
          if (spineIdrefs.size() >= MAX_SPINE_ENTRIES) {
            if (!capMessagePrinted) {
              Serial.printf("  [MEM] spineIdrefs reached cap (%u entries), skipping additional idrefs\n",
                            (unsigned)MAX_SPINE_ENTRIES);
              capMessagePrinted = true;
            }
          } else {
            spineIdrefs.push_back(idref);
          }
        }
      }
    }
  }

  unsigned long manifestMs = millis() - manifestStart;
  Serial.printf("  Spine idref collection took  %lu ms\n", manifestMs);

  // Memory debug: after manifest parsing
  uint32_t heapAfterManifest = ESP.getFreeHeap();
  int32_t deltaManifest = (int32_t)heapAfterManifest - (int32_t)heapStart;
  Serial.printf("  [MEM] after manifest: Free=%u (delta: %d)\n", heapAfterManifest, deltaManifest);

  parser->close();
  delete parser;

  // Second pass: reopen content.opf and collect only manifest entries referenced by the spine
  // (plus CSS files and the toc entry). This limits RAM usage for large manifests.
  std::vector<ManifestItem> manifest;
  if (!spineIdrefs.empty() || !tocId.isEmpty()) {
    // Re-open parser and scan manifest items. Allocate a fresh parser since the
    // original one was closed and deleted above.
    parser = new SimpleXmlParser();
    if (!parser->open(opfPath.c_str())) {
      Serial.println("ERROR: Failed to re-open content.opf for manifest parsing");
      delete parser;
      return false;
    }

    auto idIsNeeded = [&spineIdrefs, &tocId](const String& id) {
      if (id == tocId)
        return true;
      for (const auto& s : spineIdrefs) {
        if (s == id)
          return true;
      }
      return false;
    };

    while (parser->read()) {
      SimpleXmlParser::NodeType nodeType = parser->getNodeType();
      String name = parser->getName();
      if (nodeType == SimpleXmlParser::Element) {
        if (strcasecmp_helper(name, "item")) {
          String id = parser->getAttribute("id");
          String href = parser->getAttribute("href");
          String mediaType = parser->getAttribute("media-type");

          // Collect CSS files regardless (we want to parse styles)
          if (mediaType.indexOf("css") >= 0) {
            if (!href.isEmpty()) {
              cssFiles_.push_back(href);
              Serial.printf("    Found CSS file: %s\n", href.c_str());
            }
            continue;
          }

          // Only collect items that are referenced by the spine or are the toc
          if (!id.isEmpty() && idIsNeeded(id)) {
            ManifestItem item;
            item.id = id;
            item.href = href;
            item.mediaType = mediaType;
            if (manifest.size() >= MAX_MANIFEST_ENTRIES) {
              Serial.printf("  [MEM] manifest reached cap (%u entries), skipping additional items\n",
                            (unsigned)MAX_MANIFEST_ENTRIES);
            } else {
              manifest.push_back(item);
            }
          }
        }
      }
    }
    unsigned long manifestCollectMs = millis() - manifestStart - manifestMs;
    Serial.printf("  Manifest collection took  %lu ms\n", manifestCollectMs);

    parser->close();
    delete parser;
  }
  // Process tocId to find tocNcxPath_
  if (!tocId.isEmpty()) {
    for (auto& item : manifest) {
      if (item.id == tocId) {
        tocNcxPath_ = item.href;
        Serial.printf("    Found toc.ncx reference: %s\n", tocNcxPath_.c_str());
        break;
      }
    }
  }

  // Build spine
  spineCount_ = spineIdrefs.size();
  Serial.printf("  [MEM] before spine allocation: Free=%u, spineCount=%d\n", ESP.getFreeHeap(), spineCount_);
  spine_ = new SpineItem[spineCount_];
  Serial.printf("  [MEM] after spine allocation: Free=%u\n", ESP.getFreeHeap());
  for (int i = 0; i < spineCount_; i++) {
    spine_[i].idref = spineIdrefs[i];
    spine_[i].href = "";
    for (auto& item : manifest) {
      if (item.id == spine_[i].idref) {
        spine_[i].href = item.href;
        break;
      }
    }
    if (spine_[i].href.isEmpty()) {
      Serial.printf("WARNING: No manifest entry for idref: %s\n", spine_[i].idref.c_str());
    }
  }

  // Calculate spine item sizes
  Serial.printf("  [MEM] before spine size arrays: Free=%u\n", ESP.getFreeHeap());
  spineSizes_ = new size_t[spineCount_];
  spineOffsets_ = new size_t[spineCount_];
  Serial.printf("  [MEM] after spine size arrays: Free=%u\n", ESP.getFreeHeap());
  totalBookSize_ = 0;
  if (openEpub()) {
    unsigned long spineStart = millis();
    String baseDir = "";
    int lastSlash = contentOpfPath_.lastIndexOf('/');
    if (lastSlash >= 0) {
      baseDir = contentOpfPath_.substring(0, lastSlash + 1);
    }
    for (int i = 0; i < spineCount_; i++) {
      spineOffsets_[i] = totalBookSize_;
      String fullPath = baseDir + spine_[i].href;
      uint32_t fileIndex;
      epub_error err = epub_locate_file(reader_, fullPath.c_str(), &fileIndex);
      if (err == EPUB_OK) {
        epub_file_info info;
        err = epub_get_file_info(reader_, fileIndex, &info);
        if (err == EPUB_OK) {
          spineSizes_[i] = info.uncompressed_size;
          totalBookSize_ += info.uncompressed_size;
        } else {
          spineSizes_[i] = 0;
          Serial.printf("WARNING: Could not get file info for %s\n", fullPath.c_str());
        }
      } else {
        spineSizes_[i] = 0;
        Serial.printf("WARNING: Could not locate %s in EPUB\n", fullPath.c_str());
      }
    }
    closeEpub();
    unsigned long spineMs = millis() - spineStart;
    Serial.printf("  Spine size calculation took  %lu ms\n", spineMs);

    // Memory debug: after spine size calculation
    uint32_t heapAfterSpine = ESP.getFreeHeap();
    int32_t deltaSpine = (int32_t)heapAfterSpine - (int32_t)heapStart;
    Serial.printf("  [MEM] after spine calc: Free=%u (delta: %d)\n", heapAfterSpine, deltaSpine);
  }

  unsigned long endTime = millis();
  Serial.printf("  Spine parsed successfully: %d items, total size: %u bytes\n", spineCount_, totalBookSize_);
  // Memory debug: final state for parseContentOpf
  uint32_t heapEnd = ESP.getFreeHeap();
  int32_t deltaTotal = (int32_t)heapEnd - (int32_t)heapStart;
  Serial.printf("  [MEM] parseContentOpf end: Free=%u (delta: %d)\n", heapEnd, deltaTotal);
  Serial.printf("  Content.opf parsing took  %lu ms\n", endTime - startTime);

  // Parse metadata for language and other info
  if (!parseMetadata()) {
    Serial.println("WARNING: Failed to parse metadata");
  }

  return true;
}

bool EpubReader::extractAll() {
  if (!openEpub()) {
    Serial.println("ERROR: Cannot open EPUB for full extraction");
    return false;
  }

  uint32_t fileCount = epub_get_file_count(reader_);
  Serial.printf("  [EXTRACT] file count: %u\n", (unsigned)fileCount);

  for (uint32_t i = 0; i < fileCount; i++) {
    epub_file_info info;
    if (epub_get_file_info(reader_, i, &info) != EPUB_OK) {
      continue;
    }
    const char* filename = info.filename;
    if (!filename || filename[0] == '\0')
      continue;
    size_t len = strlen(filename);
    // Skip directory entries (trailing slash)
    if (len > 0 && (filename[len - 1] == '/' || filename[len - 1] == '\\'))
      continue;

    // If already extracted, skip
    if (isFileExtracted(filename))
      continue;

    Serial.printf("    Extracting: %s (size: %llu)\n", filename, (unsigned long long)info.uncompressed_size);

    String extractPath = getExtractedPath(filename);
    int lastSlash = extractPath.lastIndexOf('/');
    if (lastSlash < 0) {
      lastSlash = extractPath.lastIndexOf('\\');
    }
    if (lastSlash > 0) {
      String dirPath = extractPath.substring(0, lastSlash);
      int pos = 0;
      while (pos < dirPath.length()) {
        int nextSlash = dirPath.indexOf('/', pos + 1);
        if (nextSlash == -1)
          nextSlash = dirPath.length();
        String subDir = dirPath.substring(0, nextSlash);
        if (!SD.exists(subDir.c_str())) {
          if (!SD.mkdir(subDir.c_str())) {
            Serial.printf("ERROR: Failed to create directory %s\n", subDir.c_str());
            break;
          }
        }
        pos = nextSlash;
      }
    }

    // Open output file
    g_extract_file = SD.open(extractPath.c_str(), FILE_WRITE);
    if (!g_extract_file) {
      Serial.printf("ERROR: Failed to open file for writing: %s\n", extractPath.c_str());
      continue;
    }

    // Memory before extraction
    uint32_t heapBefore = ESP.getFreeHeap();
    epub_error err = epub_extract_streaming(reader_, i, extract_to_file_callback, nullptr, 4096);
    uint32_t heapAfter = ESP.getFreeHeap();
    int32_t delta = (int32_t)heapAfter - (int32_t)heapBefore;
    Serial.printf("      Memory after extraction: Free=%u (delta: %d)\n", heapAfter, delta);

    g_extract_file.close();

    if (err != EPUB_OK) {
      Serial.printf("ERROR: Extraction failed for %s: %s\n", filename, epub_get_error_string(err));
      // Remove partial file if written
      SD.remove(extractPath.c_str());
      // keep going with other files
    }
  }

  return true;
}

bool EpubReader::parseMetadata() {
  unsigned long startTime = millis();

  // Re-open content.opf for metadata parsing
  String opfPath = getExtractedPath(contentOpfPath_.c_str());
  if (opfPath.isEmpty()) {
    Serial.println("ERROR: Failed to get content.opf path for metadata parsing");
    return false;
  }

  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(opfPath.c_str())) {
    Serial.println("ERROR: Failed to open content.opf for metadata parsing");
    delete parser;
    return false;
  }

  bool inMetadata = false;
  while (parser->read()) {
    SimpleXmlParser::NodeType nodeType = parser->getNodeType();
    String name = parser->getName();

    if (nodeType == SimpleXmlParser::Element) {
      if (strcasecmp_helper(name, "metadata")) {
        inMetadata = true;
      } else if (inMetadata && (name.indexOf("language") >= 0)) {
        // Found language element, read the text content
        if (parser->read() && parser->getNodeType() == SimpleXmlParser::Text) {
          String lang = "";
          while (parser->hasMoreTextChars()) {
            char c = parser->readTextNodeCharForward();
            if (c != '\0') {
              lang += c;
            }
          }
          lang.trim();
          if (!lang.isEmpty()) {
            language_ = lang;
            Serial.printf("    Found language: %s\n", language_.c_str());
          }
        }
        break;  // Found language, can stop
      }
    } else if (nodeType == SimpleXmlParser::EndElement && strcasecmp_helper(name, "metadata")) {
      inMetadata = false;
    }
  }

  parser->close();
  delete parser;

  unsigned long endTime = millis();
  Serial.printf("    Metadata parsing took %lu ms\n", endTime - startTime);

  return true;
}

bool EpubReader::parseTocNcx() {
  unsigned long startTime = millis();

  // The toc.ncx path is relative to the content.opf location
  // We need to combine the content.opf directory with the toc.ncx path
  String tocPath = tocNcxPath_;

  // If content.opf is in a subdirectory, toc.ncx is likely there too
  int lastSlash = contentOpfPath_.lastIndexOf('/');
  if (lastSlash > 0) {
    String opfDir = contentOpfPath_.substring(0, lastSlash + 1);
    tocPath = opfDir + tocNcxPath_;
  }

  String extractedTocPath;
  if (isFileExtracted(tocPath.c_str())) {
    extractedTocPath = getExtractedPath(tocPath.c_str());
  } else {
    if (!extractFile(tocPath.c_str())) {
      Serial.printf("ERROR: Failed to extract toc.ncx: %s\n", tocPath.c_str());
      return false;
    }
    extractedTocPath = getExtractedPath(tocPath.c_str());
  }

  if (extractedTocPath.isEmpty()) {
    Serial.println("ERROR: Failed to get toc.ncx path");
    return false;
  }

  Serial.printf("  Parsing toc.ncx: %s\n", extractedTocPath.c_str());

  SimpleXmlParser* parser = new SimpleXmlParser();

  // Open toc.ncx from SD card to conserve RAM (avoid loading entire file into memory)
  if (!parser->open(extractedTocPath.c_str())) {
    Serial.println("ERROR: Failed to open toc.ncx for parsing");
    delete parser;
    return false;
  }

  // Use a temporary list to collect TOC items
  const int INITIAL_CAPACITY = 100;
  std::vector<TocItem> tempToc;
  tempToc.reserve(INITIAL_CAPACITY);

  // Parse <navPoint> elements using a simpler approach:
  // For each navPoint, find its direct navLabel/text and content elements
  // Structure: <navPoint><navLabel><text>Title</text></navLabel><content src="file.xhtml#anchor"/></navPoint>

  String currentTitle = "";
  String currentSrc = "";
  bool inNavPoint = false;
  bool inNavLabel = false;
  bool expectingText = false;

  while (parser->read()) {
    SimpleXmlParser::NodeType nodeType = parser->getNodeType();

    if (nodeType == SimpleXmlParser::Element) {
      String name = parser->getName();
      if (strcasecmp_helper(name, "navPoint")) {
        // Starting a new navPoint - if we were already inside one, commit it
        // This handles parent navPoints that contain nested navPoints
        if (inNavPoint && !currentTitle.isEmpty() && !currentSrc.isEmpty()) {
          TocItem item;
          int hashPos = currentSrc.indexOf('#');
          if (hashPos >= 0) {
            item.href = currentSrc.substring(0, hashPos);
            item.anchor = currentSrc.substring(hashPos + 1);
          } else {
            item.href = currentSrc;
            item.anchor = "";
          }
          currentTitle.trim();
          item.title = currentTitle;
          tempToc.push_back(item);
        }

        // Reset state for new entry
        currentTitle = "";
        currentSrc = "";
        inNavPoint = true;
      } else if (strcasecmp_helper(name, "navLabel")) {
        inNavLabel = true;
      } else if (strcasecmp_helper(name, "text") && inNavLabel) {
        expectingText = true;
      } else if (strcasecmp_helper(name, "content") && inNavPoint) {
        // Only capture content if we don't have one yet for this navPoint
        if (currentSrc.isEmpty()) {
          currentSrc = parser->getAttribute("src");
        }
      }
    } else if (nodeType == SimpleXmlParser::Text && expectingText) {
      // Read the title text - only if we don't have one yet
      if (currentTitle.isEmpty()) {
        currentTitle = "";
        while (parser->hasMoreTextChars()) {
          char c = parser->readTextNodeCharForward();
          if (c != '\0') {
            currentTitle += c;
          }
        }
      }
      expectingText = false;
    } else if (nodeType == SimpleXmlParser::EndElement) {
      String name = parser->getName();
      if (strcasecmp_helper(name, "navLabel")) {
        inNavLabel = false;
      } else if (strcasecmp_helper(name, "text")) {
        expectingText = false;
      } else if (strcasecmp_helper(name, "navPoint")) {
        // End of navPoint - commit the collected entry
        if (!currentTitle.isEmpty() && !currentSrc.isEmpty()) {
          TocItem item;
          int hashPos = currentSrc.indexOf('#');
          if (hashPos >= 0) {
            item.href = currentSrc.substring(0, hashPos);
            item.anchor = currentSrc.substring(hashPos + 1);
          } else {
            item.href = currentSrc;
            item.anchor = "";
          }
          currentTitle.trim();
          item.title = currentTitle;
          tempToc.push_back(item);
        }
        inNavPoint = false;

        // Reset state to be ready for possible siblings
        currentTitle = "";
        currentSrc = "";
        inNavLabel = false;
        expectingText = false;
      }
    }
  }

  // Move temporary vector into member TOC
  toc_ = std::move(tempToc);

  parser->close();
  delete parser;

  Serial.printf("    TOC parsed successfully: %d chapters/sections\n", (int)toc_.size());

  // // Print all TOC elements
  // for (int i = 0; i < (int)toc_.size(); i++) {
  //   Serial.printf("  [%d] %s -> %s", i, toc_[i].title.c_str(), toc_[i].href.c_str());
  //   if (!toc_[i].anchor.isEmpty()) {
  //     Serial.printf("#%s", toc_[i].anchor.c_str());
  //   }
  //   Serial.println();
  // }

  unsigned long endTime = millis();
  Serial.printf("    TOC parsing took  %lu ms\n", endTime - startTime);

  return true;
}

bool EpubReader::parseCssFiles() {
  unsigned long startTime = millis();

  if (cssFiles_.empty()) {
    return true;  // Nothing to parse
  }

  // Create CSS parser
  cssParser_ = new CssParser();

  // Get base directory of content.opf (CSS paths are relative to this)
  String baseDir = "";
  int lastSlash = contentOpfPath_.lastIndexOf('/');
  if (lastSlash >= 0) {
    baseDir = contentOpfPath_.substring(0, lastSlash + 1);
  }

  int successCount = 0;
  for (size_t i = 0; i < cssFiles_.size(); i++) {
    // Build full path relative to EPUB root
    String fullPath = baseDir + cssFiles_[i];

    // Extract CSS file if needed
    String extractedPath;
    if (isFileExtracted(fullPath.c_str())) {
      extractedPath = getExtractedPath(fullPath.c_str());
    } else {
      if (!extractFile(fullPath.c_str())) {
        Serial.printf("WARNING: Failed to extract CSS file: %s\n", fullPath.c_str());
        continue;
      }
      extractedPath = getExtractedPath(fullPath.c_str());
    }

    // Parse the CSS file
    if (cssParser_->parseFile(extractedPath.c_str())) {
      successCount++;
    }
  }

  Serial.printf("  CSS parsing complete: %d/%d files parsed, %d rules loaded\n", successCount, cssFiles_.size(),
                cssParser_->getStyleCount());

  unsigned long endTime = millis();
  Serial.printf("CSS parsing took  %lu ms\n", endTime - startTime);

  return successCount > 0;
}
