#include "EpubWordProvider.h"

#include <Arduino.h>
#include <SD.h>

EpubWordProvider::EpubWordProvider(const char* path, size_t bufSize)
    : bufSize_(bufSize), fileSize_(0), currentChapter_(0) {
  epubPath_ = String(path);
  valid_ = false;
  isEpub_ = false;

  // Check if this is a direct XHTML file or an EPUB
  String pathStr = String(path);
  int len = pathStr.length();
  bool isXhtml = (len > 6 && pathStr.substring(len - 6) == ".xhtml") ||
                 (len > 5 && pathStr.substring(len - 5) == ".html") ||
                 (len > 4 && pathStr.substring(len - 4) == ".htm");

  if (isXhtml) {
    // Direct XHTML file - use it directly (no chapter support)
    isEpub_ = false;
    xhtmlPath_ = pathStr;

    // Convert the XHTML file into a text file for the FileWordProvider
    String txtPath;
    if (!convertXhtmlToTxt(xhtmlPath_, txtPath)) {
      return;
    }

    // Create the underlying FileWordProvider and validate it
    fileProvider_ = new FileWordProvider(txtPath.c_str(), bufSize_);
    if (!fileProvider_ || !fileProvider_->isValid()) {
      if (fileProvider_) {
        delete fileProvider_;
        fileProvider_ = nullptr;
      }
      return;
    }

    // Cache sizes and initialize position
    File f = SD.open(txtPath.c_str());
    if (f) {
      fileSize_ = f.size();
      f.close();
    }
    currentIndex_ = 0;
    valid_ = true;
  } else {
    // EPUB file - create and keep EpubReader for chapter navigation
    isEpub_ = true;
    epubReader_ = new EpubReader(path);
    if (!epubReader_->isValid()) {
      delete epubReader_;
      epubReader_ = nullptr;
      return;
    }

    // Open the first chapter (index 0)
    if (!openChapter(0)) {
      delete epubReader_;
      epubReader_ = nullptr;
      return;
    }

    valid_ = true;
  }
}

EpubWordProvider::~EpubWordProvider() {
  if (parser_) {
    parser_->close();
    delete parser_;
  }
  if (epubReader_) {
    delete epubReader_;
  }
  if (fileProvider_) {
    delete fileProvider_;
    fileProvider_ = nullptr;
  }
}

bool EpubWordProvider::isBlockElement(const String& name) {
  // List of elements we want to treat as paragraph/line-break boundaries.
  // Narrowed to elements that actually cause visual line breaks in typical HTML.
  if (name == "p" || name == "div" || name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
      name == "h6" || name == "blockquote" || name == "li" || name == "section" || name == "article" ||
      name == "header" || name == "footer" || name == "nav") {
    return true;
  }
  return false;
}

bool EpubWordProvider::isSkippedElement(const String& name) {
  // Elements whose content should be skipped entirely
  return name == "head" || name == "title" || name == "style" || name == "script";
}

bool EpubWordProvider::isHeaderElement(const String& name) {
  // Header elements that should have newlines after them
  return name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" || name == "h6";
}

bool EpubWordProvider::convertXhtmlToTxt(const String& srcPath, String& outTxtPath) {
  if (srcPath.isEmpty())
    return false;

  // Create output path by replacing extension with .txt
  String dest = srcPath;
  int lastDot = dest.lastIndexOf('.');
  if (lastDot >= 0) {
    dest = dest.substring(0, lastDot);
  }
  dest += ".txt";

  // Open input and output files
  SimpleXmlParser parser;
  if (!parser.open(srcPath.c_str()))
    return false;

  File out = SD.open(dest.c_str(), FILE_WRITE);
  if (!out) {
    parser.close();
    return false;
  }

  // Perform the conversion using common logic
  performXhtmlToTxtConversion(parser, out);

  // Cleanup and close
  parser.close();
  out.close();
  outTxtPath = dest;
  return true;
}

void EpubWordProvider::performXhtmlToTxtConversion(SimpleXmlParser& parser, File& out) {
  // State tracking
  String writeBuffer;
  const size_t FLUSH_THRESH = 2048;
  bool lastWasNewline = false;
  bool trimNextText = false;
  std::vector<String> elementStack;
  std::vector<String> classStack;
  // For paragraph class support: classes associated with the current paragraph element
  String pendingParagraphClasses = "";
  bool paragraphClassesWritten = false;

  int nodeCount = 0;
  int textNodeCount = 0;
  int elementCount = 0;

  // Helper lambda to trim trailing whitespace from buffer
  auto trimTrailingWhitespace = [&]() {
    while (writeBuffer.length() > 0) {
      char lastChar = writeBuffer.charAt(writeBuffer.length() - 1);
      if (lastChar == ' ' || lastChar == '\n' || lastChar == '\r' || lastChar == '\t') {
        writeBuffer = writeBuffer.substring(0, (int)writeBuffer.length() - 1);
      } else {
        break;
      }
    }
  };

  // Helper lambda to add newline (always after trimming trailing whitespace)
  auto addNewline = [&]() {
    writeBuffer += "\n";
    lastWasNewline = true;
    trimNextText = true;
    // When a paragraph ends, reset pending classes
    pendingParagraphClasses = "";
    paragraphClassesWritten = false;
  };

  // Helper lambda to flush buffer to disk
  auto flushBuffer = [&]() {
    if (writeBuffer.length() > 0) {
      out.print(writeBuffer);
      writeBuffer = "";
    }
  };

  // Helper lambda to check if we're inside any skipped element
  auto insideSkippedElement = [&]() {
    for (const auto& elem : elementStack) {
      if (isSkippedElement(elem)) {
        return true;
      }
    }
    return false;
  };

  // Parse and convert XHTML to plain text
  while (parser.read()) {
    nodeCount++;
    auto nodeType = parser.getNodeType();

    if (nodeType == SimpleXmlParser::Text) {
      textNodeCount++;
      // Use element stack instead of parser.getName() for streaming compatibility
      if (insideSkippedElement()) {
        continue;
      }

      // Extract and normalize text content
      String rawText;
      bool hasText = parser.hasMoreTextChars();
      while (parser.hasMoreTextChars()) {
        char c = parser.readTextNodeCharForward();
        if (c == '\r')
          continue;
        if (c == '\t')
          c = ' ';
        rawText += c;
      }

      // Collapse consecutive whitespace into single spaces
      String normalized;
      bool lastWasSpace = false;
      for (size_t i = 0; i < (size_t)rawText.length(); ++i) {
        char c = rawText.charAt(i);
        if (c == ' ' || c == '\n') {
          if (!lastWasSpace) {
            normalized += ' ';
            lastWasSpace = true;
          }
        } else {
          normalized += c;
          lastWasSpace = false;
        }
      }

      // Trim leading whitespace if we're starting a new block
      if (trimNextText && !normalized.isEmpty()) {
        size_t firstNonWs = 0;
        while (firstNonWs < (size_t)normalized.length() &&
               (normalized.charAt(firstNonWs) == ' ' || normalized.charAt(firstNonWs) == '\n'))
          firstNonWs++;
        if (firstNonWs > 0) {
          normalized = normalized.substring((int)firstNonWs);
        }
        trimNextText = false;
      }

      // Append to buffer
      if (!normalized.isEmpty()) {
        writeParagraphStyleToken(writeBuffer, pendingParagraphClasses, paragraphClassesWritten);
        writeBuffer += normalized;
        lastWasNewline = false;
      }

      // Periodic flush to avoid excessive memory use
      if (writeBuffer.length() > FLUSH_THRESH) {
        flushBuffer();
      }

    } else if (nodeType == SimpleXmlParser::Element) {
      elementCount++;
      String name = parser.getName();

      // Read class attribute for this element
      String classAttr = parser.getAttribute("class");

      // Only push non-empty elements to the stack
      // Self-closing elements (like <br/>, <meta/>, <link/>) don't need to be tracked
      if (!parser.isEmptyElement()) {
        elementStack.push_back(name);
        classStack.push_back(classAttr);
      }

      // If this is a paragraph/block element, treat classAttr as the classes for this paragraph
      if (isBlockElement(name)) {
        pendingParagraphClasses = classAttr;
        paragraphClassesWritten = false;
      }

      // Only add newlines for self-closing line break elements (br, hr)
      // Block elements get their newline when they close (in EndElement)
      bool isLineBreak = parser.isEmptyElement() && (name == "br" || name == "hr");

      if (isLineBreak) {
        trimTrailingWhitespace();
        addNewline();
      }

    } else if (nodeType == SimpleXmlParser::EndElement) {
      String name = parser.getName();

      if (isBlockElement(name) || isHeaderElement(name)) {
        trimTrailingWhitespace();
        addNewline();
      }

      // Pop element from stack
      if (!elementStack.empty()) {
        elementStack.pop_back();
        if (!classStack.empty()) {
          classStack.pop_back();
        }
      }
    }
  }

  // Final flush
  flushBuffer();
}

void EpubWordProvider::writeParagraphStyleToken(String& writeBuffer, const String& pendingParagraphClasses,
                                                bool& paragraphClassesWritten) {
  // If this is the beginning of a paragraph and classes haven't been written yet,
  // write the pending paragraph classes in front of the text line.
  if (!pendingParagraphClasses.isEmpty() && !paragraphClassesWritten) {
    // Emit style properties for the paragraph classes as an escaped token
    // Format: ESC[align=center] (ESC = 0x1B) followed by a space
    const CssParser* css = epubReader_ ? epubReader_->getCssParser() : nullptr;
    if (css) {
      CssStyle combined = css->getCombinedStyle(pendingParagraphClasses);
      String styleToken = "";
      if (combined.hasTextAlign) {
        styleToken += (char)27;  // ESC
        styleToken += "[";
        styleToken += "align=";
        switch (combined.textAlign) {
          case TextAlign::Left:
            styleToken += "left";
            break;
          case TextAlign::Right:
            styleToken += "right";
            break;
          case TextAlign::Center:
            styleToken += "center";
            break;
          case TextAlign::Justify:
            styleToken += "justify";
            break;
          default:
            styleToken += "left";
            break;
        }
        styleToken += "]";
      }
      if (styleToken.length() > 0) {
        styleToken += (char)27;  // trailing ESC
        writeBuffer += styleToken;
      }
    }
    paragraphClassesWritten = true;
  }
}

// Context for true streaming: EPUB -> Parser -> TXT
struct TrueStreamingContext {
  epub_stream_context* epubStream;
};

// Callback for SimpleXmlParser to pull data from EPUB stream
static int parser_stream_callback(char* buffer, size_t maxSize, void* userData) {
  TrueStreamingContext* ctx = (TrueStreamingContext*)userData;
  if (!ctx || !ctx->epubStream) {
    return -1;
  }

  // Pull next chunk from EPUB decompressor
  int bytesRead = epub_read_chunk(ctx->epubStream, buffer, maxSize);
  return bytesRead;
}

bool EpubWordProvider::convertXhtmlStreamToTxt(const char* epubFilename, String& outTxtPath) {
  if (!epubReader_) {
    return false;
  }

  // Compute output path
  String dest = epubReader_->getExtractedPath(epubFilename);
  int lastDot = dest.lastIndexOf('.');
  if (lastDot >= 0) {
    dest = dest.substring(0, lastDot);
  }
  dest += ".txt";

  // Start pull-based streaming from EPUB
  unsigned long startMs = millis();
  epub_stream_context* epubStream = epubReader_->startStreaming(epubFilename, 8192);
  if (!epubStream) {
    Serial.println("ERROR: Failed to start EPUB streaming");
    return false;
  }

  // Set up context for parser callback
  TrueStreamingContext streamCtx;
  streamCtx.epubStream = epubStream;

  // Open parser in streaming mode
  SimpleXmlParser parser;
  if (!parser.openFromStream(parser_stream_callback, &streamCtx)) {
    epub_end_streaming(epubStream);
    Serial.println("ERROR: Failed to open parser in streaming mode");
    return false;
  }

  // Remove existing file to ensure clean write
  if (SD.exists(dest.c_str())) {
    SD.remove(dest.c_str());
  }

  File out = SD.open(dest.c_str(), FILE_WRITE);
  if (!out) {
    Serial.println("ERROR: Failed to open output TXT file for writing");
    parser.close();
    epub_end_streaming(epubStream);
    return false;
  }

  // Perform the conversion using common logic
  // Parser pulls data from EPUB stream as needed
  performXhtmlToTxtConversion(parser, out);

  // Check how much was written
  size_t bytesWritten = out.size();

  parser.close();
  epub_end_streaming(epubStream);
  out.close();
  unsigned long elapsedMs = millis() - startMs;
  Serial.printf("Converted XHTML to TXT (streamed): %s — %lu ms\n", dest.c_str(), elapsedMs);
  outTxtPath = dest;
  return true;
}

bool EpubWordProvider::openChapter(int chapterIndex) {
  if (!epubReader_) {
    return false;
  }

  int spineCount = epubReader_->getSpineCount();
  if (chapterIndex < 0 || chapterIndex >= spineCount) {
    return false;
  }

  const SpineItem* spineItem = epubReader_->getSpineItem(chapterIndex);
  if (!spineItem) {
    return false;
  }

  // Build full path: content.opf is at OEBPS/content.opf, so hrefs are relative to OEBPS/
  String contentOpfPath = epubReader_->getContentOpfPath();
  String baseDir = "";
  int lastSlash = contentOpfPath.lastIndexOf('/');
  if (lastSlash >= 0) {
    baseDir = contentOpfPath.substring(0, lastSlash + 1);
  }
  String fullHref = baseDir + spineItem->href;

  // Close existing parser if any
  if (parser_) {
    parser_->close();
    delete parser_;
    parser_ = nullptr;
  }

  // Convert XHTML to text file using selected method
  String txtPath;
  if (useStreamingConversion_) {
    // Stream XHTML from EPUB directly to memory and convert (no intermediate XHTML file)
    if (!convertXhtmlStreamToTxt(fullHref.c_str(), txtPath)) {
      return false;
    }
  } else {
    // Extract XHTML file first, then convert from file
    String xhtmlPath = epubReader_->getFile(fullHref.c_str());
    if (xhtmlPath.isEmpty()) {
      return false;
    }
    if (!convertXhtmlToTxt(xhtmlPath, txtPath)) {
      return false;
    }
  }

  String newXhtmlPath = fullHref;  // Keep for tracking

  // Delete any previous file provider and create new one for this chapter
  if (fileProvider_) {
    delete fileProvider_;
    fileProvider_ = nullptr;
  }
  fileProvider_ = new FileWordProvider(txtPath.c_str(), bufSize_);
  if (!fileProvider_ || !fileProvider_->isValid()) {
    if (fileProvider_) {
      delete fileProvider_;
      fileProvider_ = nullptr;
    }
    return false;
  }

  xhtmlPath_ = newXhtmlPath;
  currentChapter_ = chapterIndex;
  // Cache file size
  File f = SD.open(txtPath.c_str());
  if (f) {
    fileSize_ = f.size();
    f.close();
  }

  // Cache the chapter name from TOC
  currentChapterName_ = epubReader_->getChapterNameForSpine(chapterIndex);

  // Initialize index to start of chapter; do not parse nodes yet
  currentIndex_ = 0;

  return true;
}

int EpubWordProvider::getChapterCount() {
  if (!epubReader_) {
    return 1;  // Single XHTML file = 1 chapter
  }
  return epubReader_->getSpineCount();
}

int EpubWordProvider::getCurrentChapter() {
  return currentChapter_;
}

bool EpubWordProvider::setChapter(int chapterIndex) {
  if (!isEpub_) {
    // Direct XHTML file - only chapter 0 is valid
    return chapterIndex == 0;
  }

  if (chapterIndex == currentChapter_) {
    // Already on this chapter, just reset to start
    reset();
    return true;
  }

  return openChapter(chapterIndex);
}

bool EpubWordProvider::hasNextWord() {
  if (fileProvider_)
    return fileProvider_->hasNextWord();
  return false;
}

bool EpubWordProvider::hasPrevWord() {
  if (fileProvider_)
    return fileProvider_->hasPrevWord();
  return false;
}

String EpubWordProvider::getNextWord() {
  if (!fileProvider_)
    return String("");
  return fileProvider_->getNextWord();
}

String EpubWordProvider::getPrevWord() {
  if (!fileProvider_)
    return String("");
  return fileProvider_->getPrevWord();
}

float EpubWordProvider::getPercentage() {
  if (!fileProvider_)
    return 1.0f;
  // For EPUBs, calculate book-wide percentage using chapter offset
  if (isEpub_ && epubReader_) {
    size_t totalSize = epubReader_->getTotalBookSize();
    if (totalSize == 0)
      return 1.0f;
    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t positionInChapter = static_cast<size_t>(fileProvider_->getCurrentIndex());
    size_t absolutePosition = chapterOffset + positionInChapter;
    return static_cast<float>(absolutePosition) / static_cast<float>(totalSize);
  }
  // Non-EPUB: delegate to file provider percentage
  return fileProvider_->getPercentage();
}

float EpubWordProvider::getPercentage(int index) {
  if (!fileProvider_)
    return 1.0f;
  if (isEpub_ && epubReader_) {
    size_t totalSize = epubReader_->getTotalBookSize();
    if (totalSize == 0)
      return 1.0f;
    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t absolutePosition = chapterOffset + static_cast<size_t>(index);
    return static_cast<float>(absolutePosition) / static_cast<float>(totalSize);
  }
  return fileProvider_->getPercentage(index);
}

float EpubWordProvider::getChapterPercentage() {
  if (!fileProvider_)
    return 1.0f;
  return fileProvider_->getPercentage();
}

float EpubWordProvider::getChapterPercentage(int index) {
  if (!fileProvider_)
    return 1.0f;
  return fileProvider_->getPercentage(index);
}

int EpubWordProvider::getCurrentIndex() {
  if (!fileProvider_)
    return 0;
  return fileProvider_->getCurrentIndex();
}

char EpubWordProvider::peekChar(int offset) {
  if (!fileProvider_)
    return '\0';
  return fileProvider_->peekChar(offset);
}

int EpubWordProvider::consumeChars(int n) {
  if (!fileProvider_)
    return 0;
  return fileProvider_->consumeChars(n);
}

bool EpubWordProvider::isInsideWord() {
  if (!fileProvider_)
    return false;
  return fileProvider_->isInsideWord();
}

void EpubWordProvider::ungetWord() {
  if (!fileProvider_)
    return;
  fileProvider_->ungetWord();
}

void EpubWordProvider::setPosition(int index) {
  if (!fileProvider_)
    return;
  fileProvider_->setPosition(index);
}

void EpubWordProvider::reset() {
  if (fileProvider_)
    fileProvider_->reset();
}
