/**
 * test_globals.h - Global Test Configuration
 *
 * This file provides global instances of WordProvider and LayoutStrategy
 * that all tests use. To change which provider/layout is tested:
 *
 * 1. Comment/uncomment the desired provider section below
 * 2. Comment/uncomment the desired layout section below
 * 3. Rebuild and run tests
 *
 * All tests will automatically use the configured provider and layout.
 */

#pragma once

#include <iostream>
#include <string>

#include "WString.h"
#include "test_config.h"
#include "test_utils.h"

// ============================================================================
// Include all provider headers
// ============================================================================
#include "content/providers/EpubWordProvider.h"
#include "content/providers/FileWordProvider.h"
#include "content/providers/StringWordProvider.h"

// ============================================================================
// Include all layout headers
// ============================================================================
#include "text/layout/GreedyLayoutStrategy.h"
#include "text/layout/KnuthPlassLayoutStrategy.h"

namespace TestGlobals {

// ============================================================================
// CONFIGURATION: Choose your provider type
// Uncomment ONE of the following sections
// ============================================================================

// --- Option 1: StringWordProvider ---
// #define USE_STRING_PROVIDER
// inline const char* g_testFilePath = "data/books/bobiverse 1.txt";

// // --- Option 2: FileWordProvider ---
// #define USE_FILE_PROVIDER
// inline const char* g_testFilePath = "resources/books/test 0.txt";

// --- Option 3: EpubWordProvider ---
#define USE_EPUB_PROVIDER
// The test repository contains bobiverse one.epub; update the test path to match an existing file.
// inline const char* g_testFilePath = "resources/books/mabuse.epub";
inline const char* g_testFilePath = "resources/books/snow crash.epub";
// inline const char* g_testFilePath = "resources/books/mabuse.xhtml";

// XHTML test file path
inline const char* g_testXhtmlPath = "resources/books/mabuse.xhtml";
// inline const char* g_testXhtmlPath = "resources/books/bobiverse one.xhtml";

// ============================================================================
// CONFIGURATION: Choose your layout strategy
// Uncomment ONE of the following
// ============================================================================

// --- Option 1: GreedyLayoutStrategy ---
// #define USE_GREEDY_LAYOUT

// --- Option 2: KnuthPlassLayoutStrategy ---
#define USE_KNUTH_PLASS_LAYOUT

// ============================================================================
// Global storage (don't modify below this line)
// ============================================================================

// String content storage for StringWordProvider
inline std::string g_stringContent;
inline String g_arduinoString;

// Global provider instance
inline WordProvider* g_provider = nullptr;

// Global layout instance
inline LayoutStrategy* g_layout = nullptr;

// ============================================================================
// Provider name for test output
// ============================================================================
inline const char* getProviderName() {
#if defined(USE_STRING_PROVIDER)
  return "StringWordProvider";
#elif defined(USE_FILE_PROVIDER)
  return "FileWordProvider";
#elif defined(USE_EPUB_PROVIDER)
  return "EpubWordProvider";
#else
  return "Unknown";
#endif
}

// ============================================================================
// Layout name for test output
// ============================================================================
inline const char* getLayoutName() {
#if defined(USE_GREEDY_LAYOUT)
  return "GreedyLayoutStrategy";
#elif defined(USE_KNUTH_PLASS_LAYOUT)
  return "KnuthPlassLayoutStrategy";
#else
  return "Unknown";
#endif
}

// ============================================================================
// Initialize the global provider
// Call this at the start of main() before running tests
// Returns true on success, false on failure
// ============================================================================
inline bool initProvider() {
  std::cout << "Initializing provider: " << getProviderName() << "\n";
  std::cout << "Source file: " << g_testFilePath << "\n";

#if defined(USE_STRING_PROVIDER)
  // Load file content into string
  g_stringContent = TestUtils::readFile(g_testFilePath);
  if (g_stringContent.empty()) {
    std::cerr << "ERROR: Failed to read file: " << g_testFilePath << "\n";
    return false;
  }
  g_stringContent = TestUtils::normalizeLineEndings(g_stringContent);
  g_arduinoString = String(g_stringContent.c_str());
  g_provider = new StringWordProvider(g_arduinoString);
  return true;

#elif defined(USE_FILE_PROVIDER)
  g_provider = new FileWordProvider(g_testFilePath, TestConfig::FILE_BUFFER_SIZE);
  return true;

#elif defined(USE_EPUB_PROVIDER)
  auto* epub = new EpubWordProvider(g_testFilePath);
  if (!epub->isValid()) {
    std::cerr << "ERROR: Failed to open EPUB file: " << g_testFilePath << "\n";
    delete epub;
    return false;
  }

  epub->setChapter(6);

  g_provider = epub;
  return true;

#else
#error "No provider defined! Uncomment one of USE_STRING_PROVIDER, USE_FILE_PROVIDER, or USE_EPUB_PROVIDER"
#endif
}

// ============================================================================
// Initialize the global layout strategy
// Call this at the start of main() before running tests
// Returns true on success
// ============================================================================
inline bool initLayout() {
  std::cout << "Initializing layout: " << getLayoutName() << "\n";

#if defined(USE_GREEDY_LAYOUT)
  g_layout = new GreedyLayoutStrategy();
  return true;

#elif defined(USE_KNUTH_PLASS_LAYOUT)
  g_layout = new KnuthPlassLayoutStrategy();
  return true;

#else
#error "No layout defined! Uncomment one of USE_GREEDY_LAYOUT or USE_KNUTH_PLASS_LAYOUT"
#endif
}

// ============================================================================
// Initialize both provider and layout
// Call this at the start of main()
// ============================================================================
inline bool init() {
  std::cout << "\n========================================\n";
  std::cout << "Test Configuration\n";
  std::cout << "========================================\n";

  if (!initProvider()) {
    return false;
  }

  if (!initLayout()) {
    return false;
  }

  std::cout << "========================================\n\n";
  return true;
}

// ============================================================================
// Cleanup - call at end of main()
// ============================================================================
inline void cleanup() {
  delete g_provider;
  delete g_layout;
  g_provider = nullptr;
  g_layout = nullptr;
}

// ============================================================================
// Convenience accessors
// ============================================================================
inline WordProvider& provider() {
  return *g_provider;
}

inline LayoutStrategy& layout() {
  return *g_layout;
}

// Reset provider to position 0 (useful between tests)
inline void resetProvider() {
  if (g_provider) {
    g_provider->setPosition(0);
  }
}

}  // namespace TestGlobals
