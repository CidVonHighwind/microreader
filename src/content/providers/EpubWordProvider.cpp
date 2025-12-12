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

bool EpubWordProvider::createDirRecursive(const String& path) {
  if (SD.exists(path.c_str()))
    return true;
  int slash = path.lastIndexOf('/');
  if (slash <= 0)
    return true;  // root or no slash
  String parent = path.substring(0, slash);
  if (!createDirRecursive(parent))
    return false;
  return SD.mkdir(path.c_str());
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

bool EpubWordProvider::isInlineStyleElement(const String& name) {
  // Inline elements that can apply bold/italic styling to text
  return name == "b" || name == "strong" || name == "i" || name == "em" || name == "span";
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

  // Create directories if needed
  int lastSlash = dest.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = dest.substring(0, lastSlash);
    createDirRecursive(dir);
  }

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
  const size_t FLUSH_THRESHOLD = 2048;

  String buffer;                         // Output buffer
  std::vector<String> elementStack;      // Track nested elements
  std::vector<bool> inlineStyleEmitted;  // Track which inline style elements emitted tokens
  String pendingParagraphClasses;        // CSS classes for current block
  String pendingInlineStyle;             // Inline style attribute for current block
  bool paragraphClassesWritten = false;  // Have we written style token?
  bool lineHasContent = false;           // Does current line have visible content?
  bool lineHasNbsp = false;              // Does current line have &nbsp;?

  while (parser.read()) {
    SimpleXmlParser::NodeType nodeType = parser.getNodeType();

    // ========== START ELEMENT ==========
    if (nodeType == SimpleXmlParser::Element) {
      String name = parser.getName();

      // Track non-self-closing elements
      if (!parser.isEmptyElement()) {
        elementStack.push_back(name);
      }

      // Block elements: add newline before if current line has content
      // This ensures blockquotes, nested divs, etc. start on a new line
      if (isBlockElement(name) && lineHasContent) {
        buffer += "\n";
        lineHasContent = false;
        lineHasNbsp = false;
      }

      // Capture CSS classes and inline styles for block elements
      if (isBlockElement(name)) {
        pendingParagraphClasses = parser.getAttribute("class");
        pendingInlineStyle = parser.getAttribute("style");
        paragraphClassesWritten = false;
      }

      // Handle inline style elements (b, strong, i, em, span)
      if (isInlineStyleElement(name) && !parser.isEmptyElement()) {
        String classAttr = parser.getAttribute("class");
        String styleAttr = parser.getAttribute("style");
        bool emitted = writeInlineStyleToken(buffer, name, classAttr, styleAttr);
        inlineStyleEmitted.push_back(emitted);
      }

      // Handle <br/> - only add newline if line has content
      if (parser.isEmptyElement() && (name == "br" || name == "hr")) {
        if (lineHasContent) {
          buffer += "\n";
          lineHasContent = false;
          lineHasNbsp = false;
        }
      }
    }

    // ========== END ELEMENT ==========
    else if (nodeType == SimpleXmlParser::EndElement) {
      String name = parser.getName();

      // Handle end of inline style elements
      if (isInlineStyleElement(name) && !inlineStyleEmitted.empty()) {
        bool wasEmitted = inlineStyleEmitted.back();
        inlineStyleEmitted.pop_back();
        if (wasEmitted) {
          writeStyleResetToken(buffer);
        }
      }

      // Block elements: add newline if line had content OR had &nbsp;
      if (isBlockElement(name) || isHeaderElement(name)) {
        if (lineHasContent || lineHasNbsp) {
          buffer += "\n";
        }
        lineHasContent = false;
        lineHasNbsp = false;
        pendingParagraphClasses = "";
        pendingInlineStyle = "";
        paragraphClassesWritten = false;
      }

      // Pop from element stack
      if (!elementStack.empty()) {
        elementStack.pop_back();
      }
    }

    // ========== TEXT NODE ==========
    else if (nodeType == SimpleXmlParser::Text) {
      // Skip if inside <head>, <style>, <script>
      if (isInsideSkippedElement(elementStack)) {
        continue;
      }

      // Read and process text
      String text = readAndDecodeText(parser);
      if (text.isEmpty()) {
        continue;
      }

      // Check for &nbsp; before normalizing (marks intentional spacing)
      if (text.indexOf('\xA0') >= 0) {
        lineHasNbsp = true;
      }

      // Normalize: collapse whitespace, convert nbsp to space
      text = normalizeWhitespace(text);
      if (text.isEmpty()) {
        continue;
      }

      // Trim leading space if at line start
      if (!lineHasContent) {
        text = trimLeadingSpaces(text);
        if (text.isEmpty()) {
          continue;
        }
      }

      // Write style token at start of paragraph
      writeParagraphStyleToken(buffer, pendingParagraphClasses, pendingInlineStyle, paragraphClassesWritten);

      // Append text
      buffer += text;
      lineHasContent = true;
    }

    // Periodic flush
    if (buffer.length() > FLUSH_THRESHOLD) {
      out.print(buffer);
      buffer = "";
    }
  }

  // Final flush
  if (buffer.length() > 0) {
    out.print(buffer);
  }
}

// Helper functions removed - now inlined in performXhtmlToTxtConversion
// processStartElement, processEndElement, processTextNode are no longer used

bool EpubWordProvider::isInsideSkippedElement(const std::vector<String>& elementStack) {
  for (const String& elem : elementStack) {
    if (isSkippedElement(elem)) {
      return true;
    }
  }
  return false;
}

String EpubWordProvider::readAndDecodeText(SimpleXmlParser& parser) {
  String result;

  while (parser.hasMoreTextChars()) {
    char c = parser.readTextNodeCharForward();

    // Skip carriage returns
    if (c == '\r') {
      continue;
    }

    // Convert tabs to spaces
    if (c == '\t') {
      c = ' ';
    }

    // Decode HTML entities
    if (c == '&') {
      String entity = "&";
      while (parser.hasMoreTextChars()) {
        char next = parser.peekTextNodeChar();
        entity += next;
        parser.readTextNodeCharForward();
        if (next == ';' || entity.length() > 10) {
          break;
        }
      }
      result += decodeHtmlEntity(entity);
    } else {
      result += c;
    }
  }

  return result;
}

String EpubWordProvider::decodeHtmlEntity(const String& entity) {
  if (entity == "&nbsp;")
    return "\xA0";  // Non-breaking space (0xA0) - used to detect intentional blank lines
  if (entity == "&amp;")
    return "&";
  if (entity == "&lt;")
    return "<";
  if (entity == "&gt;")
    return ">";
  if (entity == "&quot;")
    return "\"";
  if (entity == "&apos;")
    return "'";
  // Unknown entity - return as-is
  return entity;
}

String EpubWordProvider::normalizeWhitespace(const String& text) {
  String result;
  bool lastWasSpace = false;

  for (int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);

    // Convert non-breaking space to regular space
    if (c == '\xA0') {
      c = ' ';
    }

    bool isSpace = (c == ' ' || c == '\n');

    if (isSpace) {
      if (!lastWasSpace) {
        result += ' ';
        lastWasSpace = true;
      }
    } else {
      result += c;
      lastWasSpace = false;
    }
  }

  return result;
}

void EpubWordProvider::trimTrailingSpaces(String& buffer) {
  while (buffer.length() > 0) {
    char last = buffer.charAt(buffer.length() - 1);
    if (last == ' ' || last == '\t') {
      buffer = buffer.substring(0, buffer.length() - 1);
    } else {
      break;
    }
  }
}

String EpubWordProvider::trimLeadingSpaces(const String& text) {
  int start = 0;
  while (start < text.length() && (text.charAt(start) == ' ' || text.charAt(start) == '\n')) {
    start++;
  }
  return text.substring(start);
}

void EpubWordProvider::writeParagraphStyleToken(String& writeBuffer, const String& pendingParagraphClasses,
                                                const String& pendingInlineStyle, bool& paragraphClassesWritten) {
  // If this is the beginning of a paragraph and styles haven't been written yet,
  // write the style token in front of the text line.
  // We check for either class-based styles or inline styles.
  if ((!pendingParagraphClasses.isEmpty() || !pendingInlineStyle.isEmpty()) && !paragraphClassesWritten) {
    // Emit style properties for the paragraph using ESC + command byte format
    // Alignment: ESC+'L'(left), ESC+'R'(right), ESC+'C'(center), ESC+'J'(justify)
    // Style: ESC+'B'(bold), ESC+'I'(italic), ESC+'X'(bold+italic)
    const CssParser* css = epubReader_ ? epubReader_->getCssParser() : nullptr;

    // Start with class-based styles
    CssStyle combined;
    if (css && !pendingParagraphClasses.isEmpty()) {
      combined = css->getCombinedStyle(pendingParagraphClasses);
    }

    // Merge inline styles (inline styles take precedence over class styles)
    if (css && !pendingInlineStyle.isEmpty()) {
      CssStyle inlineStyle = css->parseInlineStyle(pendingInlineStyle);
      combined.merge(inlineStyle);
    }

    String styleToken = "";
    if (combined.hasTextAlign) {
      styleToken += (char)0x1B;  // ESC
      switch (combined.textAlign) {
        case TextAlign::Left:
          styleToken += 'L';
          break;
        case TextAlign::Right:
          styleToken += 'R';
          break;
        case TextAlign::Center:
          styleToken += 'C';
          break;
        case TextAlign::Justify:
          styleToken += 'J';
          break;
        default:
          styleToken += 'L';
          break;
      }
    }

    // Add bold/italic style tokens
    if (combined.hasFontWeight && combined.fontWeight == CssFontWeight::Bold) {
      if (combined.hasFontStyle && combined.fontStyle == CssFontStyle::Italic) {
        // Bold + Italic
        styleToken += (char)0x1B;  // ESC
        styleToken += 'X';
      } else {
        // Bold only
        styleToken += (char)0x1B;  // ESC
        styleToken += 'B';
      }
    } else if (combined.hasFontStyle && combined.fontStyle == CssFontStyle::Italic) {
      // Italic only
      styleToken += (char)0x1B;  // ESC
      styleToken += 'I';
    }

    if (styleToken.length() > 0) {
      writeBuffer += styleToken;
    }
    paragraphClassesWritten = true;
  }
}

bool EpubWordProvider::writeInlineStyleToken(String& writeBuffer, const String& elementName, const String& classAttr,
                                             const String& styleAttr) {
  // Determine style from element name and/or CSS classes
  bool isBold = false;
  bool isItalic = false;

  // Check element name for implicit styling
  if (elementName == "b" || elementName == "strong") {
    isBold = true;
  } else if (elementName == "i" || elementName == "em") {
    isItalic = true;
  }

  // Check CSS classes and inline styles for additional styling
  const CssParser* css = epubReader_ ? epubReader_->getCssParser() : nullptr;
  if (css) {
    CssStyle combined;

    // Get class-based styles
    if (!classAttr.isEmpty()) {
      combined = css->getCombinedStyle(classAttr);
    }

    // Merge inline styles
    if (!styleAttr.isEmpty()) {
      CssStyle inlineStyle = css->parseInlineStyle(styleAttr);
      combined.merge(inlineStyle);
    }

    // Apply CSS-defined styles
    if (combined.hasFontWeight && combined.fontWeight == CssFontWeight::Bold) {
      isBold = true;
    }
    if (combined.hasFontStyle && combined.fontStyle == CssFontStyle::Italic) {
      isItalic = true;
    }
  }

  // Only emit token if there's a style to apply
  // Format: ESC + 'B'(bold), 'I'(italic), 'X'(bold+italic)
  if (isBold || isItalic) {
    writeBuffer += (char)0x1B;  // ESC

    if (isBold && isItalic) {
      writeBuffer += 'X';
    } else if (isBold) {
      writeBuffer += 'B';
    } else {
      writeBuffer += 'I';
    }

    return true;  // Style token was emitted
  }

  return false;  // No style token emitted
}

void EpubWordProvider::writeStyleResetToken(String& writeBuffer) {
  // Emit a token to reset back to normal style
  // Format: ESC + 'b'(end bold), 'i'(end italic), 'x'(end bold+italic)
  // We use 'x' as a universal reset since we don't track which style was active
  writeBuffer += (char)0x1B;  // ESC
  writeBuffer += 'x';         // Reset all styles
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

  // Create directories if needed
  int lastSlash = dest.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = dest.substring(0, lastSlash);
    createDirRecursive(dir);
  }

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
    Serial.printf("ERROR: Failed to open output TXT file '%s' for writing\n", dest.c_str());
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

StyledWord EpubWordProvider::getNextWord() {
  if (!fileProvider_)
    return StyledWord();
  return fileProvider_->getNextWord();
}

StyledWord EpubWordProvider::getPrevWord() {
  if (!fileProvider_)
    return StyledWord();
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
