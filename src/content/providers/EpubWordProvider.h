#ifndef EPUB_WORD_PROVIDER_H
#define EPUB_WORD_PROVIDER_H

#include <SD.h>

#include <cstdint>
#include <vector>

#include "../epub/EpubReader.h"
#include "../xml/SimpleXmlParser.h"
#include "FileWordProvider.h"
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
  StyledWord getNextWord() override;
  StyledWord getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  float getChapterPercentage() override;
  float getChapterPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  int consumeChars(int n) override;
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
  String getCurrentChapterName() override {
    return currentChapterName_;
  }

  // Style support
  CssStyle getCurrentStyle() override {
    return CssStyle();
  }

  TextAlign getParagraphAlignment() override {
    if (fileProvider_)
      return fileProvider_->getParagraphAlignment();
    return TextAlign::Left;
  }

  // Streaming conversion mode (true = extract to memory, false = extract to file first)
  void setUseStreamingConversion(bool enabled) {
    useStreamingConversion_ = enabled;
  }
  bool getUseStreamingConversion() const {
    return useStreamingConversion_;
  }

 private:
  // Opens a specific chapter (spine item) for reading
  bool openChapter(int chapterIndex);

  // Helper to check if an element is a block-level element
  bool isBlockElement(const String& name);

  // Helper to check if an element's content should be skipped (head, title, style, script)
  bool isSkippedElement(const String& name);

  // Helper to check if an element is a header element (h1-h6)
  bool isHeaderElement(const String& name);

  // Helper to check if an element is an inline style element (b, strong, i, em, span)
  bool isInlineStyleElement(const String& name);

  // Convert an XHTML file to a plain-text file suitable for FileWordProvider.
  bool convertXhtmlToTxt(const String& srcPath, String& outTxtPath);

  // Convert XHTML from EPUB stream to plain-text file (no intermediate XHTML file)
  bool convertXhtmlStreamToTxt(const char* epubFilename, String& outTxtPath);

  // Common conversion logic used by both convertXhtmlToTxt and convertXhtmlStreamToTxt
  void performXhtmlToTxtConversion(SimpleXmlParser& parser, File& out);

  // Emit style properties for a paragraph's classes and inline styles as an escaped token written to buffer
  void writeParagraphStyleToken(String& writeBuffer, const String& pendingParagraphClasses,
                                const String& pendingInlineStyle, bool& paragraphClassesWritten);

  // Emit inline style token (for bold/italic elements like <b>, <i>, <em>, <strong>, <span>)
  // Returns true if a style token was emitted, false otherwise
  bool writeInlineStyleToken(String& writeBuffer, const String& elementName, const String& classAttr,
                             const String& styleAttr);

  // Emit style reset token (to return to normal after inline style element closes)
  void writeStyleResetToken(String& writeBuffer);

  // Helper to create directories recursively for a given path
  bool createDirRecursive(const String& path);

  // Text processing helpers
  bool isInsideSkippedElement(const std::vector<String>& elementStack);
  String readAndDecodeText(SimpleXmlParser& parser);
  String decodeHtmlEntity(const String& entity);
  String normalizeWhitespace(const String& text);
  void trimTrailingSpaces(String& buffer);
  String trimLeadingSpaces(const String& text);

  bool valid_ = false;
  bool isEpub_ = false;                 // True if source is EPUB, false if direct XHTML
  bool useStreamingConversion_ = true;  // True = stream from EPUB to memory, false = extract XHTML file first
  size_t bufSize_ = 0;

  String epubPath_;
  String xhtmlPath_;                  // Path to current extracted XHTML file
  String currentChapterName_;         // Cached chapter name from TOC
  EpubReader* epubReader_ = nullptr;  // Kept alive for chapter navigation
  SimpleXmlParser* parser_ = nullptr;
  int currentChapter_ = 0;  // Current chapter index (0-based)

  // Underlying provider that reads the converted plain-text chapter files
  FileWordProvider* fileProvider_ = nullptr;

  size_t fileSize_;          // Total file size for percentage calculation
  size_t currentIndex_ = 0;  // Current index/offset (seeking disabled; tracked locally)
};

#endif
