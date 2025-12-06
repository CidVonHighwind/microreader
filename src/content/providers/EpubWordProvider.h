#ifndef EPUB_WORD_PROVIDER_H
#define EPUB_WORD_PROVIDER_H

#include <SD.h>

#include <cstdint>

#include "../epub/EpubReader.h"
#include "../xml/SimpleXmlParser.h"
#include "StringWordProvider.h"
#include "WordProvider.h"

class EpubWordProvider : public WordProvider {
 public:
  // path: SD path to epub file or direct xhtml file
  // bufSize: decompressed text buffer size (default 4096)
  EpubWordProvider(const char* path, size_t bufSize = 4096);
  ~EpubWordProvider() override;
  bool isValid() const {
    return valid_;
  }

  bool hasNextWord() override;
  bool hasPrevWord() override;
  String getNextWord() override;
  String getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  bool isInsideWord() override;
  void ungetWord() override;
  void reset() override;

  // Chapter navigation
  int getChapterCount() override;
  int getCurrentChapter() override;
  bool setChapter(int chapterIndex) override;
  bool hasChapters() override {
    return isEpub_;
  }

 private:
  // Opens a specific chapter (spine item) for reading
  bool openChapter(int chapterIndex);

  bool valid_ = false;
  bool isEpub_ = false;  // True if source is EPUB, false if direct XHTML
  size_t bufSize_ = 0;

  String epubPath_;
  String xhtmlPath_;                  // Path to current extracted XHTML file
  EpubReader* epubReader_ = nullptr;  // Kept alive for chapter navigation
  SimpleXmlParser* parser_ = nullptr;
  int currentChapter_ = 0;  // Current chapter index (0-based)

  size_t prevFilePos_;                                              // Previous parser file position for ungetWord()
  bool prevInsideParagraph_ = false;                                // Previous paragraph state for ungetWord()
  SimpleXmlParser::NodeType prevNodeType_ = SimpleXmlParser::None;  // Previous node type for ungetWord()
  size_t prevTextNodeStart_ = 0;                                    // Previous text node start for ungetWord()
  size_t prevTextNodeEnd_ = 0;                                      // Previous text node end for ungetWord()
  String prevElementName_;                                          // Previous element name for ungetWord()
  bool prevIsEmptyElement_ = false;                                 // Previous empty element flag for ungetWord()
  size_t fileSize_;                                                 // Total file size for percentage calculation
  bool insideParagraph_ = false;                                    // Track if we're inside a <p> tag
  bool pendingNewline_ = false;  // For backward reading: newline to emit before next paragraph content
};

#endif
