#include "Settings.h"
#include <cstring>

#include <Arduino.h>

Settings::Settings(SDCardManager& sdManager) : sd(sdManager) {}

bool Settings::load() {
  if (!sd.ready())
    return false;

  char buf[2048];
  size_t r = sd.readFileToBuffer("/microreader/settings.cfg", buf, sizeof(buf));
  if (r == 0) {
    kv.clear();
    return false;
  }

  // Ensure null-termination
  buf[sizeof(buf) - 1] = '\0';
  parseSettingsBuffer(buf);
  return true;
}

bool Settings::save() {
  if (!sd.ready())
    return false;
  String out;
  for (const auto& p : kv) {
    out += p.first;
    out += "=";
    out += p.second;
    out += "\n";
  }
  return sd.writeFile("/microreader/settings.cfg", out);
}

bool Settings::getInt(const String& key, int& out) const {
  auto it = kv.find(key);
  if (it == kv.end())
    return false;
  out = atoi(it->second.c_str());
  return true;
}

void Settings::setInt(const String& key, int v) {
  kv[key] = String(v);
}

String Settings::getString(const String& key, const String& def) const {
  auto it = kv.find(key);
  if (it == kv.end())
    return def;
  return it->second;
}

void Settings::setString(const String& key, const String& value) {
  kv[key] = value;
}

void Settings::parseSettingsBuffer(const char* buf) {
  kv.clear();
  const char* p = buf;
  while (*p) {
    // Read line
    const char* eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    if (len > 0) {
      String line;
      line.reserve(len + 1);
      line = String(p).substring(0, (int)len);
      // Trim
      while (line.length() && (line.charAt(0) == ' ' || line.charAt(0) == '\t'))
        line = line.substring(1);
      while (line.length() && (line.charAt(line.length() - 1) == ' ' || line.charAt(line.length() - 1) == '\t' ||
                               line.charAt(line.length() - 1) == '\r'))
        line = line.substring(0, line.length() - 1);
      if (line.length() > 0 && line.charAt(0) != '#') {
        int eq = line.indexOf('=');
        if (eq > 0) {
          String key = line.substring(0, eq);
          String val = line.substring(eq + 1);
          kv[key] = val;
        }
      }
    }
    if (!eol)
      break;
    p = eol + 1;
  }
}
