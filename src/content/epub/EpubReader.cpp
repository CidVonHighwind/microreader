#include "EpubReader.h"

#include <vector>

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

EpubReader::EpubReader(const char* epubPath)
    : epubPath_(epubPath), valid_(false), reader_(nullptr), spine_(nullptr), spineCount_(0) {
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
  Serial.printf("Extract directory: %s\n", extractDir_.c_str());

  if (!ensureExtractDirExists()) {
    return;
  }

  // Parse container.xml to get content.opf path
  if (!parseContainer()) {
    Serial.println("ERROR: Failed to parse container.xml");
    return;
  }

  // Parse content.opf to get spine items
  if (!parseContentOpf()) {
    Serial.println("ERROR: Failed to parse content.opf");
    return;
  }

  // Parse toc.ncx to get table of contents (optional - don't fail if missing)
  if (!tocNcxPath_.isEmpty()) {
    if (!parseTocNcx()) {
      Serial.println("WARNING: Failed to parse toc.ncx - TOC will be unavailable");
    }
  } else {
    Serial.println("INFO: No toc.ncx found in this EPUB");
  }

  // Parse CSS files for styling information (optional - don't fail if missing)
  if (!cssFiles_.empty()) {
    if (!parseCssFiles()) {
      Serial.println("WARNING: Failed to parse CSS files - styles will be unavailable");
    }
  } else {
    Serial.println("INFO: No CSS files found in this EPUB");
  }

  valid_ = true;
  Serial.println("EpubReader initialized successfully\\n");
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

bool EpubReader::extractToMemory(const char* filename, int (*callback)(const void*, size_t, void*), void* userData) {
  Serial.printf("\n=== Extracting %s to memory ===\n", filename);

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

  // Extract using streaming API with provided callback
  err = epub_extract_streaming(reader_, fileIndex, callback, userData, 4096);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: Extraction failed: %s\n", epub_get_error_string(err));
    return false;
  }

  Serial.printf("Successfully extracted %s to memory\n", filename);
  return true;
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

  Serial.printf("Parsing container: %s\n", containerPath.c_str());

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

  Serial.printf("Found content.opf: %s\n", contentOpfPath_.c_str());
  unsigned long endTime = millis();
  Serial.printf("Container parsing took %lu ms\n", endTime - startTime);

  return true;
}

bool EpubReader::parseContentOpf() {
  unsigned long startTime = millis();

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
  Serial.printf("Parsing content.opf: %s\n", opfPath.c_str());

  // Open parser once for the entire parsing
  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(opfPath.c_str())) {
    Serial.println("ERROR: Failed to open content.opf for parsing");
    delete parser;
    return false;
  }

  String tocId = "";
  std::vector<ManifestItem> manifest;
  std::vector<String> spineIdrefs;

  while (parser->read()) {
    SimpleXmlParser::NodeType nodeType = parser->getNodeType();
    String name = parser->getName();

    if (nodeType == SimpleXmlParser::Element) {
      if (strcasecmp_helper(name, "spine")) {
        tocId = parser->getAttribute("toc");
      } else if (strcasecmp_helper(name, "item")) {
        ManifestItem item;
        item.id = parser->getAttribute("id");
        item.href = parser->getAttribute("href");
        item.mediaType = parser->getAttribute("media-type");
        // Only collect xhtml/html and ncx items
        if (item.mediaType.indexOf("xhtml") >= 0 || item.mediaType.indexOf("html") >= 0 ||
            item.mediaType.indexOf("ncx") >= 0) {
          manifest.push_back(item);
        }
        if (item.mediaType.indexOf("css") >= 0) {
          String href = parser->getAttribute("href");
          if (!href.isEmpty()) {
            cssFiles_.push_back(href);
            Serial.printf("Found CSS file: %s\n", href.c_str());
          }
        }
      } else if (strcasecmp_helper(name, "itemref")) {
        String idref = parser->getAttribute("idref");
        if (!idref.isEmpty()) {
          spineIdrefs.push_back(idref);
        }
      }
    }
  }

  parser->close();
  delete parser;

  // Process tocId to find tocNcxPath_
  if (!tocId.isEmpty()) {
    for (auto& item : manifest) {
      if (item.id == tocId) {
        tocNcxPath_ = item.href;
        Serial.printf("Found toc.ncx reference: %s\n", tocNcxPath_.c_str());
        break;
      }
    }
  }

  // Build spine
  spineCount_ = spineIdrefs.size();
  spine_ = new SpineItem[spineCount_];
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
  spineSizes_ = new size_t[spineCount_];
  spineOffsets_ = new size_t[spineCount_];
  totalBookSize_ = 0;
  if (openEpub()) {
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
  }

  unsigned long endTime = millis();
  Serial.printf("\nSpine parsed successfully: %d items, total size: %u bytes\n", spineCount_, totalBookSize_);
  Serial.printf("Content.opf parsing took %lu ms\n", endTime - startTime);
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

  Serial.printf("Parsing toc.ncx: %s\n", extractedTocPath.c_str());

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

  Serial.printf("TOC parsed successfully: %d chapters/sections\n", (int)toc_.size());

  // // Print all TOC elements
  // for (int i = 0; i < (int)toc_.size(); i++) {
  //   Serial.printf("  [%d] %s -> %s", i, toc_[i].title.c_str(), toc_[i].href.c_str());
  //   if (!toc_[i].anchor.isEmpty()) {
  //     Serial.printf("#%s", toc_[i].anchor.c_str());
  //   }
  //   Serial.println();
  // }

  unsigned long endTime = millis();
  Serial.printf("TOC parsing took %lu ms\n", endTime - startTime);

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

  Serial.printf("CSS parsing complete: %d/%d files parsed, %d rules loaded\n", successCount, cssFiles_.size(),
                cssParser_->getStyleCount());

  unsigned long endTime = millis();
  Serial.printf("CSS parsing took %lu ms\n", endTime - startTime);

  return successCount > 0;
}
