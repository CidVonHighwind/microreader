#include "EpubWordProvider.h"

#include <Arduino.h>
#include <SD.h>
#include <ctype.h>

#include <cmath>  // for std::round
#include <vector>

// #define EPUB_DEBUG_CLEAN_CACHE

// Helper function to map language string to Language enum
static Language stringToLanguage(const String& langStr) {
  String lang = langStr;
  lang.toLowerCase();

  if ((lang.length() >= 2 && lang.substring(0, 2) == "en")) {
    return Language::ENGLISH;
  } else if ((lang.length() >= 2 && lang.substring(0, 2) == "de")) {
    return Language::GERMAN;
  } else {
    // Default to english if unknown
    return Language::ENGLISH;
  }
}

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
#if defined(EPUB_DEBUG_CLEAN_CACHE) || defined(TEST_BUILD)
    epubReader_ = new EpubReader(path, true);
#else
    epubReader_ = new EpubReader(path);
#endif
    if (!epubReader_->isValid()) {
      delete epubReader_;
      epubReader_ = nullptr;
      Serial.printf("ERROR: Failed to open EPUB file: %s\n", path);
      return;
    }

    currentIndex_ = 0;
    currentChapter_ = -1;

    // // Open the first chapter (index 0)
    // if (!openChapter(0)) {
    //   delete epubReader_;
    //   epubReader_ = nullptr;
    //   return;
    // }

    valid_ = true;
    Serial.printf("Opened EPUB file: %s with %d chapters\n", path, epubReader_->getSpineCount());
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

bool EpubWordProvider::convertXhtmlToTxt(const String& srcPath, String& outTxtPath, ConversionTimings* timings) {
  if (srcPath.isEmpty())
    return false;

  // Create output path by replacing extension with .txt
  String dest = srcPath;
  int lastDot = dest.lastIndexOf('.');
  if (lastDot >= 0) {
    dest = dest.substring(0, lastDot);
  }
  dest += ".txt";

  // If the TXT file already exists and is non-empty, reuse it and skip conversion
  if (SD.exists(dest.c_str())) {
    File chk = SD.open(dest.c_str());
    if (chk) {
      size_t sz = chk.size();
      chk.close();
      if (sz > 0) {
        if (timings) {
          timings->total = 0;
          timings->parserOpen = 0;
          timings->outOpen = 0;
          timings->conversion = 0;
          timings->parserClose = 0;
          timings->closeOut = 0;
          timings->bytes = sz;
        }
        Serial.printf("  Reusing existing TXT: %s  —  %u bytes\n", dest.c_str(), (unsigned)sz);
        outTxtPath = dest;
        return true;
      }
    }
  }
  // Create directories if needed
  int lastSlash = dest.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = dest.substring(0, lastSlash);
    createDirRecursive(dir);
  }

  // Open input and output files
  SimpleXmlParser parser;
  unsigned long totalStartMs = millis();
  unsigned long t0 = millis();
  if (!parser.open(srcPath.c_str()))
    return false;
  unsigned long parserOpenMs = millis() - t0;
  if (timings)
    timings->parserOpen = parserOpenMs;

  t0 = millis();
  File out = SD.open(dest.c_str(), FILE_WRITE);
  unsigned long outOpenMs = millis() - t0;
  if (!out) {
    parser.close();
    return false;
  }
  Serial.printf("  Output file open took  %lu ms\n", outOpenMs);
  if (timings)
    timings->outOpen = outOpenMs;

  // Perform the conversion using common logic
  t0 = millis();
  size_t bytesWritten = 0;
  performXhtmlToTxtConversion(parser, out, &bytesWritten);
  unsigned long conversionMs = millis() - t0;
  if (timings)
    timings->conversion = conversionMs;

  // Cleanup and close (timed)
  t0 = millis();
  parser.close();
  unsigned long parserCloseMs = millis() - t0;
  if (timings)
    timings->parserClose = parserCloseMs;
  t0 = millis();
  out.close();
  unsigned long closeOutMs = millis() - t0;
  if (timings)
    timings->closeOut = closeOutMs;
  unsigned long totalMs = millis() - totalStartMs;
  if (timings) {
    timings->total = totalMs;
    timings->bytes = (unsigned int)bytesWritten;
  }
  Serial.printf(
      "Converted XHTML to TXT: %s  —  total = %lu ms  ( parserOpen = %lu, outOpen = %lu, conversion = %lu, parserClose "
      "= %lu, "
      "closeOut = %lu )\n",
      dest.c_str(), totalMs, parserOpenMs, outOpenMs, conversionMs, parserCloseMs, closeOutMs);
  outTxtPath = dest;
  return true;
}

void EpubWordProvider::writeParagraphStyleToken(String& writeBuffer, String& pendingTag,
                                                const String& pendingParagraphClasses, const String& pendingInlineStyle,
                                                bool& paragraphClassesWritten,
                                                std::vector<char>& paragraphStyleEmitted) {
  // If this is the beginning of a paragraph and styles haven't been written yet,
  // write the style token in front of the text line.

  if (!paragraphClassesWritten) {
    // Emit style properties for the paragraph using ESC + command byte format
    // Alignment: ESC+'L'(left), ESC+'R'(right), ESC+'C'(center), ESC+'J'(justify)
    // Style: ESC+'B'(bold), ESC+'I'(italic), ESC+'X'(bold+italic)
    const CssParser* css = epubReader_ ? epubReader_->getCssParser() : nullptr;

    // Fetch CSS in order of precedence (tag < class < inline)
    CssStyle combined;

    // tag styles
    if (css) {
      CssStyle tagStyle = css->getTagStyle(pendingTag);
      combined.merge(tagStyle);
    }

    // class styles
    if (css && !pendingParagraphClasses.isEmpty()) {
      CssStyle classStyle = css->getCombinedStyle(pendingTag, pendingParagraphClasses);
      combined.merge(classStyle);
    }

    // inline styles
    if (css && !pendingInlineStyle.isEmpty()) {
      CssStyle inlineStyle = css->parseInlineStyle(pendingInlineStyle);
      combined.merge(inlineStyle);
    }

    if (combined.hasMarginTop) {
      for (int i = 0; i < combined.marginTop; ++i) {
        writeBuffer += "\n";
      }
    }

    if (combined.hasMarginBottom) {
      for (int i = 0; i < combined.marginBottom; ++i) {
        paragraphStyleEmitted.push_back('\n');
      }
    }

    // Only emit alignment tokens for paragraphs - NOT bold/italic
    // Bold/italic come from inline elements like <b>, <i>, <span>
    String styleToken = "";
    if (combined.hasTextAlign) {
      char tok = 'L';
      switch (combined.textAlign) {
        case TextAlign::Left:
          tok = 'L';
          break;
        case TextAlign::Right:
          tok = 'R';
          break;
        case TextAlign::Center:
          tok = 'C';
          break;
        case TextAlign::Justify:
          tok = 'J';
          break;
        default:
          tok = 'L';
          break;
      }
      styleToken += (char)0x1B;  // ESC
      styleToken += tok;
      paragraphStyleEmitted.push_back(tok);
    }

    if (styleToken.length() > 0) {
      writeBuffer += styleToken;
    }

    // Automatically make headers bold to reflect typical user agent stylesheets
    if (isHeaderElement(pendingTag)) {
      writeBuffer += (char)0x1B;  // ESC
      writeBuffer += 'B';
      paragraphStyleEmitted.push_back('B');
    }

    paragraphClassesWritten = true;

    // If the combined paragraph style specifies a positive text-indent value,
    // convert it to a number of spaces using a simple heuristic: spaces = round(px / 4),
    // clamped to [0, 12]. This maps typical indents to visible space counts while
    // avoiding huge or tiny counts.
    if (combined.hasTextIndent && combined.textIndent > 0.0f) {
      int spaces = (int)std::round(combined.textIndent / 4.0f);
      if (spaces < 0)
        spaces = 0;
      if (spaces > 12)
        spaces = 12;

      writeBuffer += (char)0x1B;  // ESC
      writeBuffer += 'H';
      for (int i = 0; i < spaces; ++i)
        writeBuffer += '-';
      writeBuffer += (char)0x1B;  // ESC
      writeBuffer += 'h';
    }

    // Paragraph-level CSS may also include font-weight/font-style which we
    // treat as the base inline styling for this paragraph. Record the base
    // inline style so later inline elements can override it.
    baseInlineStyle_.hasBold = combined.hasFontWeight;
    baseInlineStyle_.bold = (combined.hasFontWeight && combined.fontWeight == CssFontWeight::Bold);
    baseInlineStyle_.hasItalic = combined.hasFontStyle;
    baseInlineStyle_.italic = (combined.hasFontStyle && combined.fontStyle == CssFontStyle::Italic);
    updateEffectiveInlineCombined();
  }
}

void EpubWordProvider::performXhtmlToTxtConversion(SimpleXmlParser& parser, File& out, size_t* outBytes) {
  const size_t FLUSH_THRESHOLD = 2048;
  if (outBytes)
    *outBytes = 0;

  String buffer;                     // Output buffer
  std::vector<String> elementStack;  // Track nested elements
  std::vector<bool> linkStack;       // Track if each element is a link with href
  // Track inline style element stack (store per-element flags in object state)
  std::vector<char> paragraphStyleEmitted;  // Track paragraph style tokens emitted (uppercase)
  String pendingParagraphClasses;           // CSS classes for current block
  String pendingInlineStyle;                // Inline style attribute for current block
  String pendingTag;                        // Tag for current block
  bool paragraphClassesWritten = false;     // Have we written style token?
  bool lineHasContent = false;              // Does current line have visible content?
  bool lineHasNbsp = false;                 // Does current line have &nbsp;?
  bool pendingLinkCloseSpace = false;       // Do we need to add space before next text?

  while (parser.read()) {
    SimpleXmlParser::NodeType nodeType = parser.getNodeType();

    // ========== START ELEMENT ==========
    if (nodeType == SimpleXmlParser::Element) {
      String name = parser.getName();

      // Track non-self-closing elements
      bool isLinkWithHref = false;
      if (!parser.isEmptyElement()) {
        elementStack.push_back(name);
        // Check if this is a link with href attribute
        if (name == "a") {
          String href = parser.getAttribute("href");
          if (!href.isEmpty()) {
            isLinkWithHref = true;
            // Add space before link only if buffer doesn't already end with whitespace
            if (buffer.length() > 0) {
              char lastChar = buffer.charAt(buffer.length() - 1);
              if (lastChar != ' ' && lastChar != '\n' && lastChar != '\t') {
                buffer += ' ';
              }
            }
            buffer += (char)0x1B;  // ESC
            buffer += 'O';         // Link open (italic style)
            buffer += '(';
          }
        }
        linkStack.push_back(isLinkWithHref);
      }

      // Block elements: add newline before if current line has content
      // This ensures blockquotes, nested divs, etc. start on a new line
      if (isBlockElement(name) && lineHasContent) {
        buffer += "\n";
        lineHasContent = false;
        lineHasNbsp = false;
        pendingLinkCloseSpace = false;  // Clear pending space at block boundaries
      }

      // Capture CSS classes and inline styles for block elements
      if (isBlockElement(name)) {
        pendingParagraphClasses = parser.getAttribute("class");
        pendingInlineStyle = parser.getAttribute("style");
        pendingTag = name;
        paragraphClassesWritten = false;
      }

      // Handle inline style elements (b, strong, i, em, span)
      if (isInlineStyleElement(name) && !parser.isEmptyElement()) {
        String classAttr = parser.getAttribute("class");
        String styleAttr = parser.getAttribute("style");
        // writeInlineStyleToken will push state into inlineStyleStack_ and
        // emit a combined token if necessary (supports bold+italic stacking)
        (void)writeInlineStyleToken(buffer, name, classAttr, styleAttr);
      }

      // Handle <br/> - only add newline if line has content
      if (parser.isEmptyElement() && (name == "br" || name == "hr")) {
        if (lineHasContent) {
          // Close alignment token before newline if one was opened
          if (paragraphClassesWritten && !paragraphStyleEmitted.empty()) {
            for (auto it = paragraphStyleEmitted.rbegin(); it != paragraphStyleEmitted.rend(); ++it) {
              char startCmd = *it;
              char endCmd = startCmd;
              if (startCmd >= 'A' && startCmd <= 'Z') {
                endCmd = (char)tolower(startCmd);
                buffer += (char)0x1B;
              }
              buffer += endCmd;
            }
            paragraphStyleEmitted.clear();
          }
          // Close any open inline styles before newline
          if (writtenInlineCombined_ != '\0') {
            writeStyleResetToken(buffer, writtenInlineCombined_);
            writtenInlineCombined_ = '\0';
          }
          buffer += "\n";
          lineHasContent = false;
          lineHasNbsp = false;
          // Keep paragraphClassesWritten as false so alignment reopens on next line
          paragraphClassesWritten = false;
        }
      }
    }

    // ========== END ELEMENT ==========
    else if (nodeType == SimpleXmlParser::EndElement) {
      String name = parser.getName();

      // Handle end of inline style elements
      if (isInlineStyleElement(name) && !inlineStyleStack_.empty()) {
        closeInlineStyleElement(buffer);
      }

      // Handle end of link elements - add closing bracket and reset style
      if (name == "a" && !linkStack.empty() && linkStack.back()) {
        buffer += ')';
        buffer += (char)0x1B;  // ESC
        buffer += 'o';         // Link close (reset style)
        // Set flag to potentially add space before next text content
        pendingLinkCloseSpace = true;
      }

      // Block elements: add newline if line had content OR had &nbsp;
      if (isBlockElement(name) || isHeaderElement(name)) {
        if (lineHasContent || lineHasNbsp) {
          // If a paragraph-level style was emitted at the start, write corresponding end tokens now
          if (paragraphClassesWritten && !paragraphStyleEmitted.empty()) {
            for (auto it = paragraphStyleEmitted.rbegin(); it != paragraphStyleEmitted.rend(); ++it) {
              char startCmd = *it;
              char endCmd = startCmd;
              if (startCmd >= 'A' && startCmd <= 'Z') {
                endCmd = (char)tolower(startCmd);
                buffer += (char)0x1B;
              }
              buffer += endCmd;
            }
            paragraphStyleEmitted.clear();
          }

          buffer += "\n";
        }
        // Close any open inline styles at paragraph end to prevent carry-over between paragraphs
        // Use the *written* inline combination so we close whatever was actually emitted
        // into the output buffer instead of the abstract current state.
        if (writtenInlineCombined_ != '\0') {
          writeStyleResetToken(buffer, writtenInlineCombined_);
          writtenInlineCombined_ = '\0';
        }

        lineHasContent = false;
        lineHasNbsp = false;
        pendingParagraphClasses = "";
        pendingInlineStyle = "";
        pendingTag = "";
        paragraphClassesWritten = false;
        paragraphStyleEmitted.clear();
        pendingLinkCloseSpace = false;  // Clear pending space at paragraph end
      }

      // Pop from element stack and link stack
      if (!elementStack.empty()) {
        elementStack.pop_back();
      }
      if (!linkStack.empty()) {
        linkStack.pop_back();
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

      // Check if we need to add a space after a link close
      if (pendingLinkCloseSpace) {
        // Only add space if text doesn't start with whitespace
        bool startsWithWhitespace = false;
        if (text.length() > 0) {
          char firstChar = text.charAt(0);
          startsWithWhitespace = (firstChar == ' ' || firstChar == '\n' || firstChar == '\t' || firstChar == '\r');
        }
        if (!startsWithWhitespace) {
          buffer += ' ';
        }
        pendingLinkCloseSpace = false;
      }

      if (text.indexOf("\xC2\xA0") >= 0) {
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

      // Write style token at start of paragraph and remember the emitted raw tokens
      writeParagraphStyleToken(buffer, pendingTag, pendingParagraphClasses, pendingInlineStyle, paragraphClassesWritten,
                               paragraphStyleEmitted);

      // Ensure inline style tokens (open/close) are emitted right before we write visible text
      ensureInlineStyleEmitted(buffer);

      // Append text
      buffer += text;
      lineHasContent = true;
    }

    // Periodic flush to avoid excessive memory use and ensure data hits SD
    if (buffer.length() > FLUSH_THRESHOLD) {
      size_t toWrite = buffer.length();
      size_t written = out.write((const uint8_t*)buffer.c_str(), toWrite);
      if (outBytes)
        *outBytes += written;
      if (written != toWrite) {
        Serial.printf("WARNING: partial write during conversion: attempted=%u wrote=%u\n", (unsigned)toWrite,
                      (unsigned)written);
      }
      buffer = "";
    }
  }

  // Close any remaining open styles before final flush
  // Close paragraph styles if they were written but not closed
  if (paragraphClassesWritten && !paragraphStyleEmitted.empty()) {
    for (auto it = paragraphStyleEmitted.rbegin(); it != paragraphStyleEmitted.rend(); ++it) {
      char startCmd = *it;
      char endCmd = startCmd;
      if (startCmd >= 'A' && startCmd <= 'Z') {
        endCmd = (char)tolower(startCmd);
        buffer += (char)0x1B;
      }
      buffer += endCmd;
    }
    paragraphStyleEmitted.clear();
  }

  // Close any remaining inline styles (close what was actually emitted)
  if (writtenInlineCombined_ != '\0') {
    writeStyleResetToken(buffer, writtenInlineCombined_);
    writtenInlineCombined_ = '\0';
  }
  // Reset base and stack state
  baseInlineStyle_ = InlineStyleState();
  currentInlineCombined_ = '\0';
  inlineStyleStack_.clear();

  // Periodic flush and final flush using write() to verify bytes written
  if (outBytes)
    *outBytes = 0;

  if (buffer.length() > 0) {
    size_t toWrite = buffer.length();
    size_t written = out.write((const uint8_t*)buffer.c_str(), toWrite);
    if (outBytes)
      *outBytes += written;
    if (written != toWrite) {
      Serial.printf("WARNING: partial write: attempted=%u wrote=%u\n", (unsigned)toWrite, (unsigned)written);
    }
    buffer = "";
  }
}

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
    return "\xC2\xA0";  // Non-breaking space
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
    if (c == '\xC2' && i + 1 < text.length() && text.charAt(i + 1) == '\xA0') {
      c = ' ';
      i++;  // skip the second byte (0xA0)
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

char EpubWordProvider::writeInlineStyleToken(String& writeBuffer, const String& elementName, const String& classAttr,
                                             const String& styleAttr) {
  // Determine style flags for this element (from tag name, classes, inline styles)
  InlineStyleState state;
  // Tag name - these are explicit declarations
  if (elementName == "b" || elementName == "strong") {
    state.bold = true;
    state.hasBold = true;
  } else if (elementName == "i" || elementName == "em") {
    state.italic = true;
    state.hasItalic = true;
  }

  // Check CSS classes and inline styles for additional styling
  const CssParser* css = epubReader_ ? epubReader_->getCssParser() : nullptr;
  if (css) {
    CssStyle combined;
    if (!classAttr.isEmpty()) {
      combined = css->getCombinedStyle(elementName, classAttr);
    }
    if (!styleAttr.isEmpty()) {
      CssStyle inlineStyle = css->parseInlineStyle(styleAttr);
      combined.merge(inlineStyle);
    }
    if (combined.hasFontWeight) {
      state.hasBold = true;
      state.bold = (combined.fontWeight == CssFontWeight::Bold);
    }
    if (combined.hasFontStyle) {
      state.hasItalic = true;
      state.italic = (combined.fontStyle == CssFontStyle::Italic);
    }
  }

  // Push this element's style onto the stack
  inlineStyleStack_.push_back(state);

  // Recompute the effective combined style including the paragraph base style
  updateEffectiveInlineCombined();

  return currentInlineCombined_;
}

void EpubWordProvider::closeInlineStyleElement(String& writeBuffer) {
  if (inlineStyleStack_.empty())
    return;

  // Pop the last element and recompute the effective combined style which
  // takes paragraph base and any explicit overrides in the stack into account.
  inlineStyleStack_.pop_back();

  updateEffectiveInlineCombined();
}

void EpubWordProvider::writeStyleResetToken(String& writeBuffer, char startCmd) {
  // Emit a token to reset back to normal style
  // Map startCmd (uppercase) to corresponding lowercase end token
  if (startCmd == '\0')
    return;
  char endCmd = startCmd;
  if (endCmd >= 'A' && endCmd <= 'Z') {
    endCmd = (char)tolower(endCmd);
  }
  writeBuffer += (char)0x1B;  // ESC
  writeBuffer += endCmd;      // Reset token corresponding to startCmd
}

void EpubWordProvider::ensureInlineStyleEmitted(String& writeBuffer) {
  // If the written style already matches current, nothing to do
  if (writtenInlineCombined_ == currentInlineCombined_)
    return;

  // Close whatever was previously emitted
  if (writtenInlineCombined_ != '\0') {
    writeStyleResetToken(writeBuffer, writtenInlineCombined_);
  }

  // Open new combined style if any
  if (currentInlineCombined_ != '\0') {
    writeBuffer += (char)0x1B;
    writeBuffer += currentInlineCombined_;
  }

  // Update the written-tracking state
  writtenInlineCombined_ = currentInlineCombined_;
}

void EpubWordProvider::updateEffectiveInlineCombined() {
  // Start with base style if specified; otherwise defaults to not-set (false)
  bool effectiveBold = false;
  bool effectiveItalic = false;
  if (baseInlineStyle_.hasBold) {
    effectiveBold = baseInlineStyle_.bold;
  }
  if (baseInlineStyle_.hasItalic) {
    effectiveItalic = baseInlineStyle_.italic;
  }

  // Apply stack entries in order: any entry that explicitly specifies a property
  // overrides the current effective value for that property.
  for (const auto& s : inlineStyleStack_) {
    if (s.hasBold) {
      effectiveBold = s.bold;
    }
    if (s.hasItalic) {
      effectiveItalic = s.italic;
    }
  }

  char newCombined = '\0';
  if (effectiveBold && effectiveItalic)
    newCombined = 'X';
  else if (effectiveBold)
    newCombined = 'B';
  else if (effectiveItalic)
    newCombined = 'I';

  currentInlineCombined_ = newCombined;
}

// Context for true streaming: EPUB -> Parser -> TXT
struct TrueStreamingContext {
  epub_stream_context* epubStream;
  size_t bytesPulled = 0;
};

// Callback for SimpleXmlParser to pull data from EPUB stream
static int parser_stream_callback(char* buffer, size_t maxSize, void* userData) {
  TrueStreamingContext* ctx = (TrueStreamingContext*)userData;
  if (!ctx || !ctx->epubStream) {
    return -1;
  }

  // Pull next chunk from EPUB decompressor
  int bytesRead = epub_read_chunk(ctx->epubStream, buffer, maxSize);
  if (bytesRead > 0) {
    ctx->bytesPulled += (size_t)bytesRead;
  }
  return bytesRead;
}

bool EpubWordProvider::convertXhtmlStreamToTxt(const char* epubFilename, String& outTxtPath,
                                               ConversionTimings* timings) {
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
  unsigned long totalStartMs = millis();
  unsigned long t0 = millis();
  // If the TXT file already exists and is non-empty, reuse it and skip conversion
  if (SD.exists(dest.c_str())) {
    File chk = SD.open(dest.c_str());
    if (chk) {
      size_t sz = chk.size();
      chk.close();
      if (sz > 0) {
        if (timings) {
          timings->total = 0;
          timings->startStream = 0;
          timings->parserOpen = 0;
          timings->outOpen = 0;
          timings->conversion = 0;
          timings->parserClose = 0;
          timings->endStream = 0;
          timings->closeOut = 0;
          timings->bytes = sz;
        }
        Serial.printf("  Reusing existing streamed TXT: %s  —  %u bytes\n", dest.c_str(), (unsigned)sz);
        outTxtPath = dest;
        return true;
      }
    }
  }

  epub_stream_context* epubStream = epubReader_->startStreaming(epubFilename, 8192);
  unsigned long startStreamingMs = millis() - t0;
  if (timings)
    timings->startStream = startStreamingMs;
  if (!epubStream) {
    Serial.printf("ERROR: Failed to start EPUB streaming for file: %s\n", epubFilename);
    return false;
  }

  // Set up context for parser callback
  TrueStreamingContext streamCtx;
  streamCtx.epubStream = epubStream;

  // Open parser in streaming mode
  SimpleXmlParser parser;
  t0 = millis();
  // Memory debug: before opening parser from stream
  uint32_t heapBeforeParserOpen = ESP.getFreeHeap();
  Serial.printf("  [MEM] before parser.openFromStream: Free=%u, Total=%u, MinFree=%u\n", heapBeforeParserOpen,
                ESP.getHeapSize(), ESP.getMinFreeHeap());

  if (!parser.openFromStream(parser_stream_callback, &streamCtx)) {
    // Log memory on failure
    uint32_t heapFail = ESP.getFreeHeap();
    int32_t failDelta = (int32_t)heapFail - (int32_t)heapBeforeParserOpen;
    Serial.printf("  [MEM] parser.openFromStream FAILED: Free=%u (delta: %d)\n", heapFail, failDelta);
    epub_end_streaming(epubStream);
    Serial.println("ERROR: Failed to open parser in streaming mode");
    return false;
  }
  // Memory debug: after successful parser open
  uint32_t heapAfterParserOpen = ESP.getFreeHeap();
  int32_t parserOpenDelta = (int32_t)heapAfterParserOpen - (int32_t)heapBeforeParserOpen;
  Serial.printf("  [MEM] after parser.openFromStream: Free=%u (delta: %d)\n", heapAfterParserOpen, parserOpenDelta);
  unsigned long parserOpenMs = millis() - t0;
  if (timings)
    timings->parserOpen = parserOpenMs;

  // Remove existing file to ensure clean write (timed)
  t0 = millis();
  if (SD.exists(dest.c_str())) {
    SD.remove(dest.c_str());
  }
  File out = SD.open(dest.c_str(), FILE_WRITE);
  unsigned long outOpenMs = millis() - t0;
  if (!out) {
    Serial.printf("ERROR: Failed to open output TXT file '%s' for writing\n", dest.c_str());
    parser.close();
    epub_end_streaming(epubStream);
    return false;
  }
  Serial.printf("  Output file open took  %lu ms\n", outOpenMs);
  if (timings)
    timings->outOpen = outOpenMs;

  // Perform the conversion using common logic (timed)
  t0 = millis();
  size_t bytesWritten = 0;
  performXhtmlToTxtConversion(parser, out, &bytesWritten);
  unsigned long conversionMs = millis() - t0;
  if (timings)
    timings->conversion = conversionMs;

  // Close parser and streaming in separate timed steps
  t0 = millis();
  parser.close();
  unsigned long parserCloseMs = millis() - t0;
  if (timings)
    timings->parserClose = parserCloseMs;

  t0 = millis();
  epub_end_streaming(epubStream);
  unsigned long endStreamMs = millis() - t0;
  if (timings)
    timings->endStream = endStreamMs;

  t0 = millis();
  out.close();
  unsigned long closeOutMs = millis() - t0;
  if (timings)
    timings->closeOut = closeOutMs;

  // Re-open the output file to get final size (some SD implementations report size=0 until closed)
  File check = SD.open(dest.c_str());
  size_t checkSize = 0;
  if (check) {
    checkSize = check.size();
    check.close();
  }
  Serial.printf("  [STREAM] bytesPulled=%u, bytesWrittenReported=%u\n", (unsigned)streamCtx.bytesPulled,
                (unsigned)checkSize);
  bytesWritten = checkSize;

  unsigned long totalMs = millis() - totalStartMs;
  if (timings) {
    timings->total = totalMs;
    timings->bytes = (unsigned int)bytesWritten;
  }
  Serial.printf(
      "Converted XHTML to TXT (streamed): %s  —  total = %lu ms  ( startStream = %lu, parserOpen = %lu, outOpen = %lu, "
      "conversion = "
      "%lu, parserClose = %lu, endStream = %lu, closeOut = %lu )  —  %u bytes\n",
      dest.c_str(), totalMs, startStreamingMs, parserOpenMs, (timings ? timings->outOpen : 0), conversionMs,
      parserCloseMs, endStreamMs, closeOutMs, (unsigned int)bytesWritten);
  outTxtPath = dest;
  return true;
}

bool EpubWordProvider::openChapter(int chapterIndex) {
  if (!epubReader_) {
    return false;
  }

  int spineCount = epubReader_->getSpineCount();
  if (chapterIndex < 0 || chapterIndex >= spineCount) {
    Serial.printf("ERROR: Chapter index %d out of range (0 to %d)\n", chapterIndex, spineCount - 1);
    return false;
  }

  const SpineItem* spineItem = epubReader_->getSpineItem(chapterIndex);
  if (!spineItem) {
    Serial.printf("ERROR: Failed to get spine item for chapter index %d\n", chapterIndex);
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
  unsigned long convStart = millis();
  if (useStreamingConversion_) {
    // Stream XHTML from EPUB directly to memory and convert (no intermediate XHTML file)
    ConversionTimings t;
    if (!convertXhtmlStreamToTxt(fullHref.c_str(), txtPath, &t)) {
      return false;
    }
    // Print detailed breakdown for chapter-level conversion
    Serial.printf(
        "    Converted XHTML to TXT (streamed): %s  —  total = %lu ms  ( startStream = %lu, parserOpen = %lu, outOpen "
        "= %lu, conversion = %lu, parserClose = %lu, endStream = %lu, closeOut = %lu )  —  %u bytes\n",
        txtPath.c_str(), t.total, t.startStream, t.parserOpen, t.outOpen, t.conversion, t.parserClose, t.endStream,
        t.closeOut, (unsigned int)t.bytes);
  } else {
    // Extract XHTML file first, then convert from file
    String xhtmlPath = epubReader_->getFile(fullHref.c_str());
    if (xhtmlPath.isEmpty()) {
      return false;
    }
    ConversionTimings t;
    if (!convertXhtmlToTxt(xhtmlPath, txtPath, &t)) {
      return false;
    }
    // Print detailed breakdown for chapter-level conversion when using file-based conversion
    Serial.printf(
        "    Converted XHTML to TXT: %s  —  total = %lu ms  ( parserOpen = %lu, outOpen = %lu, conversion = %lu, "
        "parserClose = %lu, closeOut = %lu )  —  %u bytes\n",
        txtPath.c_str(), t.total, t.parserOpen, t.outOpen, t.conversion, t.parserClose, t.closeOut,
        (unsigned int)t.bytes);
  }
  unsigned long conversionAndExtractMs = millis() - convStart;
  Serial.printf("  Chapter conversion + extract took  %lu ms\n", conversionAndExtractMs);

  String newXhtmlPath = fullHref;  // Keep for tracking

  // Delete any previous file provider and create new one for this chapter
  if (fileProvider_) {
    delete fileProvider_;
    fileProvider_ = nullptr;
  }
  unsigned long fileProvStart = millis();
  fileProvider_ = new FileWordProvider(txtPath.c_str(), bufSize_);
  unsigned long fileProvMs = millis() - fileProvStart;
  Serial.printf("    FileWordProvider init took  %lu ms\n", fileProvMs);
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

  Serial.printf("Opened chapter %d: %s\n", chapterIndex, currentChapterName_.c_str());

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
  // For EPUBs, map the TXT-based chapter progress to the XHTML-based chapter range
  if (isEpub_ && epubReader_) {
    size_t totalSize = epubReader_->getTotalBookSize();
    if (totalSize == 0)
      return 1.0f;

    // Get this chapter's start and end percentage based on XHTML sizes
    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t chapterXhtmlSize = epubReader_->getSpineItemSize(currentChapter_);
    float chapterStartPct = static_cast<float>(chapterOffset) / static_cast<float>(totalSize);
    float chapterEndPct = static_cast<float>(chapterOffset + chapterXhtmlSize) / static_cast<float>(totalSize);

    // Get progress within current chapter from TXT file (0.0 to 1.0)
    float chapterProgress = fileProvider_->getPercentage();

    // Map TXT progress to XHTML-based range
    return chapterStartPct + chapterProgress * (chapterEndPct - chapterStartPct);
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

    // Get this chapter's start and end percentage based on XHTML sizes
    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t chapterXhtmlSize = epubReader_->getSpineItemSize(currentChapter_);
    float chapterStartPct = static_cast<float>(chapterOffset) / static_cast<float>(totalSize);
    float chapterEndPct = static_cast<float>(chapterOffset + chapterXhtmlSize) / static_cast<float>(totalSize);

    // Get progress within current chapter from TXT file for the given index (0.0 to 1.0)
    float chapterProgress = fileProvider_->getPercentage(index);

    // Map TXT progress to XHTML-based range
    return chapterStartPct + chapterProgress * (chapterEndPct - chapterStartPct);
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

Language EpubWordProvider::getLanguage() const {
  if (!isEpub_ || !epubReader_) {
    return Language::BASIC;  // Default for non-EPUB files
  }
  String langStr = epubReader_->getLanguage();
  return stringToLanguage(langStr);
}
