#ifndef STRING_WORD_PROVIDER_H
#define STRING_WORD_PROVIDER_H

#include "WString.h"
#include "WordProvider.h"

class StringWordProvider : public WordProvider {
 public:
  StringWordProvider(const String& text);
  ~StringWordProvider();

  bool hasNextWord() override;
  bool hasPrevWord() override;

  StyledWord getNextWord() override;
  StyledWord getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  int consumeChars(int n) override;
  bool isInsideWord() override;
  void ungetWord() override;
  void reset() override;

 private:
  // Unified scanner: `direction` should be +1 for forward scanning and -1 for backward scanning
  StyledWord scanWord(int direction);

  String text_;
  int index_;
  int prevIndex_;
};

#endif