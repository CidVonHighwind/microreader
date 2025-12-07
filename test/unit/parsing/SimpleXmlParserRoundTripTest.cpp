#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "content/xml/SimpleXmlParser.h"
#include "test_utils.h"

namespace {

const char* kFixturePath = "data/books/5F7754037AF147879447BB32918DD7A6.xhtml";

struct NodeInfo {
  SimpleXmlParser::NodeType type;
  String name;
  bool isEmpty;
  size_t filePos;
  size_t textStart;
  size_t textEnd;
};

std::vector<NodeInfo> collectForwardNodes(SimpleXmlParser& parser) {
  std::vector<NodeInfo> nodes;

  while (parser.read()) {
    NodeInfo info;
    info.type = parser.getNodeType();
    info.name = parser.getName();
    info.isEmpty = parser.isEmptyElement();
    info.filePos = parser.getFilePosition();
    info.textStart = 0;
    info.textEnd = 0;

    if (info.type == SimpleXmlParser::Text) {
      info.textStart = info.filePos;
      // Consume text to find end
      while (parser.hasMoreTextChars()) {
        parser.readTextNodeCharForward();
      }
      info.textEnd = parser.getFilePosition();
    }

    nodes.push_back(info);
  }

  return nodes;
}

std::vector<NodeInfo> collectBackwardNodes(SimpleXmlParser& parser) {
  std::vector<NodeInfo> nodes;

  parser.seekToFilePosition(parser.getFileSize());

  while (parser.readBackward()) {
    NodeInfo info;
    info.type = parser.getNodeType();
    info.name = parser.getName();
    info.isEmpty = parser.isEmptyElement();
    info.filePos = parser.getFilePosition();
    info.textStart = 0;
    info.textEnd = 0;

    if (info.type == SimpleXmlParser::Text) {
      info.textEnd = parser.getFilePosition();
      // Consume text backward to find start
      while (parser.hasMoreTextCharsBackward()) {
        parser.readPrevTextNodeChar();
      }
      info.textStart = parser.getFilePosition();
    }

    nodes.push_back(info);
  }

  return nodes;
}

void testForwardBackwardSymmetry(TestUtils::TestRunner& runner) {
  SimpleXmlParser parser;
  runner.expectTrue(parser.open(kFixturePath), "open fixture");

  auto forward = collectForwardNodes(parser);
  parser.close();

  runner.expectTrue(parser.open(kFixturePath), "reopen for backward");
  auto backward = collectBackwardNodes(parser);
  parser.close();

  runner.expectTrue(!forward.empty(), "forward nodes collected");
  runner.expectTrue(!backward.empty(), "backward nodes collected");

  std::reverse(backward.begin(), backward.end());

  runner.expectTrue(forward.size() == backward.size(), "node counts match");
  std::cout << "Forward: " << forward.size() << " nodes, Backward: " << backward.size() << " nodes\n";

  size_t mismatches = 0;
  for (size_t i = 0; i < forward.size() && i < backward.size(); i++) {
    const auto& f = forward[i];
    const auto& b = backward[i];

    bool match = (f.type == b.type && f.name == b.name && f.isEmpty == b.isEmpty);
    if (!match && mismatches < 10) {
      std::cout << "Mismatch at " << i << ": fwd(" << f.type << "," << f.name.c_str() << ") vs bwd(" << b.type << ","
                << b.name.c_str() << ")\n";
      mismatches++;
    }

    runner.expectTrue(match, "node " + std::to_string(i) + " matches");
  }
}

void testSeekToTextNode(TestUtils::TestRunner& runner) {
  SimpleXmlParser parser;
  runner.expectTrue(parser.open(kFixturePath), "open fixture for text seek");

  // Find first text node
  while (parser.read()) {
    if (parser.getNodeType() == SimpleXmlParser::Text) {
      size_t textStart = parser.getFilePosition();

      // Read text content
      String originalText;
      while (parser.hasMoreTextChars()) {
        originalText += parser.readTextNodeCharForward();
      }

      if (originalText.length() < 5)
        continue;

      // Seek back to start and re-read
      parser.seekToFilePosition(textStart);

      runner.expectTrue(parser.getNodeType() == SimpleXmlParser::Text, "text node after seek");

      String rebuiltText;
      while (parser.hasMoreTextChars()) {
        rebuiltText += parser.readTextNodeCharForward();
      }

      runner.expectTrue(originalText == rebuiltText, "text content matches after seek");
      break;
    }
  }

  parser.close();
}

void testSeekToMiddleOfText(TestUtils::TestRunner& runner) {
  SimpleXmlParser parser;
  runner.expectTrue(parser.open(kFixturePath), "open fixture for mid-text seek");

  auto fileContent = TestUtils::readFile(kFixturePath);
  runner.expectTrue(!fileContent.empty(), "fixture loaded");

  // Find a text node
  while (parser.read()) {
    if (parser.getNodeType() == SimpleXmlParser::Text) {
      size_t start = parser.getFilePosition();

      String fullText;
      while (parser.hasMoreTextChars()) {
        fullText += parser.readTextNodeCharForward();
      }
      size_t end = parser.getFilePosition();

      if (fullText.length() < 10)
        continue;

      // Seek to middle
      size_t mid = start + fullText.length() / 2;
      parser.seekToFilePosition(mid);

      // Read remaining text
      String remaining;
      while (parser.hasMoreTextChars()) {
        remaining += parser.readTextNodeCharForward();
      }

      // Should match the second half of the original text
      String expected = fullText.substring(fullText.length() / 2);
      runner.expectTrue(remaining == expected, "mid-text seek gives correct remainder");
      break;
    }
  }

  parser.close();
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("SimpleXmlParser Round Trip");
  testForwardBackwardSymmetry(runner);
  testSeekToTextNode(runner);
  testSeekToMiddleOfText(runner);
  return runner.allPassed() ? 0 : 1;
}
