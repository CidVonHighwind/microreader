#ifndef STRING_WORD_PROVIDER_H
#define STRING_WORD_PROVIDER_H

#include "WordProvider.h"

class StringWordProvider : public WordProvider {
 public:
  StringWordProvider(const String& text);
  ~StringWordProvider();

  bool hasNextWord() override;

  LayoutStrategy::Word getNextWord(TextRenderer& renderer) override;
  LayoutStrategy::Word getPrevWord(TextRenderer& renderer) override;

  float getPercentage() override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  void ungetWord() override;
  void reset() override;

 private:
  String text_;
  int index_;
  int prevIndex_;
};

#endif