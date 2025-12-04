#include "EpubWordProvider.h"

#include <Arduino.h>

#include "EpubReader.h"

EpubWordProvider::EpubWordProvider(const char* path, size_t bufSize) : bufSize_(bufSize) {
  epubPath_ = String(path);
  index_ = 0;
  prevIndex_ = 0;
  valid_ = false;

  Serial.printf("\n=== EpubWordProvider: Initializing with %s ===\n", path);

  // Check if file exists
  File testFile = SD.open(path);
  if (!testFile) {
    Serial.println("ERROR: Cannot open EPUB file");
    return;
  }
  testFile.close();

  Serial.println("EPUB file opened successfully");
  valid_ = true;
}

EpubWordProvider::~EpubWordProvider() {
  Serial.println("EpubWordProvider destroyed");
}

bool EpubWordProvider::hasNextWord() {
  return false;
}

String EpubWordProvider::getNextWord() {
  return String("epub");
}

String EpubWordProvider::getPrevWord() {
  return String("epub");
}

float EpubWordProvider::getPercentage() {
  return 0.0f;
}

float EpubWordProvider::getPercentage(int index) {
  return 0.0f;
}

int EpubWordProvider::getCurrentIndex() {
  return 0;
}

char EpubWordProvider::peekChar(int offset) {
  return '\0';
}

bool EpubWordProvider::isInsideWord() {
  return false;
}

void EpubWordProvider::ungetWord() {}

void EpubWordProvider::setPosition(int index) {}

void EpubWordProvider::reset() {}