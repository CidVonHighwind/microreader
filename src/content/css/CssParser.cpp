#include "CssParser.h"

CssParser::CssParser() {}

CssParser::~CssParser() {}

bool CssParser::parseFile(const char* filepath) {
  File file = SD.open(filepath);
  if (!file) {
    Serial.printf("CssParser: Failed to open %s\n", filepath);
    return false;
  }

  // Stream parse: read character-by-character and process rules
  String selector;
  String properties;
  bool inComment = false;
  bool inAtRule = false;
  bool inRule = false;
  bool inString = false;
  char stringQuote = 0;
  int braceCount = 0;

  // Use a single-character pushback instead of File::peek(), which may be missing in mocks
  int pushback = -1;
  while (file.available() || pushback != -1) {
    char c;
    if (pushback != -1) {
      c = (char)pushback;
      pushback = -1;
    } else {
      c = (char)file.read();
    }

    // Handle comment start '/*'
    if (!inComment && c == '/') {
      // Probe next char
      int next = -1;
      if (file.available())
        next = file.read();
      if (next == '*') {
        inComment = true;
        continue;
      }
      // Not a comment. Push back the next char if any, and continue processing '/'
      if (next != -1)
        pushback = next;
    }

    if (inComment) {
      // Look for end of comment '*/'
      if (c == '*') {
        int next = -1;
        if (file.available())
          next = file.read();
        if (next == '/') {
          inComment = false;
        } else if (next != -1) {
          pushback = next;
        }
      }
      continue;
    }

    // Ignore carriage returns entirely
    if (c == '\r')
      continue;

    if (!inRule) {
      // Handle AT-rules by skipping until ; or matching braces
      if (inAtRule) {
        if (c == '{') {
          braceCount++;
        } else if (c == '}') {
          if (braceCount > 0) {
            braceCount--;
            if (braceCount == 0) {
              inAtRule = false;
            }
          }
        } else if (c == ';' && braceCount == 0) {
          inAtRule = false;
        }
        continue;
      }

      // Not in rule and not in AT-rule
      if (c == '@') {
        inAtRule = true;
        braceCount = 0;
        // start collecting the at-rule but we don't need it; skip it
        continue;
      }

      if (c == '{') {
        // start of a declaration block
        inRule = true;
        braceCount = 1;
        selector.trim();
        properties = "";
        continue;
      }

      // Normal selector characters
      selector += c;
    } else {
      // We are inside a declaration block for a selector
      // Track quoted strings so braces inside values don't confuse us
      if (!inString && (c == '"' || c == '\'')) {
        inString = true;
        stringQuote = c;
        properties += c;
        continue;
      } else if (inString && c == stringQuote) {
        inString = false;
        stringQuote = 0;
        properties += c;
        continue;
      }

      if (!inString) {
        if (c == '{') {
          braceCount++;
          properties += c;
          continue;
        } else if (c == '}') {
          braceCount--;
          if (braceCount == 0) {
            // End of declaration block; parse the rule
            properties.trim();
            if (selector.length() > 0 && properties.length() > 0) {
              parseRule(selector, properties);
            }
            // Reset for next rule
            selector = "";
            properties = "";
            inRule = false;
            continue;
          }
        }
      }

      // Append character to properties
      properties += c;
    }
  }

  // If EOF reached and still inside a rule, try to finalize it
  if (inRule && properties.length() > 0) {
    properties.trim();
    selector.trim();
    if (selector.length() > 0) {
      parseRule(selector, properties);
    }
  }

  file.close();
  Serial.printf("  CssParser: Loaded %d style rules\n", styleMap_.size());
  return true;
}

const CssStyle* CssParser::getStyleForClass(const String& className) const {
  auto it = styleMap_.find(className);
  if (it != styleMap_.end()) {
    return &it->second;
  }
  return nullptr;
}

CssStyle CssParser::getCombinedStyle(const String& classNames) const {
  CssStyle combined;

  // Split class names by whitespace
  int start = 0;
  int len = classNames.length();

  while (start < len) {
    // Skip leading whitespace
    while (start < len &&
           (classNames.charAt(start) == ' ' || classNames.charAt(start) == '\t' || classNames.charAt(start) == '\n')) {
      start++;
    }
    if (start >= len)
      break;

    // Find end of class name
    int end = start;
    while (end < len && classNames.charAt(end) != ' ' && classNames.charAt(end) != '\t' &&
           classNames.charAt(end) != '\n') {
      end++;
    }

    if (end > start) {
      String className = classNames.substring(start, end);
      const CssStyle* style = getStyleForClass(className);
      if (style) {
        combined.merge(*style);
      }
    }

    start = end;
  }

  return combined;
}

void CssParser::parseRule(const String& selector, const String& properties) {
  // Parse the selector - handle comma-separated selectors
  int start = 0;
  int len = selector.length();

  while (start < len) {
    // Find next comma or end
    int end = selector.indexOf(',', start);
    if (end < 0)
      end = len;

    String singleSelector = selector.substring(start, end);
    singleSelector.trim();

    if (singleSelector.length() > 0) {
      // Extract class name from selector
      String className = extractClassName(singleSelector);

      if (className.length() > 0) {
        // Parse properties
        CssStyle style;

        // Split properties by semicolon
        int propStart = 0;
        int propLen = properties.length();

        while (propStart < propLen) {
          int propEnd = properties.indexOf(';', propStart);
          if (propEnd < 0)
            propEnd = propLen;

          String prop = properties.substring(propStart, propEnd);
          prop.trim();

          if (prop.length() > 0) {
            // Split property into name and value
            int colonPos = prop.indexOf(':');
            if (colonPos > 0) {
              String propName = prop.substring(0, colonPos);
              String propValue = prop.substring(colonPos + 1);
              propName.trim();
              propValue.trim();

              // Convert to lowercase for comparison
              propName.toLowerCase();

              parseProperty(propName, propValue, style);
            }
          }

          propStart = propEnd + 1;
        }

        // Store style if it has any supported properties
        if (style.hasTextAlign || style.hasFontStyle || style.hasFontWeight) {
          // Merge with existing style if present
          auto it = styleMap_.find(className);
          if (it != styleMap_.end()) {
            it->second.merge(style);
          } else {
            styleMap_[className] = style;
          }
        }
      }
    }

    start = end + 1;
  }
}

void CssParser::parseProperty(const String& name, const String& value, CssStyle& style) {
  if (name == "text-align") {
    style.textAlign = parseTextAlign(value);
    style.hasTextAlign = true;
  } else if (name == "font-style") {
    style.fontStyle = parseFontStyle(value);
    style.hasFontStyle = true;
  } else if (name == "font-weight") {
    style.fontWeight = parseFontWeight(value);
    style.hasFontWeight = true;
  } else if (name == "text-indent") {
    // Parse text-indent values like '20px', '1.5em', or plain numbers.
    String v = value;
    v.trim();
    v.toLowerCase();

    // Default unit: pixels. For 'em' convert to px assuming 16px per em.
    float factor = 1.0f;
    // endsWith / toFloat may not be available on all String implementations
    if (v.length() >= 2 && v.substring(v.length() - 2) == String("em")) {
      factor = 16.0f;
      v = v.substring(0, v.length() - 2);
    } else if (v.length() >= 2 && v.substring(v.length() - 2) == String("px")) {
      v = v.substring(0, v.length() - 2);
    }

    v.trim();
    // Attempt to parse float value using C's atof on the c_str
    float indentVal = 0.0f;
    if (v.length() > 0) {
      indentVal = static_cast<float>(atof(v.c_str())) * factor;
    }
    style.textIndent = indentVal;
    style.hasTextIndent = (indentVal > 0.0f);
  }
  // Add more property parsing here as needed
}

TextAlign CssParser::parseTextAlign(const String& value) {
  String v = value;
  v.toLowerCase();
  v.trim();

  if (v == "left" || v == "start") {
    return TextAlign::Left;
  } else if (v == "right" || v == "end") {
    return TextAlign::Right;
  } else if (v == "center") {
    return TextAlign::Center;
  } else if (v == "justify") {
    return TextAlign::Justify;
  }

  // Default to left
  return TextAlign::Left;
}

CssFontStyle CssParser::parseFontStyle(const String& value) {
  String v = value;
  v.toLowerCase();
  v.trim();

  if (v == "italic" || v == "oblique") {
    return CssFontStyle::Italic;
  }

  // Default to normal
  return CssFontStyle::Normal;
}

CssFontWeight CssParser::parseFontWeight(const String& value) {
  String v = value;
  v.toLowerCase();
  v.trim();

  if (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900") {
    return CssFontWeight::Bold;
  }

  // Default to normal
  return CssFontWeight::Normal;
}

// skipWhitespaceAndComments removed (used by parseString which was removed)

// findSelectorEnd removed (used by parseString)

// findRuleEnd removed (used by parseString)

String CssParser::extractClassName(const String& selector) {
  // Find the class selector (starts with '.')
  int dotPos = selector.indexOf('.');
  if (dotPos < 0) {
    return String("");  // No class selector
  }

  // Extract class name (everything after '.' until end or special char)
  int start = dotPos + 1;
  int end = start;
  int len = selector.length();

  while (end < len) {
    char c = selector.charAt(end);
    // Class name can contain letters, digits, hyphens, underscores
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      end++;
    } else {
      break;
    }
  }

  if (end > start) {
    return selector.substring(start, end);
  }

  return String("");
}

CssStyle CssParser::parseInlineStyle(const String& styleAttr) const {
  CssStyle style;

  if (styleAttr.isEmpty()) {
    return style;
  }

  // Parse inline style declarations (semicolon-separated)
  // Format: "property1: value1; property2: value2;"
  int propStart = 0;
  int propLen = styleAttr.length();

  while (propStart < propLen) {
    int propEnd = styleAttr.indexOf(';', propStart);
    if (propEnd < 0)
      propEnd = propLen;

    String prop = styleAttr.substring(propStart, propEnd);
    prop.trim();

    if (prop.length() > 0) {
      // Split property into name and value
      int colonPos = prop.indexOf(':');
      if (colonPos > 0) {
        String propName = prop.substring(0, colonPos);
        String propValue = prop.substring(colonPos + 1);
        propName.trim();
        propValue.trim();

        // Convert to lowercase for comparison
        propName.toLowerCase();

        // Use the same parsing logic as parseProperty
        // Note: parseProperty is non-const, so we inline the logic here
        if (propName == "text-align") {
          String v = propValue;
          v.toLowerCase();
          v.trim();

          if (v == "left" || v == "start") {
            style.textAlign = TextAlign::Left;
          } else if (v == "right" || v == "end") {
            style.textAlign = TextAlign::Right;
          } else if (v == "center") {
            style.textAlign = TextAlign::Center;
          } else if (v == "justify") {
            style.textAlign = TextAlign::Justify;
          } else {
            style.textAlign = TextAlign::Left;
          }
          style.hasTextAlign = true;
        } else if (propName == "font-style") {
          String v = propValue;
          v.toLowerCase();
          v.trim();

          if (v == "italic" || v == "oblique") {
            style.fontStyle = CssFontStyle::Italic;
          } else {
            style.fontStyle = CssFontStyle::Normal;
          }
          style.hasFontStyle = true;
        } else if (propName == "font-weight") {
          String v = propValue;
          v.toLowerCase();
          v.trim();

          if (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900") {
            style.fontWeight = CssFontWeight::Bold;
          } else {
            style.fontWeight = CssFontWeight::Normal;
          }
          style.hasFontWeight = true;
        } else if (propName == "text-indent") {
          String v = propValue;
          v.trim();
          v.toLowerCase();

          float factor = 1.0f;
          if (v.length() >= 2 && v.substring(v.length() - 2) == String("em")) {
            factor = 16.0f;
            v = v.substring(0, v.length() - 2);
          } else if (v.length() >= 2 && v.substring(v.length() - 2) == String("px")) {
            v = v.substring(0, v.length() - 2);
          }
          v.trim();
          float indentVal = 0.0f;
          if (v.length() > 0) {
            indentVal = static_cast<float>(atof(v.c_str())) * factor;
          }
          style.textIndent = indentVal;
          style.hasTextIndent = (indentVal > 0.0f);
        }
      }
    }

    propStart = propEnd + 1;
  }

  return style;
}
