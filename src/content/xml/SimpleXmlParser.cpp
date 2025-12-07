#include "SimpleXmlParser.h"

#include <Arduino.h>

SimpleXmlParser::SimpleXmlParser()
    : bufferStartPos_(0),
      bufferLen_(0),
      filePos_(0),
      currentNodeType_(None),
      isEmptyElement_(false),
      textNodeStartPos_(0),
      textNodeEndPos_(0),
      textNodeCurrentPos_(0),
      peekedTextNodeChar_('\0'),
      hasPeekedTextNodeChar_(false),
      peekedPrevTextNodeChar_('\0'),
      hasPeekedPrevTextNodeChar_(false),
      elementStartPos_(0),
      elementEndPos_(0) {}

SimpleXmlParser::~SimpleXmlParser() {
  close();
}

bool SimpleXmlParser::open(const char* filepath) {
  close();
  file_ = SD.open(filepath, FILE_READ);
  if (!file_) {
    return false;
  }

  bufferStartPos_ = 0;
  bufferLen_ = 0;
  filePos_ = 0;
  currentNodeType_ = None;
  textNodeStartPos_ = 0;
  textNodeEndPos_ = 0;
  textNodeCurrentPos_ = 0;
  peekedTextNodeChar_ = '\0';
  hasPeekedTextNodeChar_ = false;
  peekedPrevTextNodeChar_ = '\0';
  hasPeekedPrevTextNodeChar_ = false;
  elementStartPos_ = 0;
  elementEndPos_ = 0;

  return true;
}

void SimpleXmlParser::close() {
  if (file_) {
    file_.close();
  }
  bufferStartPos_ = 0;
  bufferLen_ = 0;
  filePos_ = 0;
  currentNodeType_ = None;
  textNodeStartPos_ = 0;
  textNodeEndPos_ = 0;
  textNodeCurrentPos_ = 0;
  peekedTextNodeChar_ = '\0';
  hasPeekedTextNodeChar_ = false;
  peekedPrevTextNodeChar_ = '\0';
  hasPeekedPrevTextNodeChar_ = false;
  elementStartPos_ = 0;
  elementEndPos_ = 0;
}

// Load buffer centered around the given position
bool SimpleXmlParser::loadBufferAround(size_t pos) {
  if (!file_) {
    return false;
  }

  size_t fileSize = file_.size();
  if (fileSize == 0) {
    return false;
  }

  // Try to position buffer so pos is in the middle
  size_t idealStart = (pos >= BUFFER_SIZE / 2) ? (pos - BUFFER_SIZE / 2) : 0;

  // Adjust if we'd go past end of file
  if (idealStart + BUFFER_SIZE > fileSize) {
    idealStart = (fileSize > BUFFER_SIZE) ? (fileSize - BUFFER_SIZE) : 0;
  }

  if (!file_.seek(idealStart)) {
    return false;
  }

  bufferStartPos_ = idealStart;
  bufferLen_ = file_.read(buffer_, BUFFER_SIZE);

  return bufferLen_ > 0;
}

char SimpleXmlParser::getByteAt(size_t pos) {
  if (!file_) {
    return '\0';
  }

  // Check if position is already in buffer
  if (bufferLen_ > 0 && pos >= bufferStartPos_ && pos < bufferStartPos_ + bufferLen_) {
    return (char)buffer_[pos - bufferStartPos_];
  }

  // Need to load buffer around this position
  if (!loadBufferAround(pos)) {
    return '\0';
  }

  if (pos >= bufferStartPos_ && pos < bufferStartPos_ + bufferLen_) {
    return (char)buffer_[pos - bufferStartPos_];
  }

  return '\0';
}

char SimpleXmlParser::peekChar() {
  return getByteAt(filePos_);
}

char SimpleXmlParser::readChar() {
  char c = getByteAt(filePos_);
  if (c != '\0') {
    filePos_++;
  }
  return c;
}

bool SimpleXmlParser::skipWhitespace() {
  while (true) {
    char c = peekChar();
    if (c == '\0') {
      return false;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      readChar();
    } else {
      return true;
    }
  }
}

bool SimpleXmlParser::matchString(const char* str) {
  size_t len = strlen(str);
  size_t savedFilePos = filePos_;

  for (size_t i = 0; i < len; i++) {
    char c = readChar();
    if (c != str[i]) {
      filePos_ = savedFilePos;
      return false;
    }
  }
  return true;
}

// ========== Forward Reading ==========

bool SimpleXmlParser::read() {
  if (!file_) {
    currentNodeType_ = EndOfFile;
    return false;
  }

  // If we just read a text node and haven't consumed it, skip to the next '<'
  if (currentNodeType_ == Text && textNodeCurrentPos_ > 0) {
    filePos_ = textNodeCurrentPos_;
    while (true) {
      char c = peekChar();
      if (c == '\0' || c == '<') {
        break;
      }
      readChar();
    }
  }

  // Clear previous state
  currentName_ = "";
  currentValue_ = "";
  isEmptyElement_ = false;
  attributes_.clear();
  textNodeStartPos_ = 0;
  textNodeEndPos_ = 0;
  textNodeCurrentPos_ = 0;
  peekedTextNodeChar_ = '\0';
  hasPeekedTextNodeChar_ = false;
  hasPeekedPrevTextNodeChar_ = false;
  elementStartPos_ = 0;
  elementEndPos_ = 0;

  while (true) {
    char c = peekChar();
    if (c == '\0') {
      currentNodeType_ = EndOfFile;
      return false;
    }

    if (c == '<') {
      readChar();  // consume '<'
      char next = peekChar();

      if (next == '/') {
        return readEndElement();
      } else if (next == '!') {
        readChar();  // consume '!'
        char peek2 = peekChar();
        if (peek2 == '-') {
          return readComment();
        } else if (peek2 == '[') {
          return readCDATA();
        }
        // Skip unknown declaration
        skipToEndOfTag();
        continue;
      } else if (next == '?') {
        return readProcessingInstruction();
      } else {
        return readElement();
      }
    } else {
      return readText();
    }
  }

  return false;
}

bool SimpleXmlParser::readElement() {
  elementStartPos_ = filePos_ - 1;  // -1 because we already consumed '<'
  currentNodeType_ = Element;
  currentName_ = readElementName();
  parseAttributes();

  skipWhitespace();
  char c = peekChar();
  if (c == '/') {
    readChar();
    isEmptyElement_ = true;
  }

  while (true) {
    c = readChar();
    if (c == '>' || c == '\0')
      break;
  }
  elementEndPos_ = filePos_;

  return true;
}

bool SimpleXmlParser::readEndElement() {
  elementStartPos_ = filePos_ - 1;  // -1 because we already consumed '<'
  currentNodeType_ = EndElement;
  readChar();  // consume '/'
  currentName_ = readElementName();

  while (true) {
    char c = readChar();
    if (c == '>' || c == '\0')
      break;
  }
  elementEndPos_ = filePos_;

  return true;
}

bool SimpleXmlParser::readText() {
  elementStartPos_ = filePos_;
  currentNodeType_ = Text;
  textNodeStartPos_ = filePos_;
  textNodeCurrentPos_ = filePos_;

  // Scan to find end and check for whitespace-only
  size_t scanPos = filePos_;
  bool hasNonWhitespace = false;

  while (true) {
    char c = getByteAt(scanPos);
    if (c == '\0' || c == '<') {
      break;
    }
    if (!hasNonWhitespace && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      hasNonWhitespace = true;
    }
    scanPos++;
  }

  textNodeEndPos_ = scanPos;
  elementEndPos_ = scanPos;

  // Skip whitespace-only text nodes
  if (!hasNonWhitespace) {
    filePos_ = textNodeEndPos_;
    return read();
  }

  return true;
}

bool SimpleXmlParser::readComment() {
  elementStartPos_ = filePos_ - 2;  // -2 for '<!' already consumed
  currentNodeType_ = Comment;
  currentValue_ = "";

  if (readChar() != '-' || peekChar() != '-') {
    skipToEndOfTag();
    elementEndPos_ = filePos_;
    return false;
  }
  readChar();  // consume second '-'

  while (true) {
    char c = readChar();
    if (c == '\0')
      break;

    if (c == '-' && peekChar() == '-') {
      readChar();
      if (peekChar() == '>') {
        readChar();
        break;
      }
      currentValue_ += '-';
      currentValue_ += '-';
    } else {
      currentValue_ += c;
    }
  }
  elementEndPos_ = filePos_;

  return true;
}

bool SimpleXmlParser::readCDATA() {
  elementStartPos_ = filePos_ - 2;  // -2 for '<!' already consumed
  currentNodeType_ = CDATA;
  currentValue_ = "";

  if (matchString("[CDATA[")) {
    while (true) {
      char c = readChar();
      if (c == '\0')
        break;

      if (c == ']' && peekChar() == ']') {
        readChar();
        if (peekChar() == '>') {
          readChar();
          break;
        }
        currentValue_ += ']';
        currentValue_ += ']';
      } else {
        currentValue_ += c;
      }
    }
  }
  elementEndPos_ = filePos_;

  return true;
}

bool SimpleXmlParser::readProcessingInstruction() {
  elementStartPos_ = filePos_ - 1;  // -1 for '<' already consumed
  currentNodeType_ = ProcessingInstruction;
  readChar();  // consume '?'

  currentName_ = readElementName();
  currentValue_ = "";

  while (true) {
    char c = readChar();
    if (c == '\0')
      break;

    if (c == '?' && peekChar() == '>') {
      readChar();
      break;
    }

    currentValue_ += c;
  }
  elementEndPos_ = filePos_;

  return true;
}

String SimpleXmlParser::readElementName() {
  String name;

  while (true) {
    char c = peekChar();
    if (c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>' || c == '/' || c == '=')
      break;

    name += readChar();
  }

  return name;
}

void SimpleXmlParser::parseAttributes() {
  attributes_.clear();

  while (true) {
    skipWhitespace();
    char c = peekChar();

    if (c == '>' || c == '/' || c == '\0')
      break;

    String attrName = readElementName();
    if (attrName.isEmpty())
      break;

    skipWhitespace();

    if (peekChar() != '=')
      break;
    readChar();

    skipWhitespace();

    char quote = peekChar();
    if (quote != '"' && quote != '\'')
      break;
    readChar();

    String attrValue;
    while (true) {
      char c = readChar();
      if (c == '\0' || c == quote)
        break;
      attrValue += c;
    }

    Attribute attr;
    attr.name = attrName;
    attr.value = attrValue;
    attributes_.push_back(attr);
  }
}

void SimpleXmlParser::skipToEndOfTag() {
  while (true) {
    char c = readChar();
    if (c == '>' || c == '\0')
      break;
  }
}

String SimpleXmlParser::getAttribute(const char* name) const {
  size_t nameLen = strlen(name);

  for (size_t i = 0; i < attributes_.size(); i++) {
    const String& attrName = attributes_[i].name;
    if (attrName.length() != nameLen)
      continue;

    bool match = true;
    for (size_t j = 0; j < nameLen; j++) {
      char c1 = attrName.charAt(j);
      char c2 = name[j];
      if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
      if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;
      if (c1 != c2) {
        match = false;
        break;
      }
    }

    if (match) {
      return attributes_[i].value;
    }
  }
  return String("");
}

// ========== Text Node Character Reading ==========

char SimpleXmlParser::readTextNodeCharForward() {
  if (currentNodeType_ != Text) {
    return '\0';
  }

  hasPeekedTextNodeChar_ = false;

  if (textNodeEndPos_ > 0 && textNodeCurrentPos_ >= textNodeEndPos_) {
    return '\0';
  }

  char c = getByteAt(textNodeCurrentPos_);
  if (c == '\0' || c == '<') {
    return '\0';
  }

  textNodeCurrentPos_++;
  filePos_ = textNodeCurrentPos_;

  return c;
}

char SimpleXmlParser::readTextNodeCharBackward() {
  if (currentNodeType_ != Text) {
    return '\0';
  }

  hasPeekedTextNodeChar_ = false;

  if (textNodeCurrentPos_ <= textNodeStartPos_) {
    return '\0';
  }

  textNodeCurrentPos_--;
  filePos_ = textNodeCurrentPos_;

  char c = getByteAt(textNodeCurrentPos_);
  if (c == '\0') {
    textNodeCurrentPos_++;
    return '\0';
  }

  return c;
}

char SimpleXmlParser::peekTextNodeChar() {
  if (currentNodeType_ != Text) {
    return '\0';
  }

  if (hasPeekedTextNodeChar_) {
    return peekedTextNodeChar_;
  }

  peekedTextNodeChar_ = getByteAt(textNodeCurrentPos_);

  if (peekedTextNodeChar_ == '<' || peekedTextNodeChar_ == '\0') {
    return '\0';
  }

  hasPeekedTextNodeChar_ = true;
  return peekedTextNodeChar_;
}

bool SimpleXmlParser::hasMoreTextChars() const {
  if (currentNodeType_ != Text) {
    return false;
  }

  if (textNodeEndPos_ > 0 && textNodeCurrentPos_ >= textNodeEndPos_) {
    return false;
  }

  char c = const_cast<SimpleXmlParser*>(this)->getByteAt(textNodeCurrentPos_);
  return c != '\0' && c != '<';
}

bool SimpleXmlParser::hasMoreTextCharsBackward() const {
  if (currentNodeType_ != Text) {
    return false;
  }
  return textNodeCurrentPos_ > textNodeStartPos_;
}

char SimpleXmlParser::peekPrevTextNodeChar() {
  if (currentNodeType_ != Text) {
    return '\0';
  }
  if (hasPeekedPrevTextNodeChar_) {
    return peekedPrevTextNodeChar_;
  }
  if (textNodeCurrentPos_ <= textNodeStartPos_) {
    return '\0';
  }
  peekedPrevTextNodeChar_ = getByteAt(textNodeCurrentPos_ - 1);
  hasPeekedPrevTextNodeChar_ = true;
  return peekedPrevTextNodeChar_;
}

char SimpleXmlParser::readPrevTextNodeChar() {
  if (currentNodeType_ != Text) {
    return '\0';
  }
  hasPeekedPrevTextNodeChar_ = false;

  if (textNodeCurrentPos_ <= textNodeStartPos_) {
    return '\0';
  }

  textNodeCurrentPos_--;
  filePos_ = textNodeCurrentPos_;  // Update filePos_ to match forward reading behavior
  char c = getByteAt(textNodeCurrentPos_);
  return c;
}

// ========== Backward Reading ==========

bool SimpleXmlParser::readBackward() {
  if (!file_) {
    currentNodeType_ = EndOfFile;
    return false;
  }

  // Clear previous state
  currentName_ = "";
  currentValue_ = "";
  isEmptyElement_ = false;
  attributes_.clear();
  textNodeStartPos_ = 0;
  textNodeEndPos_ = 0;
  textNodeCurrentPos_ = 0;
  peekedTextNodeChar_ = '\0';
  hasPeekedTextNodeChar_ = false;
  hasPeekedPrevTextNodeChar_ = false;
  elementStartPos_ = 0;
  elementEndPos_ = 0;

  if (filePos_ == 0) {
    currentNodeType_ = EndOfFile;
    return false;
  }

  // Look at the character just before current position
  size_t scanPos = filePos_ - 1;
  char c = getByteAt(scanPos);

  // Case 1: We're right after a '>' - this means a tag just ended here
  if (c == '>') {
    // Find the '<' that starts this tag
    size_t tagEnd = scanPos + 1;  // Position after '>'
    size_t tagStart = scanPos;

    while (tagStart > 0) {
      tagStart--;
      if (getByteAt(tagStart) == '<') {
        break;
      }
    }

    // Skip DOCTYPE and other declarations
    char tagLead = getByteAt(tagStart + 1);
    if (tagLead == '!') {
      filePos_ = tagStart;
      if (tagStart == 0) {
        currentNodeType_ = EndOfFile;
        return false;
      }
      return readBackward();
    }

    // Parse the tag by reading forward from tagStart
    filePos_ = tagStart;
    bool result = read();

    // Element positions are already set by read()
    // Just ensure they're correct for backward reading
    elementStartPos_ = tagStart;
    elementEndPos_ = tagEnd;

    // Set position for next backward read to be before this tag
    filePos_ = tagStart;

    return result;
  }

  // Case 2: We're in text content (not right after '>')
  // Find where this text starts (after previous '>') and ends (at next '<')
  size_t textEnd = filePos_;
  size_t textStart = 0;
  size_t searchPos = scanPos;

  // Scan backward to find '>' or '<'
  while (searchPos > 0) {
    char ch = getByteAt(searchPos);
    if (ch == '>') {
      textStart = searchPos + 1;
      break;
    }
    if (ch == '<') {
      // We're inside a tag, not text - parse the tag instead
      size_t tagStart = searchPos;
      filePos_ = searchPos;
      bool result = read();
      // Element positions are already set by read()
      // Just ensure elementStartPos is correct
      elementStartPos_ = tagStart;
      filePos_ = searchPos;
      return result;
    }
    searchPos--;
  }

  // If we reached the beginning without finding '>' or '<', text starts at 0
  if (searchPos == 0 && getByteAt(0) != '<' && getByteAt(0) != '>') {
    textStart = 0;
  }

  // Check if this is whitespace-only
  bool hasNonWhitespace = false;
  for (size_t pos = textStart; pos < textEnd; pos++) {
    char ch = getByteAt(pos);
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
      hasNonWhitespace = true;
      break;
    }
  }

  // Skip whitespace-only text nodes
  if (!hasNonWhitespace) {
    if (textStart == 0) {
      currentNodeType_ = EndOfFile;
      return false;
    }
    filePos_ = textStart;
    return readBackward();
  }

  // Set up as text node
  currentNodeType_ = Text;
  textNodeStartPos_ = textStart;
  textNodeEndPos_ = textEnd;
  textNodeCurrentPos_ = textEnd;  // Start at end for backward reading
  elementStartPos_ = textStart;
  elementEndPos_ = textEnd;

  // Position for next backward read
  filePos_ = textStart;

  return true;
}

// ========== Seeking ==========

bool SimpleXmlParser::seekToFilePosition(size_t pos) {
  if (!file_) {
    return false;
  }

  size_t fileSize = file_.size();
  if (pos > fileSize) {
    return false;
  }

  // Reset state
  currentNodeType_ = None;
  currentName_ = "";
  currentValue_ = "";
  isEmptyElement_ = false;
  attributes_.clear();
  textNodeStartPos_ = 0;
  textNodeEndPos_ = 0;
  textNodeCurrentPos_ = 0;
  hasPeekedTextNodeChar_ = false;
  hasPeekedPrevTextNodeChar_ = false;

  filePos_ = pos;

  // If at end of file, nothing more to do
  if (pos >= fileSize) {
    return true;
  }

  // Check what character we're at
  char c = getByteAt(pos);

  // If we're at a '<', we're at the start of a tag - nothing more to do
  if (c == '<') {
    return true;
  }

  // We might be in text content or inside a tag
  // Scan backward to find context
  size_t scanPos = pos;
  while (scanPos > 0) {
    scanPos--;
    char ch = getByteAt(scanPos);
    if (ch == '<') {
      // We're inside a tag - just set filePos and let read() handle it
      return true;
    }
    if (ch == '>') {
      // We're in text content between tags
      // Set up text node boundaries
      size_t textStart = scanPos + 1;

      // Find the end of this text (next '<')
      size_t textEnd = pos;
      while (textEnd < fileSize) {
        char endCh = getByteAt(textEnd);
        if (endCh == '<' || endCh == '\0') {
          break;
        }
        textEnd++;
      }

      // Check if this is whitespace-only between textStart and textEnd
      bool hasNonWhitespace = false;
      for (size_t checkPos = textStart; checkPos < textEnd; checkPos++) {
        char checkCh = getByteAt(checkPos);
        if (checkCh != ' ' && checkCh != '\t' && checkCh != '\n' && checkCh != '\r') {
          hasNonWhitespace = true;
          break;
        }
      }

      if (hasNonWhitespace) {
        // Set up as text node positioned at 'pos'
        currentNodeType_ = Text;
        textNodeStartPos_ = textStart;
        textNodeEndPos_ = textEnd;
        textNodeCurrentPos_ = pos;
      }
      // If whitespace-only, leave as None and let read() skip it

      return true;
    }
  }

  // We reached the beginning without finding '>' or '<'
  // This means we're in text at the start of the file
  if (getByteAt(0) != '<') {
    size_t textEnd = pos;
    while (textEnd < fileSize) {
      char endCh = getByteAt(textEnd);
      if (endCh == '<' || endCh == '\0') {
        break;
      }
      textEnd++;
    }

    bool hasNonWhitespace = false;
    for (size_t checkPos = 0; checkPos < textEnd; checkPos++) {
      char checkCh = getByteAt(checkPos);
      if (checkCh != ' ' && checkCh != '\t' && checkCh != '\n' && checkCh != '\r') {
        hasNonWhitespace = true;
        break;
      }
    }

    if (hasNonWhitespace) {
      currentNodeType_ = Text;
      textNodeStartPos_ = 0;
      textNodeEndPos_ = textEnd;
      textNodeCurrentPos_ = pos;
    }
  }

  return true;
}
