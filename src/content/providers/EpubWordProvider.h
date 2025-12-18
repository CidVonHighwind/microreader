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
#include "../../text/hyphenation/HyphenationStrategy.h"

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

  // Get the language of the EPUB for hyphenation
  Language getLanguage() const;

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
  struct ConversionTimings {
    unsigned long startStream = 0;
    unsigned long parserOpen = 0;
    unsigned long outOpen = 0;
    unsigned long conversion = 0;
    unsigned long parserClose = 0;
    unsigned long endStream = 0;
    unsigned long closeOut = 0;
    unsigned long total = 0;
    size_t bytes = 0;
  };
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
  bool convertXhtmlToTxt(const String& srcPath, String& outTxtPath, ConversionTimings* timings = nullptr);

  // Convert XHTML from EPUB stream to plain-text file (no intermediate XHTML file)
  bool convertXhtmlStreamToTxt(const char* epubFilename, String& outTxtPath, ConversionTimings* timings = nullptr);

  // Common conversion logic used by both convertXhtmlToTxt and convertXhtmlStreamToTxt
  // If outBytes is provided, it will be set to the number of bytes written to `out`.
  void performXhtmlToTxtConversion(SimpleXmlParser& parser, File& out, size_t* outBytes = nullptr);

  // Emit style properties for a paragraph's classes and inline styles as an escaped token written to buffer
  void writeParagraphStyleToken(String& writeBuffer, const String& pendingParagraphClasses,
                                const String& pendingInlineStyle, bool& paragraphClassesWritten,
                                std::vector<char>& paragraphStyleEmitted);

  // Emit inline style token (for bold/italic elements like <b>, <i>, <em>, <strong>, <span>)
  // Returns the uppercase command char emitted (e.g. 'B','I','X') or '\0' if none
  char writeInlineStyleToken(String& writeBuffer, const String& elementName, const String& classAttr,
                             const String& styleAttr);

  // Close an inline style element (called when an inline element ends)
  void closeInlineStyleElement(String& writeBuffer);

  // Track active inline style stack for correct combined styling (bold+italic = 'X')
  struct InlineStyleState {
    // Value and whether it was explicitly specified for this element.
    // If hasBold/hasItalic is true, the corresponding value should override
    // any previously-set base or ancestor inline styles.
    bool bold = false;
    bool italic = false;
    bool hasBold = false;
    bool hasItalic = false;
  };
  std::vector<InlineStyleState> inlineStyleStack_;
  char currentInlineCombined_ = '\0';
  // The currently-written inline style combination (what's been emitted to the buffer).
  // This is kept separate from `currentInlineCombined_` (the effective style) so we
  // can delay emitting style tokens until the moment actual text is written.
  char writtenInlineCombined_ = '\0';
  // Base inline style (from paragraph-level CSS classes / inline style)
  InlineStyleState baseInlineStyle_;

  // Recompute the effective combined style char (`currentInlineCombined_`) from
  // the paragraph base style and the inline style stack (stack entries can
  // explicitly override base and ancestor values if they specify the property).
  void updateEffectiveInlineCombined();

  // Emit style reset token (to return to normal after inline style element closes)
  void writeStyleResetToken(String& writeBuffer, char startCmd);

  // Ensure that the currently-emitted inline style in the output buffer matches
  // the effective inline style state (`currentInlineCombined_`). This will emit
  // the necessary reset/open tokens just before writing visible text.
  void ensureInlineStyleEmitted(String& writeBuffer);

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
