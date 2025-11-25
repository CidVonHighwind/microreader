#ifndef STRING_WORD_PROVIDER_H
#define STRING_WORD_PROVIDER_H

#include "WString.h"
#include "WordProvider.h"

class StringWordProvider : public WordProvider {
 public:
  StringWordProvider(const String& text);
  ~StringWordProvider();

  bool hasNextWord() override;

  String getNextWord(TextRenderer& renderer) override;
  String getPrevWord(TextRenderer& renderer) override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  void ungetWord() override;
  void reset() override;

 private:
  // Unified scanner: `direction` should be +1 for forward scanning and -1 for backward scanning
  String scanWord(int direction, TextRenderer& renderer);

  String text_;
  int index_;
  int prevIndex_;
};

#endif