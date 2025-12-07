#include "EpubWordProvider.h"

#include <Arduino.h>

// Helper to check if two strings are equal (case-insensitive)
static bool equalsIgnoreCase(const String& str, const char* target) {
  if (str.length() != strlen(target))
    return false;
  for (size_t i = 0; i < str.length(); i++) {
    char c1 = str.charAt(i);
    char c2 = target[i];
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return false;
  }
  return true;
}

// Helper to check if element name is a block-level element (case-insensitive)
static bool isBlockElement(const String& name) {
  if (name.length() == 0)
    return false;

  const char* blockElements[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "title", "li", "br"};
  for (size_t i = 0; i < sizeof(blockElements) / sizeof(blockElements[0]); i++) {
    const char* blockElem = blockElements[i];
    size_t blockLen = strlen(blockElem);
    if (name.length() != blockLen)
      continue;

    bool match = true;
    for (size_t j = 0; j < blockLen; j++) {
      char c1 = name.charAt(j);
      char c2 = blockElem[j];
      if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
      if (c1 != c2) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
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

    // Open the XHTML file with SimpleXmlParser for buffered reading
    parser_ = new SimpleXmlParser();
    if (!parser_->open(xhtmlPath_.c_str())) {
      delete parser_;
      parser_ = nullptr;
      return;
    }

    // Get file size for percentage calculation
    fileSize_ = parser_->getFileSize();

    // Position parser at first node for reading
    parser_->read();
    prevFilePos_ = parser_->getFilePosition();

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
    if (!openChapter(43)) {
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

  // Get the XHTML file (will extract if needed)
  String newXhtmlPath = epubReader_->getFile(fullHref.c_str());
  if (newXhtmlPath.isEmpty()) {
    return false;
  }

  // Close existing parser if any
  if (parser_) {
    parser_->close();
    delete parser_;
    parser_ = nullptr;
  }

  // Open the new XHTML file
  parser_ = new SimpleXmlParser();
  if (!parser_->open(newXhtmlPath.c_str())) {
    delete parser_;
    parser_ = nullptr;
    return false;
  }

  xhtmlPath_ = newXhtmlPath;
  currentChapter_ = chapterIndex;
  fileSize_ = parser_->getFileSize();

  // Cache the chapter name from TOC
  currentChapterName_ = epubReader_->getChapterNameForSpine(chapterIndex);

  // Position parser at first node for reading
  parser_->read();
  prevFilePos_ = parser_->getFilePosition();

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
  if (!parser_) {
    return false;
  }
  // Check if we have more content to read
  return parser_->getFilePosition() < fileSize_;
}

bool EpubWordProvider::hasPrevWord() {
  if (!parser_) {
    return false;
  }
  return parser_->getFilePosition() > 0;
}

String EpubWordProvider::getNextWord() {
  if (!parser_) {
    return String("");
  }

  // Save position for ungetWord at start
  prevFilePos_ = parser_->getFilePosition();

  // Skip to next text content
  while (true) {
    SimpleXmlParser::NodeType nodeType = parser_->getNodeType();

    // If we don't have a current node (e.g., after seekToFilePosition), read one
    if (nodeType == SimpleXmlParser::None || nodeType == SimpleXmlParser::EndOfFile) {
      if (!parser_->read()) {
        return String("");  // End of document
      }
      continue;  // Loop to check the newly read node
    }

    if (nodeType == SimpleXmlParser::Text) {
      // We're in a text node, try to read a character
      if (parser_->hasMoreTextChars()) {
        char c = parser_->readTextNodeCharForward();

        // Handle spaces
        if (c == ' ') {
          String token;
          token += c;
          // Collect consecutive spaces (but don't cross into next node)
          while (parser_->hasMoreTextChars()) {
            char next = parser_->peekTextNodeChar();
            if (next != ' ')
              break;
            token += parser_->readTextNodeCharForward();
          }
          return token;
        }
        // Skip carriage return
        else if (c == '\r') {
          continue;  // Loop again
        }
        // Handle newline and tab
        else if (c == '\n' || c == '\t') {
          return String(c);
        }
        // Handle regular word characters
        else {
          String token;
          token += c;
          // Collect word characters, crossing inline element boundaries
          while (true) {
            // First, consume all word chars in current text node
            while (parser_->hasMoreTextChars()) {
              char next = parser_->peekTextNodeChar();
              if (next == '\0' || next == ' ' || next == '\n' || next == '\t' || next == '\r')
                break;
              token += parser_->readTextNodeCharForward();
            }

            // If we still have chars in this text node, we hit a delimiter - done
            if (parser_->hasMoreTextChars()) {
              break;
            }

            // Text node exhausted - check if next node continues the word
            if (!parser_->read()) {
              break;  // End of document
            }

            SimpleXmlParser::NodeType nextType = parser_->getNodeType();

            // If next is text, check if it starts with word char
            if (nextType == SimpleXmlParser::Text) {
              if (parser_->hasMoreTextChars()) {
                char peek = parser_->peekTextNodeChar();
                if (peek != '\0' && peek != ' ' && peek != '\n' && peek != '\t' && peek != '\r') {
                  // Continue building word from this text node
                  continue;
                }
              }
              // Text starts with delimiter or is empty - done with word
              break;
            }
            // Skip inline elements (like <span>) and continue looking for text
            else if (nextType == SimpleXmlParser::Element) {
              if (!isBlockElement(parser_->getName())) {
                // Inline element - skip it and continue
                continue;
              }
              // Block element - done with word
              break;
            }
            // Skip inline end elements (like </span>) and continue
            else if (nextType == SimpleXmlParser::EndElement) {
              if (!isBlockElement(parser_->getName())) {
                // Inline end element - skip it and continue
                continue;
              }
              // Block end element - done with word
              break;
            } else {
              // Other node type - done with word
              break;
            }
          }
          return token;
        }
      } else {
        // No more chars in current text node, move to next node
        if (!parser_->read()) {
          return String("");  // End of document
        }
        // Continue loop with new node
      }
    } else if (nodeType == SimpleXmlParser::EndElement) {
      // Check if this is a block element end tag
      String elementName = parser_->getName();
      if (isBlockElement(elementName)) {
        // Move past this end element first, then return newline
        // This way getCurrentIndex() before getNextWord() returns the element start
        parser_->read();
        return String('\n');
      }
      // Inline end tag (like </span>) - just move to next node
      if (!parser_->read()) {
        return String("");  // End of document
      }
    } else if (nodeType == SimpleXmlParser::Element) {
      String elementName = parser_->getName();

      if (isBlockElement(elementName)) {
        // Check if this is a self-closing element (like <br/>)
        if (parser_->isEmptyElement()) {
          // Move past this element first, then return newline
          parser_->read();
          return String('\n');
        }
      }
      // Move past this element (opening tags don't produce tokens)
      if (!parser_->read()) {
        return String("");  // End of document
      }
    } else {
      // Other node types (comments, processing instructions, etc.)
      if (!parser_->read()) {
        return String("");  // End of document
      }
    }
  }
}

String EpubWordProvider::getPrevWord() {
  if (!parser_) {
    return String("");
  }

  // Save position for ungetWord at start
  prevFilePos_ = parser_->getFilePosition();

  // Navigate backward through nodes
  while (true) {
    SimpleXmlParser::NodeType nodeType = parser_->getNodeType();

    // If we don't have a current node (e.g., after seekToFilePosition), read backward
    if (nodeType == SimpleXmlParser::None || nodeType == SimpleXmlParser::EndOfFile) {
      if (!parser_->readBackward()) {
        return String("");  // Beginning of document
      }
      continue;  // Loop to check the newly read node
    }

    if (nodeType == SimpleXmlParser::Text) {
      // We're in a text node, try to read a character backward
      if (parser_->hasMoreTextCharsBackward()) {
        char c = parser_->readPrevTextNodeChar();

        // Handle spaces
        if (c == ' ') {
          String token;
          token += c;
          // Collect consecutive spaces backward (within this text node only)
          while (parser_->hasMoreTextCharsBackward()) {
            char prev = parser_->peekPrevTextNodeChar();
            if (prev != ' ')
              break;
            token += parser_->readPrevTextNodeChar();
          }
          return token;
        }
        // Skip carriage return
        else if (c == '\r') {
          continue;
        }
        // Handle newline and tab
        else if (c == '\n' || c == '\t') {
          return String(c);
        }
        // Handle regular word characters - collect backward then reverse
        else {
          String rev;
          rev += c;
          // Collect word characters backward, crossing inline element boundaries
          while (true) {
            // First, consume all word chars in current text node (backward)
            while (parser_->hasMoreTextCharsBackward()) {
              char prev = parser_->peekPrevTextNodeChar();
              if (prev == '\0' || prev == ' ' || prev == '\n' || prev == '\t' || prev == '\r')
                break;
              rev += parser_->readPrevTextNodeChar();
            }

            // If we still have chars backward in this text node, we hit a delimiter - done
            if (parser_->hasMoreTextCharsBackward()) {
              break;
            }

            // Text node exhausted backward - check if previous node continues the word
            if (!parser_->readBackward()) {
              break;  // Beginning of document
            }

            SimpleXmlParser::NodeType prevType = parser_->getNodeType();

            // If previous is text, check if it ends with word char
            if (prevType == SimpleXmlParser::Text) {
              if (parser_->hasMoreTextCharsBackward()) {
                char peek = parser_->peekPrevTextNodeChar();
                if (peek != '\0' && peek != ' ' && peek != '\n' && peek != '\t' && peek != '\r') {
                  // Continue building word from this text node
                  continue;
                }
              }
              // Text ends with delimiter or is empty - done with word
              break;
            }
            // Skip inline elements (like <span>) and continue looking for text
            else if (prevType == SimpleXmlParser::Element) {
              if (!isBlockElement(parser_->getName())) {
                // Inline element - skip it and continue
                continue;
              }
              // Block element - done with word
              break;
            }
            // Skip inline end elements (like </span>) and continue
            else if (prevType == SimpleXmlParser::EndElement) {
              if (!isBlockElement(parser_->getName())) {
                // Inline end element - skip it and continue
                continue;
              }
              // Block end element - done with word
              break;
            } else {
              // Other node type - done with word
              break;
            }
          }
          // Reverse to get correct order
          String token;
          for (int i = rev.length() - 1; i >= 0; --i) {
            token += rev.charAt(i);
          }
          return token;
        }
      }
      // No more chars in current text node, move to previous node
      if (!parser_->readBackward()) {
        return String("");  // Beginning of document
      }
    } else if (nodeType == SimpleXmlParser::EndElement) {
      // Block element end tag - return newline
      String elementName = parser_->getName();
      if (isBlockElement(elementName)) {
        // Save position of this element for ungetWord before advancing
        prevFilePos_ = parser_->getElementEndPos();
        // Move to previous node, then return newline
        parser_->readBackward();
        return String('\n');
      }
      // Inline end element - just move to previous node
      if (!parser_->readBackward()) {
        return String("");  // Beginning of document
      }
    } else if (nodeType == SimpleXmlParser::Element) {
      String elementName = parser_->getName();

      if (isBlockElement(elementName)) {
        // Check if this is a self-closing element (like <br/>)
        if (parser_->isEmptyElement()) {
          // Save position of this element for ungetWord before advancing
          prevFilePos_ = parser_->getElementEndPos();
          // Move past this element backward, then return newline
          parser_->readBackward();
          return String('\n');
        }
      }
      // Move to previous node (opening tags don't produce tokens when going backward)
      if (!parser_->readBackward()) {
        return String("");  // Beginning of document
      }
    } else {
      // Other node types (comments, processing instructions, etc.)
      if (!parser_->readBackward()) {
        return String("");  // Beginning of document
      }
    }
  }
}

float EpubWordProvider::getPercentage() {
  if (!parser_)
    return 1.0f;

  // For EPUBs, calculate book-wide percentage
  if (isEpub_ && epubReader_) {
    size_t totalSize = epubReader_->getTotalBookSize();
    if (totalSize == 0)
      return 1.0f;

    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t positionInChapter = parser_->getFilePosition();
    size_t absolutePosition = chapterOffset + positionInChapter;

    return static_cast<float>(absolutePosition) / static_cast<float>(totalSize);
  }

  // For single XHTML files, use file-based percentage
  if (fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(parser_->getFilePosition()) / static_cast<float>(fileSize_);
}

float EpubWordProvider::getPercentage(int index) {
  // For EPUBs, calculate book-wide percentage for a given position in current chapter
  if (isEpub_ && epubReader_) {
    size_t totalSize = epubReader_->getTotalBookSize();
    if (totalSize == 0)
      return 1.0f;

    size_t chapterOffset = epubReader_->getSpineItemOffset(currentChapter_);
    size_t absolutePosition = chapterOffset + index;

    return static_cast<float>(absolutePosition) / static_cast<float>(totalSize);
  }

  // For single XHTML files
  if (fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(index) / static_cast<float>(fileSize_);
}

float EpubWordProvider::getChapterPercentage() {
  if (!parser_ || fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(parser_->getFilePosition()) / static_cast<float>(fileSize_);
}

float EpubWordProvider::getChapterPercentage(int index) {
  if (fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(index) / static_cast<float>(fileSize_);
}

int EpubWordProvider::getCurrentIndex() {
  if (!parser_) {
    return 0;
  }
  return parser_->getFilePosition();
}

char EpubWordProvider::peekChar(int offset) {
  return '\0';  // Not implemented
}

int EpubWordProvider::consumeChars(int n) {
  if (!parser_ || n <= 0) {
    return 0;
  }

  int consumed = 0;

  while (consumed < n) {
    SimpleXmlParser::NodeType nodeType = parser_->getNodeType();

    // If we don't have a current node, read one
    if (nodeType == SimpleXmlParser::None || nodeType == SimpleXmlParser::EndOfFile) {
      if (!parser_->read()) {
        break;  // End of document
      }
      continue;
    }

    if (nodeType == SimpleXmlParser::Text) {
      // Consume characters from this text node
      while (consumed < n && parser_->hasMoreTextChars()) {
        char c = parser_->readTextNodeCharForward();
        // Skip carriage returns, they don't count as consumed characters
        if (c != '\r') {
          consumed++;
        }
      }

      // If text node exhausted, move to next node
      if (!parser_->hasMoreTextChars()) {
        if (!parser_->read()) {
          break;  // End of document
        }
      }
    } else if (nodeType == SimpleXmlParser::Element || nodeType == SimpleXmlParser::EndElement) {
      // Skip inline elements, stop at block elements
      String elementName = parser_->getName();
      if (isBlockElement(elementName)) {
        break;  // Stop at block elements
      }
      // Skip inline element
      if (!parser_->read()) {
        break;
      }
    } else {
      // Skip other node types
      if (!parser_->read()) {
        break;
      }
    }
  }

  return consumed;
}

bool EpubWordProvider::isInsideWord() {
  if (!parser_) {
    return false;
  }

  // For now, return false since backward scanning is not implemented
  // This would require tracking previous character which is complex
  return false;
}

void EpubWordProvider::ungetWord() {
  if (!parser_) {
    return;
  }
  parser_->seekToFilePosition(prevFilePos_);
}

void EpubWordProvider::setPosition(int index) {
  if (!parser_) {
    return;
  }

  size_t filePos;
  if (index < 0) {
    filePos = 0;
  } else if (static_cast<size_t>(index) > fileSize_) {
    filePos = fileSize_;
  } else {
    filePos = static_cast<size_t>(index);
  }

  parser_->seekToFilePosition(filePos);
  prevFilePos_ = parser_->getFilePosition();
}

void EpubWordProvider::reset() {
  // Reset parser to beginning
  if (parser_) {
    parser_->seekToFilePosition(0);
    prevFilePos_ = parser_->getFilePosition();
  }
}
