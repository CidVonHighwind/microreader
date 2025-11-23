#ifndef TEXT_LAYOUT_H
#define TEXT_LAYOUT_H

#include <WString.h>

#include <vector>

#include "LayoutStrategy.h"

class TextRenderer;  // Forward declaration

class TextLayout {
 public:
  // Re-export LayoutConfig for convenience
  using LayoutConfig = LayoutStrategy::LayoutConfig;

  // Minimum cost for a breakpoint. Values <= MIN_COST force a break.
  static constexpr int32_t MIN_COST = -1000000;  // Scaled by 1000

  // Maximum cost for a breakpoint. Values >= MAX_COST prevent a break.
  static constexpr int32_t MAX_COST = 1000000;  // Scaled by 1000

  // Constructor - uses default greedy layout strategy
  TextLayout();

  // Constructor with custom layout strategy
  explicit TextLayout(LayoutStrategy* strategy);

  // Destructor
  ~TextLayout();

  // Set the layout strategy (takes ownership)
  void setStrategy(LayoutStrategy* strategy);

  // Layout text on a page using the current layout strategy
  void layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config);

 private:
  // Layout strategy (owned by this object)
  LayoutStrategy* strategy_;
};

#endif
