#ifndef SIMPLE_XML_PARSER_H
#define SIMPLE_XML_PARSER_H

#include <Arduino.h>
#include <SD.h>

#include <utility>
#include <vector>

/**
 * SimpleXmlParser - A buffered XML parser for reading attributes
 *
 * This parser uses a read buffer to minimize SD card I/O operations.
 * It's designed for simple tag and attribute extraction.
 */
class SimpleXmlParser {
 public:
  SimpleXmlParser();
  ~SimpleXmlParser();

  /**
   * Open an XML file for parsing
   * Returns true if successful
   */
  bool open(const char* filepath);

  /**
   * Close the current file
   */
  void close();

  // ========== Node Navigation API ==========

  enum NodeType {
    None = 0,
    Element,                // Opening tag like <div>
    Text,                   // Text content
    EndElement,             // Closing tag like </div>
    Comment,                // <!-- comment -->
    ProcessingInstruction,  // <?xml ... ?>
    CDATA,                  // <![CDATA[ ... ]]>
    EndOfFile
  };

  /**
   * Read next node from XML stream
   * Returns true if a node was read, false if end of file
   * Call getNodeType() to determine what was read
   */
  bool read();

  /**
   * Read previous node from XML stream (backward navigation)
   * Returns true if a node was read, false if beginning of file
   * Call getNodeType() to determine what was read
   */
  bool readBackward();

  /**
   * Get the type of the current node
   */
  NodeType getNodeType() const {
    return currentNodeType_;
  }

  /**
   * Get the name of the current element (for Element/EndElement nodes)
   * Returns empty string for other node types
   */
  String getName() const {
    return currentName_;
  }

  /**
   * Check if current element is empty (self-closing like <br/>)
   * Only valid for Element nodes
   */
  bool isEmptyElement() const {
    return isEmptyElement_;
  }

  /**
   * Get attribute value by name from current element
   * Returns empty string if attribute not found
   * Only valid for Element nodes
   */
  String getAttribute(const char* name) const;

  /**
   * Peek at next character in current text node without advancing
   * Only valid when on a Text node
   * Returns '\0' when end of text node reached
   */
  char peekTextNodeChar();

  /**
   * Check if there are more characters in current text node
   * Only valid when on a Text node
   */
  bool hasMoreTextChars() const;

  /**
   * Check if there are more characters backward in current text node
   * Only valid when on a Text node
   */
  bool hasMoreTextCharsBackward() const;

  /**
   * Peek at previous character in current text node without moving backward
   * Only valid when on a Text node
   */
  char peekPrevTextNodeChar();

  /**
   * Read previous character from current text node, moving backward
   * Only valid when on a Text node
   */
  char readPrevTextNodeChar();

  // Text node reading helpers
  char readTextNodeCharForward();
  char readTextNodeCharBackward();

  /**
   * Seek to a specific file position.
   * After seeking, you must call read() or readBackward() to parse a node.
   * Returns true if successful
   */
  bool seekToFilePosition(size_t pos);

  /**
   * Get current file position
   * For text nodes: returns current position within the text
   * For elements: returns the start of the element (where '<' begins)
   * This ensures seeking to this position will re-read the same node
   */
  size_t getFilePosition() const {
    // When in a text node, return the current position within the text
    if (currentNodeType_ == Text) {
      return textNodeCurrentPos_;
    }
    // For elements, return the start position so seeking here re-reads the element
    if (currentNodeType_ == Element || currentNodeType_ == EndElement) {
      return elementStartPos_;
    }
    return filePos_;
  }

  /**
   * Get the start position of the current element/node in the file
   * This is the position where the element begins (e.g., the '<' for tags)
   */
  size_t getElementStartPos() const {
    return elementStartPos_;
  }

  /**
   * Get the end position of the current element/node in the file
   * This is the position after the element ends (e.g., after '>' for tags)
   */
  size_t getElementEndPos() const {
    return elementEndPos_;
  }

  /**
   * Get total file size
   */
  size_t getFileSize() const {
    if (!file_) {
      return 0;
    }
    return file_.size();
  }

 private:
  File file_;

  // Buffering for faster I/O
  static const size_t BUFFER_SIZE = 8192;
  uint8_t buffer_[BUFFER_SIZE];
  size_t bufferStartPos_;  // File position of first byte in buffer
  size_t bufferLen_;       // Number of valid bytes in buffer
  size_t filePos_;         // Current position in file

  // Helper functions
  char getByteAt(size_t pos);         // Get byte at any position, loading buffer if needed
  bool loadBufferAround(size_t pos);  // Load buffer centered around position
  bool skipWhitespace();
  bool matchString(const char* str);
  char readChar();
  char peekChar();

  // Node state
  struct Attribute {
    String name;
    String value;
  };

  NodeType currentNodeType_;
  String currentName_;
  String currentValue_;  // Only used for Comment, CDATA, ProcessingInstruction nodes
  bool isEmptyElement_;
  std::vector<Attribute> attributes_;

  // Text node reading state
  size_t textNodeStartPos_;     // File position where text node content starts
  size_t textNodeEndPos_;       // File position where text node content ends
  size_t textNodeCurrentPos_;   // Current position within text node
  char peekedTextNodeChar_;     // Cached character for peekTextNodeChar
  bool hasPeekedTextNodeChar_;  // Whether we have a peeked character
  // Backward text node reading state
  char peekedPrevTextNodeChar_;
  bool hasPeekedPrevTextNodeChar_;

  // Element/node position tracking
  size_t elementStartPos_;  // Start position of current element in file
  size_t elementEndPos_;    // End position of current element in file

  // XmlReader helper methods
  bool readElement();
  bool readEndElement();
  bool readText();
  bool readComment();
  bool readCDATA();
  bool readProcessingInstruction();
  void parseAttributes();
  String readElementName();
  void skipToEndOfTag();
};

#endif
