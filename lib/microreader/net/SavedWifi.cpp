#include "SavedWifi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace microreader {

void SavedWifi::load() {
  networks_.clear();
  if (file_.empty())
    return;
  FILE* f = std::fopen(file_.c_str(), "r");
  if (!f)
    return;
  char line[256];
  while (std::fgets(line, sizeof(line), f) && networks_.size() < kMax) {
    char* nl = std::strchr(line, '\n');
    if (nl)
      *nl = '\0';
    if (char* cr = std::strchr(line, '\r'))
      *cr = '\0';
    if (line[0] == '#' || line[0] == '\0')
      continue;
    char* tab = std::strchr(line, '\t');
    Network n;
    if (tab) {
      *tab = '\0';
      n.password = tab + 1;
    }
    n.ssid = line;
    if (n.ssid.empty() || n.ssid.size() > 32 || n.password.size() > 64)
      continue;
    networks_.push_back(std::move(n));
  }
  std::fclose(f);
}

bool SavedWifi::save() const {
  if (file_.empty())
    return false;
  FILE* f = std::fopen(file_.c_str(), "w");
  if (!f)
    return false;
  std::fputs("# Sami Reader Wi-Fi networks: <name>\t<password>\n", f);
  for (const auto& n : networks_)
    std::fprintf(f, "%s\t%s\n", n.ssid.c_str(), n.password.c_str());
  std::fclose(f);
  return true;
}

bool SavedWifi::find(const std::string& ssid, std::string& password) const {
  for (const auto& n : networks_) {
    if (n.ssid == ssid) {
      password = n.password;
      return true;
    }
  }
  return false;
}

void SavedWifi::remember(const std::string& ssid, const std::string& password) {
  if (ssid.empty() || ssid.find_first_of("\t\r\n") != std::string::npos ||
      password.find_first_of("\t\r\n") != std::string::npos)
    return;
  networks_.erase(std::remove_if(networks_.begin(), networks_.end(), [&](const Network& n) { return n.ssid == ssid; }),
                  networks_.end());
  networks_.insert(networks_.begin(), Network{ssid, password});
  if (networks_.size() > kMax)
    networks_.resize(kMax);
  save();
}

void SavedWifi::forget(const std::string& ssid) {
  const size_t before = networks_.size();
  networks_.erase(std::remove_if(networks_.begin(), networks_.end(), [&](const Network& n) { return n.ssid == ssid; }),
                  networks_.end());
  if (networks_.size() != before)
    save();
}

}  // namespace microreader
