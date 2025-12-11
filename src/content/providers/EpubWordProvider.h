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
  String getNextWord() override;
  String getPrevWord() override;

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

  // Convert an XHTML file to a plain-text file suitable for FileWordProvider.
  bool convertXhtmlToTxt(const String& srcPath, String& outTxtPath);

  // Convert XHTML from EPUB stream to plain-text file (no intermediate XHTML file)
  bool convertXhtmlStreamToTxt(const char* epubFilename, String& outTxtPath);

  // Common conversion logic used by both convertXhtmlToTxt and convertXhtmlStreamToTxt
  void performXhtmlToTxtConversion(SimpleXmlParser& parser, File& out);

  // Emit style properties for a paragraph's classes as an escaped token written to buffer
  void writeParagraphStyleToken(String& writeBuffer, const String& pendingParagraphClasses,
                                bool& paragraphClassesWritten);

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
