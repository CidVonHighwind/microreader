#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "text/hyphenation/EnglishHyphenation.h"
#include "text/hyphenation/GermanHyphenation.h"

struct TestCase {
  std::string word;
  std::string hyphenated;
  std::vector<size_t> expectedPositions;
  int frequency;
};

struct EvaluationResult {
  int truePositives = 0;
  int falsePositives = 0;
  int falseNegatives = 0;
  double precision = 0.0;
  double recall = 0.0;
  double f1Score = 0.0;
  double weightedScore = 0.0;
};

std::vector<TestCase> loadTestData(const std::string& filename) {
  std::vector<TestCase> testCases;
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << filename << std::endl;
    return testCases;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream iss(line);
    std::string word, hyphenated, freqStr;

    if (std::getline(iss, word, '|') && std::getline(iss, hyphenated, '|') && std::getline(iss, freqStr, '|')) {
      TestCase testCase;
      testCase.word = word;
      testCase.hyphenated = hyphenated;
      testCase.frequency = std::stoi(freqStr);

      size_t charPos = 0;
      for (size_t i = 0; i < hyphenated.length(); i++) {
        if (hyphenated[i] == '=') {
          testCase.expectedPositions.push_back(charPos);
        } else {
          charPos++;
        }
      }

      testCases.push_back(testCase);
    }
  }

  file.close();
  return testCases;
}

std::string positionsToHyphenated(const std::string& word, const std::vector<size_t>& positions) {
  std::string result;
  for (size_t i = 0; i < word.length(); i++) {
    if (std::find(positions.begin(), positions.end(), i) != positions.end()) {
      result += '=';
    }
    result += word[i];
  }
  return result;
}

EvaluationResult evaluateWord(const TestCase& testCase,
                              std::function<std::vector<size_t>(const std::string&)> hyphenateFunc) {
  EvaluationResult result;

  std::vector<size_t> actualPositions = hyphenateFunc(testCase.word);

  std::vector<size_t> expected = testCase.expectedPositions;
  std::vector<size_t> actual = actualPositions;

  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());

  for (size_t pos : actual) {
    if (std::find(expected.begin(), expected.end(), pos) != expected.end()) {
      result.truePositives++;
    } else {
      result.falsePositives++;
    }
  }

  for (size_t pos : expected) {
    if (std::find(actual.begin(), actual.end(), pos) == actual.end()) {
      result.falseNegatives++;
    }
  }

  if (result.truePositives + result.falsePositives > 0) {
    result.precision = static_cast<double>(result.truePositives) / (result.truePositives + result.falsePositives);
  }

  if (result.truePositives + result.falseNegatives > 0) {
    result.recall = static_cast<double>(result.truePositives) / (result.truePositives + result.falseNegatives);
  }

  if (result.precision + result.recall > 0) {
    result.f1Score = 2 * result.precision * result.recall / (result.precision + result.recall);
  }

  double fpPenalty = 2.0;
  double fnPenalty = 1.0;

  int totalErrors = result.falsePositives * fpPenalty + result.falseNegatives * fnPenalty;
  int totalPossible = expected.size() * fpPenalty;

  if (totalPossible > 0) {
    result.weightedScore = 1.0 - (static_cast<double>(totalErrors) / totalPossible);
    result.weightedScore = std::max(0.0, result.weightedScore);
  } else if (result.falsePositives == 0) {
    result.weightedScore = 1.0;
  }

  return result;
}

void printResults(const std::string& language, const std::vector<TestCase>& testCases,
                  const std::vector<std::pair<TestCase, EvaluationResult>>& worstCases, int perfectMatches,
                  int partialMatches, int completeMisses, double totalPrecision, double totalRecall, double totalF1,
                  double totalWeighted, int totalTP, int totalFP, int totalFN,
                  std::function<std::vector<size_t>(const std::string&)> hyphenateFunc) {
  std::string lang_upper = language;
  if (!lang_upper.empty()) {
    lang_upper[0] = std::toupper(lang_upper[0]);
  }

  std::cout << "================================================================================" << std::endl;
  std::cout << lang_upper << " HYPHENATION EVALUATION RESULTS" << std::endl;
  std::cout << "================================================================================" << std::endl;
  std::cout << std::endl;

  std::cout << "Total test cases:   " << testCases.size() << std::endl;
  std::cout << "Perfect matches:    " << perfectMatches << " (" << (perfectMatches * 100.0 / testCases.size()) << "%)"
            << std::endl;
  std::cout << "Partial matches:    " << partialMatches << std::endl;
  std::cout << "Complete misses:    " << completeMisses << std::endl;
  std::cout << std::endl;

  std::cout << "--- Overall Metrics (averaged per word) ---" << std::endl;
  std::cout << "Average Precision:       " << (totalPrecision / testCases.size() * 100.0) << "%" << std::endl;
  std::cout << "Average Recall:          " << (totalRecall / testCases.size() * 100.0) << "%" << std::endl;
  std::cout << "Average F1 Score:        " << (totalF1 / testCases.size() * 100.0) << "%" << std::endl;
  std::cout << "Average Weighted Score:  " << (totalWeighted / testCases.size() * 100.0) << "% (FP penalty: 2x)"
            << std::endl;
  std::cout << std::endl;

  std::cout << "--- Overall Metrics (total counts) ---" << std::endl;
  std::cout << "True Positives:          " << totalTP << std::endl;
  std::cout << "False Positives:         " << totalFP << " (incorrect hyphenation points)" << std::endl;
  std::cout << "False Negatives:         " << totalFN << " (missed hyphenation points)" << std::endl;

  double overallPrecision = totalTP + totalFP > 0 ? static_cast<double>(totalTP) / (totalTP + totalFP) : 0.0;
  double overallRecall = totalTP + totalFN > 0 ? static_cast<double>(totalTP) / (totalTP + totalFN) : 0.0;
  double overallF1 = overallPrecision + overallRecall > 0
                         ? 2 * overallPrecision * overallRecall / (overallPrecision + overallRecall)
                         : 0.0;

  std::cout << "Overall Precision:       " << (overallPrecision * 100.0) << "%" << std::endl;
  std::cout << "Overall Recall:          " << (overallRecall * 100.0) << "%" << std::endl;
  std::cout << "Overall F1 Score:        " << (overallF1 * 100.0) << "%" << std::endl;
  std::cout << std::endl;

  std::cout << "--- Worst Cases (lowest weighted scores) ---" << std::endl;
  int showCount = std::min(10, static_cast<int>(worstCases.size()));
  for (int i = 0; i < showCount; i++) {
    const auto& testCase = worstCases[i].first;
    const auto& result = worstCases[i].second;

    std::vector<size_t> actualPositions = hyphenateFunc(testCase.word);
    std::string actualHyphenated = positionsToHyphenated(testCase.word, actualPositions);

    std::cout << "Word: " << testCase.word << " (freq: " << testCase.frequency << ")" << std::endl;
    std::cout << "  Expected:  " << testCase.hyphenated << std::endl;
    std::cout << "  Got:       " << actualHyphenated << std::endl;
    std::cout << "  Precision: " << (result.precision * 100.0) << "%"
              << "  Recall: " << (result.recall * 100.0) << "%"
              << "  F1: " << (result.f1Score * 100.0) << "%"
              << "  Weighted: " << (result.weightedScore * 100.0) << "%" << std::endl;
    std::cout << "  TP: " << result.truePositives << "  FP: " << result.falsePositives
              << "  FN: " << result.falseNegatives << std::endl;
    std::cout << std::endl;
  }

  // Additional compact list of the worst ~100 words to aid iteration
  int compactCount = std::min(100, static_cast<int>(worstCases.size()));
  if (compactCount > 0) {
    std::cout << "--- Compact Worst Cases (" << compactCount << ") ---" << std::endl;
    for (int i = 0; i < compactCount; i++) {
      const auto& testCase = worstCases[i].first;
      std::vector<size_t> actualPositions = hyphenateFunc(testCase.word);
      std::string actualHyphenated = positionsToHyphenated(testCase.word, actualPositions);
      std::cout << testCase.word << " | exp:" << testCase.hyphenated << " | got:" << actualHyphenated << std::endl;
    }
    std::cout << std::endl;
  }
}

int main(int argc, char* argv[]) {
  std::string language = "english";  // default

  if (argc > 1) {
    language = argv[1];
  }

  std::vector<std::string> languages;
  if (language == "all") {
    languages = {"english", "german"};
  } else {
    languages = {language};
  }

  for (const auto& lang : languages) {
    std::string testDataFile;
    std::function<std::vector<size_t>(const std::string&)> hyphenateFunc;

    if (lang == "english") {
      testDataFile = "test/resources/english_hyphenation_tests.txt";
      hyphenateFunc = EnglishHyphenation::hyphenate;
    } else if (lang == "german") {
      testDataFile = "test/resources/german_hyphenation_tests.txt";
      hyphenateFunc = GermanHyphenation::hyphenate;
    } else {
      std::cerr << "Unknown language: " << lang << std::endl;
      continue;
    }

    std::cout << "Loading test data from: " << testDataFile << std::endl;
    std::vector<TestCase> testCases = loadTestData(testDataFile);

    if (testCases.empty()) {
      std::cerr << "No test cases loaded for " << lang << ". Skipping." << std::endl;
      continue;
    }

    std::cout << "Loaded " << testCases.size() << " test cases for " << lang << std::endl;
    std::cout << std::endl;

    int perfectMatches = 0;
    int partialMatches = 0;
    int completeMisses = 0;

    double totalPrecision = 0.0;
    double totalRecall = 0.0;
    double totalF1 = 0.0;
    double totalWeighted = 0.0;

    int totalTP = 0, totalFP = 0, totalFN = 0;

    std::vector<std::pair<TestCase, EvaluationResult>> worstCases;

    for (const auto& testCase : testCases) {
      EvaluationResult result = evaluateWord(testCase, hyphenateFunc);

      totalTP += result.truePositives;
      totalFP += result.falsePositives;
      totalFN += result.falseNegatives;

      totalPrecision += result.precision;
      totalRecall += result.recall;
      totalF1 += result.f1Score;
      totalWeighted += result.weightedScore;

      if (result.f1Score == 1.0) {
        perfectMatches++;
      } else if (result.f1Score > 0.0) {
        partialMatches++;
      } else {
        completeMisses++;
      }

      worstCases.push_back({testCase, result});
    }

    std::sort(worstCases.begin(), worstCases.end(),
              [](const auto& a, const auto& b) { return a.second.weightedScore < b.second.weightedScore; });

    printResults(lang, testCases, worstCases, perfectMatches, partialMatches, completeMisses, totalPrecision,
                 totalRecall, totalF1, totalWeighted, totalTP, totalFP, totalFN, hyphenateFunc);
  }

  return 0;
}
