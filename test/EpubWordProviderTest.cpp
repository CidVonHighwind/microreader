#include <iostream>
#include <string>

#include "../src/ui/screens/textview/EpubWordProvider.h"
#include "test_config.h"
#include "test_utils.h"

// Minimal TextRenderer stub for compatibility
class TextRenderer {};

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("EpubWordProvider Test");

  // Test 1: Constructor with non-existent file
  // This tests that the provider handles invalid files gracefully
  const char* invalidPath = "nonexistent.epub";
  EpubWordProvider invalidProvider(invalidPath);
  runner.expectTrue(!invalidProvider.isValid(), "Invalid EPUB path returns false for isValid()");

  // Test 2: Constructor with valid EPUB file
  const char* validPath = "data\\books\\Dr. Mabuse, der Spieler.epub";
  EpubWordProvider validProvider(validPath);
  runner.expectTrue(validProvider.isValid(), "Valid EPUB path returns true for isValid()");

  return runner.allPassed() ? 0 : 1;
}
