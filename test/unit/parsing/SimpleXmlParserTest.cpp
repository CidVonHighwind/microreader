#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "content/xml/SimpleXmlParser.h"
#include "test_config.h"
#include "test_utils.h"

struct NodeSnapshot {
  SimpleXmlParser::NodeType type;
  String name;
  bool isEmpty = false;
  String text;
  size_t filePosStart;
  size_t filePosEnd;
};

String readTextForward(SimpleXmlParser& parser) {
  String text = "";
  while (parser.hasMoreTextChars()) {
    text += parser.readTextNodeCharForward();
  }
  return text;
}

String readTextBackward(SimpleXmlParser& parser) {
  String text = "";
  while (parser.hasMoreTextCharsBackward()) {
    text = String(parser.readPrevTextNodeChar()) + text;
  }
  return text;
}

std::vector<NodeSnapshot> readForwardNodes(TestUtils::TestRunner& runner, const char* path) {
  SimpleXmlParser parser;
  std::vector<NodeSnapshot> result;

  runner.expectTrue(parser.open(path), "Open XHTML for forward pass", "", true);
  while (parser.read()) {
    NodeSnapshot snap;
    snap.type = parser.getNodeType();
    snap.name = parser.getName();
    snap.isEmpty = parser.isEmptyElement();
    snap.filePosStart = parser.getElementStartPos();
    snap.filePosEnd = parser.getElementEndPos();

    if (snap.type == SimpleXmlParser::Text) {
      snap.text = readTextForward(parser);
    }

    result.push_back(snap);
  }
  parser.close();
  return result;
}

std::vector<NodeSnapshot> readBackwardNodes(TestUtils::TestRunner& runner, const char* path) {
  SimpleXmlParser parser;
  std::vector<NodeSnapshot> result;

  runner.expectTrue(parser.open(path), "Open XHTML for backward pass", "", true);
  parser.seekToFilePosition(parser.getFileSize());

  while (parser.readBackward()) {
    NodeSnapshot snap;
    snap.type = parser.getNodeType();
    snap.name = parser.getName();
    snap.isEmpty = parser.isEmptyElement();
    snap.filePosStart = parser.getElementStartPos();
    snap.filePosEnd = parser.getElementEndPos();

    if (snap.type == SimpleXmlParser::Text) {
      snap.text = readTextBackward(parser);
    }

    // Skip duplicates (shouldn't happen but being safe)
    if (!result.empty()) {
      const auto& last = result.back();
      if (last.type == snap.type && last.name == snap.name && last.isEmpty == snap.isEmpty &&
          last.filePosStart == snap.filePosStart && last.filePosEnd == snap.filePosEnd) {
        continue;
      }
    }

    result.push_back(snap);
  }
  parser.close();
  return result;
}

void testForwardBackwardSymmetry(TestUtils::TestRunner& runner, const char* path) {
  auto forward = readForwardNodes(runner, path);
  auto backward = readBackwardNodes(runner, path);

  runner.expectTrue(!forward.empty(), "Forward pass captured nodes", "", true);
  runner.expectTrue(!backward.empty(), "Backward pass captured nodes", "", true);

  std::reverse(backward.begin(), backward.end());

  runner.expectTrue(forward.size() == backward.size(), "Node counts match", "", true);
  std::cout << "Forward nodes: " << forward.size() << ", backward nodes: " << backward.size() << "\n";

  int positionMismatches = 0;
  const int MAX_POSITION_REPORTS = 10;

  for (size_t i = 0; i < forward.size() && i < backward.size(); i++) {
    const auto& f = forward[i];
    const auto& b = backward[i];

    if (f.type != b.type || f.name != b.name || f.isEmpty != b.isEmpty) {
      std::cout << "Mismatch at node " << i << ": forward type=" << f.type << " name=" << f.name.c_str()
                << " empty=" << f.isEmpty << ", backward type=" << b.type << " name=" << b.name.c_str()
                << " empty=" << b.isEmpty << "\n";
    }

    runner.expectTrue(f.type == b.type, "Node " + std::to_string(i) + " type matches", "", true);
    runner.expectTrue(f.name == b.name, "Node " + std::to_string(i) + " name matches", "", true);
    runner.expectTrue(f.isEmpty == b.isEmpty, "Node " + std::to_string(i) + " isEmpty matches", "", true);

    if (f.type == SimpleXmlParser::Text) {
      runner.expectTrue(f.text == b.text, "Node " + std::to_string(i) + " text matches", "", true);
    }

    // Check position consistency
    if (f.filePosStart != b.filePosStart) {
      if (positionMismatches < MAX_POSITION_REPORTS) {
        std::cout << "*** Position START mismatch at node " << i << " (type=" << f.type << " name=" << f.name.c_str()
                  << "): forward=" << f.filePosStart << " backward=" << b.filePosStart << "\n";
      }
      positionMismatches++;
    }
    if (f.filePosEnd != b.filePosEnd) {
      if (positionMismatches < MAX_POSITION_REPORTS) {
        std::cout << "*** Position END mismatch at node " << i << " (type=" << f.type << " name=" << f.name.c_str()
                  << "): forward=" << f.filePosEnd << " backward=" << b.filePosEnd << "\n";
      }
      positionMismatches++;
    }
  }

  if (positionMismatches > 0) {
    std::cout << "Total position mismatches: " << positionMismatches << " out of " << forward.size() << " nodes\n";
  }

  runner.expectTrue(positionMismatches == 0, "All node positions match between forward and backward reading");
}

void testMidTextNavigation(TestUtils::TestRunner& runner, const char* path) {
  SimpleXmlParser parser;
  runner.expectTrue(parser.open(path), "Open XHTML for mid-text test", "", true);

  int textNodesTested = 0;
  while (parser.read()) {
    if (parser.getNodeType() != SimpleXmlParser::Text)
      continue;

    size_t startPos = parser.getFilePosition();
    String fullText = readTextForward(parser);
    if (fullText.length() < 2)
      continue;

    // Calculate a mid-point offset (in characters)
    size_t offset = fullText.length() / 2;

    // Reset to start and read partway to get the file position at the midpoint
    parser.seekToFilePosition(startPos);
    runner.expectTrue(parser.getNodeType() == SimpleXmlParser::Text, "Text node restored after seek to start", "",
                      true);

    for (size_t i = 0; i < offset && parser.hasMoreTextChars(); i++) {
      parser.readTextNodeCharForward();
    }

    // Save the file position at the midpoint
    size_t midPos = parser.getFilePosition();

    // Now seek to midPos - this should restore us to the exact same state
    parser.seekToFilePosition(midPos);
    runner.expectTrue(parser.getNodeType() == SimpleXmlParser::Text, "Text node restored after seek to mid", "", true);
    String forwardPart = readTextForward(parser);

    // Seek to midPos again and read backward to get the first half
    parser.seekToFilePosition(midPos);
    runner.expectTrue(parser.getNodeType() == SimpleXmlParser::Text, "Text node restored after second seek to mid", "",
                      true);
    String backwardPart = readTextBackward(parser);

    // Combine backward + forward should equal the full text
    String combined = backwardPart + forwardPart;
    runner.expectTrue(combined == fullText,
                      "Combined text matches full text for node " + std::to_string(textNodesTested), "", true);

    if (combined != fullText) {
      std::cout << "Full text length: " << fullText.length() << ", combined length: " << combined.length() << "\n";
      std::cout << "Backward part length: " << backwardPart.length()
                << ", forward part length: " << forwardPart.length() << "\n";
      std::cout << "Full text: \"" << fullText.c_str() << "\"\n";
      std::cout << "Backward part: \"" << backwardPart.c_str() << "\"\n";
      std::cout << "Forward part: \"" << forwardPart.c_str() << "\"\n";
      std::cout << "Combined: \"" << combined.c_str() << "\"\n";
    }

    textNodesTested++;
  }

  runner.expectTrue(textNodesTested > 0, "At least one text node was tested", "", true);
  std::cout << "Tested " << textNodesTested << " text nodes for mid-text navigation\n";

  parser.close();
}

int main() {
  TestUtils::TestRunner runner("SimpleXmlParser Position Test");
  const char* xhtmlPath = "data/books/5F7754037AF147879447BB32918DD7A6.xhtml";

  readForwardNodes(runner, xhtmlPath);
  testForwardBackwardSymmetry(runner, xhtmlPath);
  testMidTextNavigation(runner, xhtmlPath);

  return runner.allPassed() ? 0 : 1;
}
