#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include <Arduino.h>
#include <SD.h>

#include <map>
#include <vector>

#include "CssStyle.h"

/**
 * CssParser - Simple CSS parser for extracting supported properties
 *
 * This parser extracts CSS rules and maps class selectors to their
 * supported style properties. It handles:
 * - Class selectors (.classname)
 * - Element.class selectors (p.classname)
 * - Multiple selectors separated by commas
 *
 * Limitations:
 * - Does not support complex selectors (descendant, child, etc.)
 * - Does not support pseudo-classes or pseudo-elements
 * - Only extracts properties we actually use (text-align)
 */
class CssParser {
 public:
  CssParser();
  ~CssParser();

  /**
   * Parse a CSS file and add its rules to the style map
   * Returns true if parsing was successful
   */
  bool parseFile(const char* filepath);

  /**
   * Get the style for a given class name
   * Returns nullptr if no style is defined for this class
   */
  const CssStyle* getStyleForClass(const String& className) const;

  /**
   * Get the combined style for multiple class names (space-separated)
   * Styles are merged in order, later classes override earlier ones
   */
  CssStyle getCombinedStyle(const String& classNames) const;

  /**
   * Parse an inline style attribute (e.g., "text-align: center; color: red;")
   * Returns a CssStyle with the parsed properties
   */
  CssStyle parseInlineStyle(const String& styleAttr) const;

  /**
   * Check if any styles have been loaded
   */
  bool hasStyles() const {
    return !styleMap_.empty();
  }

  /**
   * Get the number of loaded style rules
   */
  size_t getStyleCount() const {
    return styleMap_.size();
  }

  /**
   * Clear all loaded styles
   */
  void clear() {
    styleMap_.clear();
  }

 private:
  // Parse a single rule block (selector { properties })
  void parseRule(const String& selector, const String& properties);

  // Parse property value and update style
  void parseProperty(const String& name, const String& value, CssStyle& style);

  // Parse text-align value
  TextAlign parseTextAlign(const String& value);

  // Parse font-style value
  CssFontStyle parseFontStyle(const String& value);

  // Parse font-weight value
  CssFontWeight parseFontWeight(const String& value);

  // Extract class name from a selector (e.g., ".foo" or "p.foo" -> "foo")
  String extractClassName(const String& selector);

  // Map of class names to their styles
  std::map<String, CssStyle> styleMap_;
};

#endif
