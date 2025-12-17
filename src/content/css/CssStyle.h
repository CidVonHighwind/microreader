#ifndef CSS_STYLE_H
#define CSS_STYLE_H

#include <Arduino.h>

/**
 * Text alignment values supported by the reader
 */
enum class TextAlign {
  None,    // Default none alignment
  Left,    // Left alignment
  Right,   // Right alignment
  Center,  // Center alignment
  Justify  // Justified text (both edges aligned)
};

/**
 * Font style values (italic)
 */
enum class CssFontStyle {
  Normal,  // Default normal style
  Italic   // Italic text
};

/**
 * Font weight values (bold)
 */
enum class CssFontWeight {
  Normal,  // Default normal weight
  Bold     // Bold text
};

/**
 * CssStyle - Represents supported CSS properties for a selector
 *
 * This structure holds the subset of CSS properties that the reader supports.
 * Currently supported:
 * - text-align: left, right, center, justify
 *
 * Properties can be extended in the future to support:
 * - font-style: normal, italic
 * - font-weight: normal, bold
 *
 * Future extensions:
 * - text-indent
 * - margin-top/bottom (for paragraph spacing)
 */
struct CssStyle {
  TextAlign textAlign = TextAlign::Left;
  bool hasTextAlign = false;  // True if text-align was explicitly set

  CssFontStyle fontStyle = CssFontStyle::Normal;
  bool hasFontStyle = false;  // True if font-style was explicitly set

  CssFontWeight fontWeight = CssFontWeight::Normal;
  bool hasFontWeight = false;  // True if font-weight was explicitly set

  // Text-indent support (in CSS units, stored here as pixels approximation)
  float textIndent = 0.0f;
  bool hasTextIndent = false;

  // Merge another style into this one (other style takes precedence)
  void merge(const CssStyle& other) {
    if (other.hasTextAlign) {
      textAlign = other.textAlign;
      hasTextAlign = true;
    }
    if (other.hasFontStyle) {
      fontStyle = other.fontStyle;
      hasFontStyle = true;
    }
    if (other.hasFontWeight) {
      fontWeight = other.fontWeight;
      hasFontWeight = true;
    }
    if (other.hasTextIndent) {
      textIndent = other.textIndent;
      hasTextIndent = true;
    }
  }

  // Reset to default values
  void reset() {
    textAlign = TextAlign::Left;
    hasTextAlign = false;
    fontStyle = CssFontStyle::Normal;
    hasFontStyle = false;
    fontWeight = CssFontWeight::Normal;
    hasFontWeight = false;
  }
};

/**
 * ActiveStyle - Tracks the currently active style during parsing
 *
 * This is used by the word provider to track what styles are in effect
 * as elements are entered and exited.
 */
struct ActiveStyle {
  CssStyle style;
  bool isBlockElement = false;  // True if this style came from a block element
};

#endif
