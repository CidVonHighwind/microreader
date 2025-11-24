#include <fstream>
#include <iostream>
#include <string>

#include "../src/screens/text view/StringWordProvider.h"

// Provide a minimal TextRenderer type only so we can call the real
// StringWordProvider::getNextWord signature. The implementation of
// StringWordProvider no longer depends on the renderer, so this type
// can be empty for host tests.
class TextRenderer {};

static std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open())
    return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  std::string path;
  if (argc >= 2)
    path = argv[1];
  else
    path = "data/chapter one.txt";

  std::string content = readFile(path);
  if (content.empty()) {
    std::cerr << "Failed to open '" << path << "'\n";
    return 2;
  }

  // Convert std::string to Arduino-like String via implicit constructor
  String s(content.c_str());
  StringWordProvider provider(s);
  TextRenderer renderer;

  std::string rebuilt;
  while (provider.hasNextWord()) {
    std::string w = provider.getNextWord(renderer).c_str();
    if (w.length() == 0)
      break;
    // Append token exactly as returned. w may contain whitespace tokens
    rebuilt += w;
  }
  if (rebuilt == content) {
    std::cout << "TEST PASS (forward): reconstructed text equals original\n";
  } else {
    std::cerr << "TEST FAIL (forward): reconstructed text differs from original\n";
    size_t i = 0;
    size_t n = std::min(rebuilt.size(), content.size());
    while (i < n && rebuilt[i] == content[i])
      ++i;
    std::cerr << "First difference at index " << i << "\n";
    if (i < n) {
      std::cerr << "original byte: 0x" << std::hex << (unsigned int)(unsigned char)content[i] << std::dec
                << " rebuilt byte: 0x" << std::hex << (unsigned int)(unsigned char)rebuilt[i] << std::dec << "\n";
    } else {
      std::cerr << "Lengths differ: original=" << content.size() << " rebuilt=" << rebuilt.size() << "\n";
    }
    return 3;
  }

  // Now test backward reconstruction using getPrevWord starting from end
  provider.setPosition(static_cast<int>(content.length()));
  std::string rebuiltBack;
  while (true) {
    std::string w = provider.getPrevWord(renderer).c_str();
    if (w.length() == 0)
      break;
    // Prepend token since getPrevWord returns tokens in reverse order
    rebuiltBack.insert(0, w);
  }

  if (rebuiltBack == content) {
    std::cout << "TEST PASS (backward): reconstructed text equals original\n";
    return 0;
  } else {
    std::cerr << "TEST FAIL (backward): reconstructed text differs from original\n";
    size_t i = 0;
    size_t n = std::min(rebuiltBack.size(), content.size());
    while (i < n && rebuiltBack[i] == content[i])
      ++i;
    std::cerr << "First difference at index " << i << "\n";
    if (i < n) {
      std::cerr << "original byte: 0x" << std::hex << (unsigned int)(unsigned char)content[i] << std::dec
                << " rebuilt byte: 0x" << std::hex << (unsigned int)(unsigned char)rebuiltBack[i] << std::dec << "\n";
    } else {
      std::cerr << "Lengths differ: original=" << content.size() << " rebuiltBack=" << rebuiltBack.size() << "\n";
    }
    return 4;
  }
}
