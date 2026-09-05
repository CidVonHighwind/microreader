#include "EpubParser.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "../HeapLog.h"
#include "HtmlEntities.h"
#include "ImageDecoder.h"
#include "XmlReader.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

namespace microreader {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string sv_to_string(XmlStringView sv) {
  if (!sv.data || sv.length == 0)
    return {};
  return std::string(sv.data, sv.length);
}

static bool sv_eq(XmlStringView sv, const char* s) {
  return sv == s;
}

// Extract just the file data for a given zip entry into a vector.
static EpubError extract_entry(IZipFile& file, const ZipReader& zip, const ZipEntry& entry, std::vector<uint8_t>& out) {
  if (zip.extract(file, entry, out) != ZipError::Ok) {
    return EpubError::ZipError;
  }
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// CssCache
// ---------------------------------------------------------------------------

bool CssCache::low_memory() {
#ifdef ESP_PLATFORM
  return esp_get_free_heap_size() < 24 * 1024;
#else
  return false;
#endif
}

size_t CssCache::find_evict_slot(uint32_t protect_gen) const {
  size_t best = kMaxEntries;
  for (size_t i = 0; i < count_; ++i) {
    bool candidate = (protect_gen == 0) || (entries_[i].last_used_gen <= protect_gen);
    if (candidate) {
      if (best == kMaxEntries || entries_[i].last_used_gen < entries_[best].last_used_gen)
        best = i;
    }
  }
  return best;
}

const CssStylesheet* CssCache::get_or_load(IZipFile& file, const ZipReader& zip, const std::string& path,
                                            const CssConfig& config, uint32_t protect_gen,
                                            uint8_t* work_buf, size_t work_buf_size) {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].path == path) {
      entries_[i].last_used_gen = ++gen_;
      return &entries_[i].sheet;
    }
  }

  // Cache miss requires extraction. Without a work buffer we cannot extract —
  // return nullptr rather than falling back to the allocating overload (which
  // heap-allocates ~44KB and aborts on a fragmented ESP32 heap).
  if (!work_buf || work_buf_size == 0)
    return nullptr;

  const ZipEntry* ze = zip.find(path);
  if (!ze)
    return nullptr;

  std::vector<uint8_t> css_data;
  if (zip.extract(file, *ze, css_data, work_buf, work_buf_size) != ZipError::Ok)
    return nullptr;

  bool over_budget = total_bytes_ + css_data.size() > kMaxCacheBytes;
  bool need_evict = over_budget || low_memory();

  size_t slot;
  if (!need_evict && count_ < kMaxEntries) {
    slot = count_++;
  } else {
    slot = find_evict_slot(protect_gen);
    if (slot == kMaxEntries) {
      // All entries are from the current chapter — grow array if possible.
      if (count_ < kMaxEntries)
        slot = count_++;
      else
        return nullptr;  // array full, all entries protected
    } else {
      total_bytes_ -= entries_[slot].bytes;
    }
  }

  entries_[slot].path = path;
  entries_[slot].sheet = CssStylesheet(config);
  entries_[slot].sheet.extend_from_mut_sheet(reinterpret_cast<char*>(css_data.data()), css_data.size());
  entries_[slot].bytes = css_data.size();
  entries_[slot].last_used_gen = ++gen_;
  total_bytes_ += css_data.size();
  return &entries_[slot].sheet;
}

void CssCache::clear() {
  for (size_t i = 0; i < count_; ++i)
    entries_[i] = Entry{};
  count_ = 0;
  total_bytes_ = 0;
  gen_ = 0;
}

// ---------------------------------------------------------------------------
// CssContext — passed into parse_xhtml_events for on-demand CSS loading.
// ---------------------------------------------------------------------------

struct CssContext {
  IZipFile* file;
  const ZipReader* zip;
  CssCache* cache;
  CssConfig config;
  const std::string* base_dir;
  uint32_t chapter_start_gen;  // gen snapshot before this chapter's CSS loads — protects those entries
  uint8_t* work_buf;           // caller's decompression buffer (from framebuffer); nullptr during streaming
  size_t work_buf_size;
};

// ---------------------------------------------------------------------------
// CssSheets — lightweight wrapper over up to N stylesheet pointers.
// Avoids copying rules when multiple CSS files apply to one chapter.
// ---------------------------------------------------------------------------

struct CssSheets {
  static constexpr size_t kMax = CssCache::kMaxEntries + 1;  // cached + one extern stylesheet
  const CssStylesheet* ptrs[kMax];
  size_t n = 0;

  void add(const CssStylesheet* s) {
    if (s && n < kMax)
      ptrs[n++] = s;
  }

  bool empty() const { return n == 0; }

  const CssConfig& config_or(const CssConfig& fallback) const {
    return n > 0 ? ptrs[0]->config() : fallback;
  }

  CssRule get(const char* el, size_t el_len, const char* id, size_t id_len, const char* cls, size_t cls_len) const {
    CssRule r;
    for (size_t i = 0; i < n; ++i)
      r = r + ptrs[i]->get(el, el_len, id, id_len, cls, cls_len);
    return r;
  }
};

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

std::string Epub::resolve_path(const std::string& base_dir, const std::string& href) {
  std::string path = href;

  // Strip leading "./"
  while (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
    path = path.substr(2);
  }

  std::string dir = base_dir;

  // Handle "../"
  while (path.size() >= 3 && path[0] == '.' && path[1] == '.' && path[2] == '/') {
    path = path.substr(3);
    // Go up one directory
    if (!dir.empty() && dir.back() == '/')
      dir.pop_back();
    auto pos = dir.rfind('/');
    if (pos != std::string::npos) {
      dir = dir.substr(0, pos + 1);
    } else {
      dir.clear();
    }
  }

  return dir + path;
}

int Epub::find_entry_index(const std::string& path) const {
  for (size_t i = 0; i < zip_.entry_count(); ++i) {
    if (zip_.entry(i).name == path)
      return static_cast<int>(i);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Container.xml parsing
// ---------------------------------------------------------------------------

EpubError Epub::parse_container(IZipFile& file, std::string& rootfile_path, uint8_t* work_buf, size_t work_buf_size,
                                uint8_t* xml_buf, size_t xml_buf_size) {
  auto* entry = zip_.find("META-INF/container.xml");
  if (!entry)
    return EpubError::ContainerMissing;

  // Stream-parse via ZipEntryInput to avoid allocating the full decompressed
  // container.xml as a contiguous heap block (can fail after fragmentation).
  ZipEntryInput zip_input;
  if (zip_input.open(file, *entry, work_buf, work_buf_size) != ZipError::Ok)
    return EpubError::ZipError;

  XmlReader reader;
  if (reader.open(zip_input, xml_buf, xml_buf_size) != XmlError::Ok)
    return EpubError::XmlError;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement && sv_eq(ev.name, "rootfile")) {
      auto fp = ev.attrs.get("full-path");
      if (!fp.empty()) {
        rootfile_path = sv_to_string(fp);
        return EpubError::Ok;
      }
    }
  }
  return EpubError::ContentOpfMissing;
}

// ---------------------------------------------------------------------------
// OPF parsing (metadata, manifest, spine)
// ---------------------------------------------------------------------------

namespace {

enum class MediaType : uint8_t {
  Image,
  Xhtml,
  Css,
  Ncx,
  Unknown,
};

MediaType parse_media_type(const std::string& s) {
  if (s == "image/jpeg" || s == "image/png" || s == "image/gif")
    return MediaType::Image;
  if (s == "application/xhtml+xml")
    return MediaType::Xhtml;
  if (s == "text/css")
    return MediaType::Css;
  if (s == "application/x-dtbncx+xml")
    return MediaType::Ncx;
  return MediaType::Unknown;
}

// FNV-1a hash for compact manifest ID lookup (avoids storing ID strings).
static uint32_t fnv1a(const char* data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i)
    h = (h ^ static_cast<uint8_t>(data[i])) * 16777619u;
  return h;
}

}  // namespace

// NOTE: OPF parsing uses a flat single-pass design instead of hierarchical
// sub-parsers. This avoids XmlReader buffer state issues where nested parse
// functions break on large OPFs (e.g. 77K+ bytes with 500+ manifest items).

// Decode basic HTML entities (used by NCX and OPF parsers)

// Normalize a path by resolving ".." segments (e.g. "OEBPS/ncx/../Text/ch01.xhtml" → "OEBPS/Text/ch01.xhtml").
static std::string normalize_path(const std::string& path) {
  std::vector<std::string_view> parts;
  std::string_view sv(path);
  size_t start = 0;
  for (size_t i = 0; i <= sv.size(); ++i) {
    if (i == sv.size() || sv[i] == '/') {
      auto seg = sv.substr(start, i - start);
      if (seg == "..") {
        if (!parts.empty())
          parts.pop_back();
      } else if (!seg.empty() && seg != ".") {
        parts.push_back(seg);
      }
      start = i + 1;
    }
  }
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      result += '/';
    result.append(parts[i].data(), parts[i].size());
  }
  return result;
}

// Decode percent escapes in a URI path for ZIP entry lookup.  EPUB 2 NCX
// targets are URIs, while ZIP entry names store the decoded filename bytes.
// Invalid escapes are kept verbatim so they can still match a literal '%'.
static std::string decode_percent_escapes(const std::string& path) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };

  std::string decoded;
  decoded.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      int high = hex_value(path[i + 1]);
      int low = hex_value(path[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded += static_cast<char>((high << 4) | low);
        i += 2;
        continue;
      }
    }
    decoded += path[i];
  }
  return decoded;
}

static std::string decode_entities(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '&') {
      size_t semi = text.find(';', i);
      if (semi != std::string::npos && semi - i < 12) {
        DecodedEntity decoded = decode_html_entity(text.data() + i + 1, semi - i - 1);
        if (decoded.kind == DecodedEntity::Kind::Space) {
          result += ' ';
        } else if (decoded.kind == DecodedEntity::Kind::Utf8) {
          result.append(decoded.utf8.data(), decoded.utf8.size());
        } else if (decoded.kind == DecodedEntity::Kind::Codepoint) {
          append_utf8(result, decoded.codepoint);
        } else {
          result += text.substr(i, semi - i + 1);
        }
        i = semi + 1;
        continue;
      }
    }
    result += text[i];
    ++i;
  }
  return result;
}

// Collapse all runs of whitespace (including newlines, tabs) to a single space
// and strip leading/trailing whitespace. Used to normalize navLabel text.
static std::string normalize_whitespace(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool in_space = true;  // start true so leading whitespace is stripped
  for (unsigned char c : s) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!in_space) {
        out += ' ';
        in_space = true;
      }
    } else {
      out += static_cast<char>(c);
      in_space = false;
    }
  }
  // Strip trailing space
  if (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

// Parse NCX table of contents
static EpubError parse_ncx(IZipFile& file, const ZipReader& zip, const ZipEntry& entry, const std::string& root_dir,
                           TableOfContents& toc, uint8_t* work_buf, size_t work_buf_size, uint8_t* xml_buf,
                           size_t xml_buf_size) {
  ZipEntryInput zip_input;
  if (zip_input.open(file, entry, work_buf, work_buf_size) != ZipError::Ok)
    return EpubError::ZipError;

  XmlReader reader;
  if (reader.open(zip_input, xml_buf, xml_buf_size) != XmlError::Ok)
    return EpubError::XmlError;

  // Simple NCX parsing: find navPoint → navLabel → text, content[@src]
  // nav_depth tracks nesting so sub-chapters carry a non-zero depth.
  std::string current_label;
  int nav_depth = 0;  // current nesting level (0 = outside any navPoint)
  bool in_nav_label = false;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;

    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "navPoint")) {
        ++nav_depth;
        current_label.clear();
      } else if (nav_depth > 0 && sv_eq(ev.name, "navLabel")) {
        in_nav_label = true;
      } else if (in_nav_label && sv_eq(ev.name, "text")) {
        XmlEvent text;
        if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
          current_label = normalize_whitespace(decode_entities(sv_to_string(text.content)));
        }
      } else if (nav_depth > 0 && sv_eq(ev.name, "content")) {
        auto src = sv_to_string(ev.attrs.get("src"));
        if (!src.empty()) {
          // Separate fragment from path
          std::string fragment;
          auto hash = src.find('#');
          if (hash != std::string::npos) {
            fragment = src.substr(hash + 1);
            src = src.substr(0, hash);
          }

          std::string full_path = normalize_path(root_dir + src);
          int idx = -1;
          for (size_t i = 0; i < zip.entry_count(); ++i) {
            if (zip.entry(i).name == full_path) {
              idx = static_cast<int>(i);
              break;
            }
          }
          // Prefer an exact match: '%' is legal in a ZIP entry name.  If it
          // is absent, resolve the URI's percent escapes for EPUB 2 NCX files
          // that encode otherwise literal filename characters (e.g. %21).
          if (idx < 0 && full_path.find('%') != std::string::npos) {
            std::string decoded_path = decode_percent_escapes(full_path);
            if (decoded_path != full_path) {
              for (size_t i = 0; i < zip.entry_count(); ++i) {
                if (zip.entry(i).name == decoded_path) {
                  idx = static_cast<int>(i);
                  break;
                }
              }
            }
          }
          if (idx >= 0) {
            uint8_t depth = static_cast<uint8_t>(nav_depth - 1 < 255 ? nav_depth - 1 : 255);
            toc.add_entry(current_label, static_cast<uint16_t>(idx), depth, fragment);
          }
        }
      }
    } else if (ev.type == XmlEventType::EndElement) {
      if (sv_eq(ev.name, "navPoint"))
        --nav_depth;
      if (sv_eq(ev.name, "navLabel"))
        in_nav_label = false;
    }
  }
  return EpubError::Ok;
}

EpubError Epub::parse_opf(IZipFile& file, const std::string& opf_path, uint8_t* work_buf, uint8_t* xml_buf,
                          bool parse_css_ncx) {
  auto* entry = zip_.find(opf_path);
  if (!entry)
    return EpubError::ContentOpfMissing;

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024;
  static constexpr size_t kXmlBufSize = 4096;
  ZipEntryInput zip_input;
  if (zip_input.open(file, *entry, work_buf, kWorkBufSize) != ZipError::Ok)
    return EpubError::ZipError;

  XmlReader reader;
  if (reader.open(zip_input, xml_buf, kXmlBufSize) != XmlError::Ok)
    return EpubError::XmlError;

  // Compact manifest: store only hash + file_idx (~6 bytes/entry instead of ~120).
  // This keeps heap usage under control for EPUBs with 500+ manifest items.
  // Pre-reserve to avoid reallocation fragmentation (critical for ESP32).
  struct ManifestRef {
    uint32_t id_hash;
    int16_t file_idx;
  };
  std::vector<ManifestRef> manifest;
  manifest.reserve(zip_.entry_count());
  int ncx_file_idx = -1;
  std::string toc_id_ref;
  uint32_t ncx_id_hash = 0;

  enum class Section { None, Metadata, Manifest, Spine } section = Section::None;

  XmlEvent ev;
  for (;;) {
    if (reader.next_event(ev) != XmlError::Ok)
      break;
    if (ev.type == XmlEventType::EndOfFile)
      break;

    if (ev.type == XmlEventType::StartElement) {
      // Section transitions
      if (sv_eq(ev.name, "metadata") || sv_eq(ev.name, "opf:metadata")) {
        section = Section::Metadata;
        continue;
      } else if (sv_eq(ev.name, "manifest") || sv_eq(ev.name, "opf:manifest")) {
        section = Section::Manifest;
        continue;
      } else if (sv_eq(ev.name, "spine") || sv_eq(ev.name, "opf:spine")) {
        section = Section::Spine;
        toc_id_ref = sv_to_string(ev.attrs.get("toc"));
        continue;
      }

      // Handle elements per section
      if (section == Section::Metadata) {
        if (sv_eq(ev.name, "dc:title") || sv_eq(ev.name, "title")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (metadata_.title.empty())
              metadata_.title = normalize_whitespace(sv_to_string(text.content));
          }
        } else if (sv_eq(ev.name, "dc:creator") || sv_eq(ev.name, "creator")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (!metadata_.author.has_value())
              metadata_.author = normalize_whitespace(sv_to_string(text.content));
          }
        } else if (sv_eq(ev.name, "dc:language") || sv_eq(ev.name, "language")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (!metadata_.language.has_value())
              metadata_.language = normalize_whitespace(sv_to_string(text.content));
          }
        } else if (sv_eq(ev.name, "meta")) {
          auto name = ev.attrs.get("name");
          if (sv_eq(name, "cover")) {
            auto content = ev.attrs.get("content");
            if (!content.empty()) {
              metadata_.cover_id = sv_to_string(content);
            }
          }
        }
      } else if (section == Section::Manifest) {
        if (sv_eq(ev.name, "item")) {
          auto id = ev.attrs.get("id");
          auto href = ev.attrs.get("href");
          auto mt_sv = ev.attrs.get("media-type");

          if (!id.empty() && !href.empty()) {
            std::string full_path = root_dir_ + std::string(href.data, href.length);
            int idx = -1;
            for (size_t i = 0; i < zip_.entry_count(); ++i) {
              if (zip_.entry(i).name == full_path) {
                idx = static_cast<int>(i);
                break;
              }
            }
            uint32_t h = fnv1a(id.data, id.length);
            manifest.push_back({h, static_cast<int16_t>(idx)});

            auto mt = parse_media_type(std::string(mt_sv.data, mt_sv.length));
            if (mt == MediaType::Ncx) {
              ncx_file_idx = idx;
              ncx_id_hash = h;
            }
            // Resolve cover inline (metadata is parsed before manifest)
            if (metadata_.cover_id.has_value() && id.length == metadata_.cover_id->size() &&
                std::string(id.data, id.length) == *metadata_.cover_id) {
              cover_idx_ = idx;
            }
          }
        }
      } else if (section == Section::Spine) {
        if (sv_eq(ev.name, "itemref")) {
          auto idref = ev.attrs.get("idref");
          uint32_t h = fnv1a(idref.data, idref.length);
          for (auto& ref : manifest) {
            if (ref.id_hash == h && ref.file_idx >= 0) {
              spine_.push_back({static_cast<uint16_t>(ref.file_idx)});
              break;
            }
          }
        }
      }
    } else if (ev.type == XmlEventType::EndElement) {
      if (sv_eq(ev.name, "metadata") || sv_eq(ev.name, "opf:metadata") || sv_eq(ev.name, "manifest") ||
          sv_eq(ev.name, "opf:manifest") || sv_eq(ev.name, "spine") || sv_eq(ev.name, "opf:spine")) {
        section = Section::None;
      }
    }
  }

  if (!toc_id_ref.empty() && ncx_file_idx >= 0) {
    if (fnv1a(toc_id_ref.data(), toc_id_ref.size()) != ncx_id_hash)
      ncx_file_idx = -1;
  }

  manifest.clear();
  manifest.shrink_to_fit();

  if (!parse_css_ncx)
    return EpubError::Ok;

  if (ncx_file_idx >= 0) {
    auto& ncx_entry = zip_.entry(ncx_file_idx);
    // NCX src paths are relative to the NCX file's directory, not the OPF root.
    std::string ncx_dir = root_dir_;
    auto slash = std::string_view(ncx_entry.name).rfind('/');
    if (slash != std::string_view::npos)
      ncx_dir = std::string(ncx_entry.name.data(), slash + 1);
    parse_ncx(file, zip_, ncx_entry, ncx_dir, toc_, work_buf, kWorkBufSize, xml_buf, kXmlBufSize);
  }

  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Epub::open
// ---------------------------------------------------------------------------

void Epub::close() {
  zip_ = ZipReader{};
  root_dir_.clear();
  root_dir_.shrink_to_fit();
  metadata_ = EpubMetadata{};
  spine_.clear();
  spine_.shrink_to_fit();
  toc_ = TableOfContents{};
  css_cache_.clear();
  cover_idx_ = -1;
}

EpubError Epub::open(IZipFile& file, uint8_t* work_buf, uint8_t* xml_buf, bool parse_css_ncx) {
  close();  // release previous data
  // Pass work_buf for the central-directory bulk read to avoid a heap
  // allocation that may fail on fragmented ESP32 heap (cd_buf ≤ 48 KB fits).
  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 2048;
  if (zip_.open(file) != ZipError::Ok)
    return EpubError::ZipError;

  std::string rootfile_path;
  auto err = parse_container(file, rootfile_path, work_buf,
                             ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 2048, xml_buf, 4096);
  if (err != EpubError::Ok)
    return err;

  // Determine root directory
  auto slash = rootfile_path.rfind('/');
  if (slash != std::string::npos) {
    root_dir_ = rootfile_path.substr(0, slash + 1);
  } else {
    root_dir_.clear();
  }

  err = parse_opf(file, rootfile_path, work_buf, xml_buf, parse_css_ncx);
  if (err != EpubError::Ok) {
    return err;
  }

  return EpubError::Ok;
}

EpubError Epub::open_zip_only(IZipFile& file) {
  close();
  if (zip_.open(file) != ZipError::Ok)
    return EpubError::ZipError;
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// XHTML body → Paragraphs
// ---------------------------------------------------------------------------

namespace {

static bool is_block_element(XmlStringView sv) {
  if (!sv.data)
    return false;
  switch (sv.length) {
    case 1:
      return sv.data[0] == 'p';
    case 2:
      if (sv.data[0] == 'h')
        return sv.data[1] >= '1' && sv.data[1] <= '6';
      return std::memcmp(sv.data, "li", 2) == 0 || std::memcmp(sv.data, "ul", 2) == 0 ||
             std::memcmp(sv.data, "ol", 2) == 0 || std::memcmp(sv.data, "dl", 2) == 0 ||
             std::memcmp(sv.data, "dd", 2) == 0 || std::memcmp(sv.data, "dt", 2) == 0 ||
             std::memcmp(sv.data, "tr", 2) == 0;
    case 3:
      return std::memcmp(sv.data, "div", 3) == 0 || std::memcmp(sv.data, "pre", 3) == 0 ||
             std::memcmp(sv.data, "nav", 3) == 0;
    case 5:
      return std::memcmp(sv.data, "aside", 5) == 0 || std::memcmp(sv.data, "table", 5) == 0 ||
             std::memcmp(sv.data, "tbody", 5) == 0 || std::memcmp(sv.data, "thead", 5) == 0 ||
             std::memcmp(sv.data, "tfoot", 5) == 0;
    case 6:
      return std::memcmp(sv.data, "figure", 6) == 0 || std::memcmp(sv.data, "header", 6) == 0 ||
             std::memcmp(sv.data, "footer", 6) == 0;
    case 7:
      return std::memcmp(sv.data, "section", 7) == 0 || std::memcmp(sv.data, "article", 7) == 0;
    case 10:
      return std::memcmp(sv.data, "blockquote", 10) == 0 || std::memcmp(sv.data, "figcaption", 10) == 0;
    default:
      return false;
  }
}

static bool is_italic(XmlStringView name) {
  return name == "i" || name == "em";
}

static bool is_bold(XmlStringView name) {
  return name == "b" || name == "strong" || name == "h1" || name == "h2" || name == "h3" || name == "h4" ||
         name == "h5" || name == "h6";
}

static bool is_breaking(XmlStringView name) {
  return name == "br" || name == "tr";
}

static uint8_t heading_size_pct(XmlStringView name) {
  if (name == "h1")
    return 160;
  if (name == "h2")
    return 140;
  if (name == "h3")
    return 120;
  return 100;
}

static bool is_small_element(XmlStringView name) {
  return name == "small" || name == "sub" || name == "sup";
}

class BodyParser {
 public:
  using Sink = void (*)(void* ctx, Paragraph&& para);

  void set_sink(Sink s, void* ctx) {
    sink_ = s;
    sink_ctx_ = ctx;
  }

  // Pre-allocate run vector capacity to avoid mid-parse reallocation on
  // fragmented ESP32 heap. Call before parse_xhtml_events.
  void reserve_runs(size_t n) {
#ifdef ESP_PLATFORM
    // Cap to what the heap can actually provide to avoid bad_alloc on
    // fragmented ESP32 heap (largest block may be much smaller than free).
    {
      size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      constexpr size_t kReserveHeadroom = 6144;
      size_t affordable = (largest > kReserveHeadroom) ? (largest - kReserveHeadroom) / sizeof(Run) : 0;
      if (affordable < n)
        n = affordable;
    }
#endif
    if (n > 0)
      runs_.reserve(n);
  }

  void push_text(const char* text, size_t len) {
    if (pre_depth_.has_value()) {
      push_preformatted_text(text, len);
      return;
    }

    // Flush any deferred list prefix (bullet/number) into this first text push.
    // Only trigger on non-whitespace text so that whitespace-only nodes between
    // <li> and an inner <p> don't strand the bullet in its own paragraph.
    if (!pending_list_prefix_.empty()) {
      bool has_nonws = false;
      for (size_t i = 0; i < len; ++i) {
        if (!std::isspace(static_cast<unsigned char>(text[i]))) {
          has_nonws = true;
          break;
        }
      }
      if (has_nonws) {
        std::string prefix = std::move(pending_list_prefix_);
        push_text(prefix.c_str(), prefix.size());
      }
    }

    // If previous text event ended with an incomplete multi-byte sequence
    // (UTF-8 lead byte or partial entity like "&amp"), prepend it so the
    // full sequence is processed as a whole.
    std::string joined;
    const char* t = text;
    size_t tlen = len;
    if (!pending_utf8_.empty()) {
      joined = std::move(pending_utf8_);
      pending_utf8_.clear();
      joined.append(text, len);
      t = joined.data();
      tlen = joined.size();
    }

    // Check if this chunk ends with an incomplete UTF-8 lead byte.
    if (tlen > 0) {
      size_t check = (tlen >= 4) ? tlen - 4 : 0;
      for (size_t j = tlen; j > check; --j) {
        unsigned char uc = static_cast<unsigned char>(t[j - 1]);
        if ((uc & 0xC0) != 0x80) {
          size_t expected = 1;
          if (uc >= 0xC0 && uc < 0xE0)
            expected = 2;
          else if (uc >= 0xE0 && uc < 0xF0)
            expected = 3;
          else if (uc >= 0xF0)
            expected = 4;
          size_t available = tlen - (j - 1);
          if (available < expected) {
            pending_utf8_.assign(t + j - 1, available);
            tlen = j - 1;
          }
          break;
        }
      }
    }

    // Check if this chunk ends with a partial entity (& followed by up to
    // 11 chars of entity name but no terminating ';').  Buffer it for the
    // next text event so the entity decoder sees the complete token.
    if (tlen > 0) {
      // Scan backwards for '&' within the last 12 bytes (max entity length)
      size_t scan_start = (tlen > 12) ? tlen - 12 : 0;
      for (size_t j = tlen; j > scan_start; --j) {
        if (t[j - 1] == '&') {
          // Found '&' — check if there's a ';' after it.
          bool has_semi = false;
          for (size_t k = j; k < tlen; ++k) {
            if (t[k] == ';') {
              has_semi = true;
              break;
            }
          }
          if (!has_semi) {
            // Partial entity — save from '&' onwards
            pending_utf8_.assign(t + j - 1, tlen - (j - 1));
            tlen = j - 1;
          }
          break;
        }
      }
    }

    // If at start of paragraph, trim leading whitespace
    bool no_runs = runs_.empty();
    if (no_runs && current_run_.empty()) {
      while (tlen > 0 && std::isspace(static_cast<unsigned char>(*t))) {
        ++t;
        --tlen;
      }
    }

    if (tlen == 0)
      return;

    // Minimize reallocations — output can't exceed input length.
    current_run_.reserve(current_run_.size() + tlen);

    // Fused decode_entities + whitespace normalization, writing directly
    // into current_run_ (no intermediate buffer).
    //
    // - in_space:      pending collapsed whitespace not yet emitted
    // - wrote_content: at least one non-ws char was emitted in this call
    // - at_para_start: suppress leading whitespace entirely
    bool at_para_start = no_runs && current_run_.empty();
    bool in_space = false;
    bool wrote_content = false;
    size_t i = 0;

// Emit pending whitespace before non-ws content.
#define PUSH_TEXT_EMIT_SPACE_()                               \
  do {                                                        \
    if (!wrote_content) {                                     \
      if (in_space && !has_trailing_space_ && !at_para_start) \
        current_run_ += ' ';                                  \
      wrote_content = true;                                   \
    } else if (in_space) {                                    \
      current_run_ += ' ';                                    \
    }                                                         \
    in_space = false;                                         \
  } while (0)

    while (i < tlen) {
      unsigned char uc = static_cast<unsigned char>(t[i]);

      // Entity decoding
      if (uc == '&') {
        // Find semicolon within 12 chars
        size_t semi = SIZE_MAX;
        for (size_t j = i + 1; j < tlen && j < i + 12; ++j) {
          if (t[j] == ';') {
            semi = j;
            break;
          }
        }
        if (semi != SIZE_MAX) {
          const char* ent = t + i + 1;
          size_t ent_len = semi - i - 1;
          DecodedEntity decoded = decode_html_entity(ent, ent_len);

          if (decoded.kind == DecodedEntity::Kind::Space) {
            in_space = true;
            i = semi + 1;
            continue;
          }

          if (decoded.kind == DecodedEntity::Kind::Utf8 || decoded.kind == DecodedEntity::Kind::Codepoint) {
            PUSH_TEXT_EMIT_SPACE_();
            size_t mark = current_run_.size();
            if (decoded.kind == DecodedEntity::Kind::Utf8) {
              current_run_.append(decoded.utf8.data(), decoded.utf8.size());
            } else {
              append_utf8(current_run_, decoded.codepoint);
            }
            if (text_transform_ != TextTransform::None)
              apply_text_transform_inplace(current_run_, mark);
            i = semi + 1;
            continue;
          }
          // Unknown entity — keep as-is, fall through
        }
        // Not a valid entity — output the '&' character
        PUSH_TEXT_EMIT_SPACE_();
        current_run_ += '&';
        ++i;
        continue;
      }

      // UTF-8 BOM / zero-width no-break space (U+FEFF = 0xEF 0xBB 0xBF) — silently skip.
      if (uc == 0xEF && i + 2 < tlen && static_cast<unsigned char>(t[i + 1]) == 0xBB &&
          static_cast<unsigned char>(t[i + 2]) == 0xBF) {
        i += 3;
        continue;
      }

      // UTF-8 non-breaking space (0xC2 0xA0)
      if (uc == 0xC2 && i + 1 < tlen && static_cast<unsigned char>(t[i + 1]) == 0xA0) {
        in_space = true;
        i += 2;
        continue;
      }

      // Normal whitespace
      if (std::isspace(uc)) {
        in_space = true;
        ++i;
        continue;
      }

      // Regular character — batch consecutive plain bytes.
      PUSH_TEXT_EMIT_SPACE_();
      {
        size_t span_start = i;
        ++i;
        while (i < tlen) {
          unsigned char nc = static_cast<unsigned char>(t[i]);
          if (nc == '&' || std::isspace(nc) ||
              (nc == 0xC2 && i + 1 < tlen && static_cast<unsigned char>(t[i + 1]) == 0xA0) ||
              (nc == 0xEF && i + 2 < tlen && static_cast<unsigned char>(t[i + 1]) == 0xBB &&
               static_cast<unsigned char>(t[i + 2]) == 0xBF))
            break;
          ++i;
        }
        if (text_transform_ == TextTransform::None) {
          current_run_.append(t + span_start, i - span_start);
        } else {
          size_t mark = current_run_.size();
          current_run_.append(t + span_start, i - span_start);
          apply_text_transform_inplace(current_run_, mark);
        }
      }
    }

#undef PUSH_TEXT_EMIT_SPACE_

    if (!wrote_content) {
      // All whitespace — propagate trailing-space state
      if (!has_trailing_space_ && !at_para_start) {
        current_run_ += ' ';
        has_trailing_space_ = true;
      }
      return;
    }

    has_trailing_space_ = in_space;
    if (has_trailing_space_) {
      current_run_ += ' ';
    }

    // Prevent current_run_ from growing too large.  On ESP32-C3 with ~60KB
    // available after the decompression + XML buffers, a std::string growing
    // from N to 2N needs 3N bytes temporarily (old + new).  Capping at 2KB
    // keeps the worst-case reallocation at ~6KB.  Splitting a run at a word
    // boundary (after a space) produces identical layout output regardless of
    // XML buffer size, because the layout engine splits words on whitespace.
    if (current_run_.size() >= 2048 && !current_run_.empty() && current_run_.back() == ' ')
      flush_text(false);
  }

  void set_bold(bool b) {
    if (bold_ != b) {
      flush_text(false);
      bold_ = b;
    }
  }

  void set_italic(bool i) {
    if (italic_ != i) {
      flush_text(false);
      italic_ = i;
    }
  }

  void set_size_pct(uint8_t s) {
    if (font_size_pct_ != s) {
      flush_text(false);
      font_size_pct_ = s;
    }
  }

  void set_vertical_align(VerticalAlign a) {
    if (vertical_align_ != a) {
      flush_text(false);
      vertical_align_ = a;
    }
  }

  void set_margin_left(uint16_t m) {
    if (margin_left_ != m) {
      flush_text(false);
      margin_left_ = m;
    }
  }

  void set_margin_right(uint16_t m) {
    if (margin_right_ != m) {
      flush_text(false);
      margin_right_ = m;
    }
  }

  void break_line(bool emit_if_empty = false) {
    flush_text(true);
    // If flush_text had nothing to flush (current_run_ was empty because
    // a prior set_size() already flushed it), mark the last run as breaking.
    if (!runs_.empty() && !runs_.back().breaking) {
      runs_.back().breaking = true;
    }
    // If we have no runs at all (e.g. <br/> between block elements),
    // emit an empty paragraph to produce visible blank space.
    // Only do this for actual <br/> tags (emit_if_empty=true), not <tr> etc.
    if (emit_if_empty && runs_.empty()) {
      auto para = Paragraph::make_text(TextParagraph{});
      emit(std::move(para));
    }
    // After a line break, suppress leading whitespace (from source formatting)
    has_trailing_space_ = true;
  }

  void flush_run() {
    flush_text(false);
    if (!runs_.empty()) {
      // Trim trailing whitespace from last run
      auto& last = runs_.back();
      while (!last.text.empty() && std::isspace(static_cast<unsigned char>(last.text.back()))) {
        last.text.pop_back();
      }
      TextParagraph tp;
      tp.runs = std::move(runs_);
      // NOTE: do NOT reserve runs_ here — tp.runs still holds the buffer,
      // so a reserve now would require two simultaneous 512-run allocations.
      // Reserve AFTER emit() when the old buffer has been freed.
      size_t prev_cap = tp.runs.capacity();
      tp.alignment = alignment_;
      tp.indent = indent_;
      tp.inline_image = pending_inline_image_;  // attach float image if pending
      tp.line_height_pct = line_height_pct_;
      pending_inline_image_.reset();
      auto para = Paragraph::make_text(std::move(tp));

      // Apply vertical margin collapsing: max(pending_bottom, current_top)
      if (pending_margin_top_.has_value() || pending_margin_bottom_.has_value()) {
        uint16_t top = pending_margin_top_.value_or(0);
        uint16_t bot = pending_margin_bottom_.value_or(0);
        // Cap spacing to avoid huge CSS margins (e.g. 5em) creating excessive whitespace
        // on a small e-reader screen.
        static constexpr uint16_t kMaxSpacing = 60;  // ~2.7 × 22px line height
        para.spacing_before = std::min(std::max(top, bot), kMaxSpacing);
        pending_margin_top_.reset();
        pending_margin_bottom_.reset();
      }

      emit(std::move(para));
      // Re-reserve runs_ after emit() so the just-freed buffer block is available.
      // Cap to what the heap can actually provide (largest block may be fragmented).
      if (prev_cap > 0) {
#ifdef ESP_PLATFORM
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        constexpr size_t kReserveHeadroom = 6144;
        size_t affordable = (largest > kReserveHeadroom) ? (largest - kReserveHeadroom) / sizeof(Run) : 0;
        if (affordable < prev_cap)
          prev_cap = affordable;
#endif
        if (prev_cap > 0)
          runs_.reserve(prev_cap);
      }
      runs_.clear();
      indent_.reset();
    } else if (pending_inline_image_.has_value()) {
      // No text to merge with — emit as standalone image paragraph
      auto img = *pending_inline_image_;
      pending_inline_image_.reset();
      emit(Paragraph::make_image(img.key, img.attr_width, img.attr_height));
    }
  }

  void push_image(uint16_t key, uint16_t w = 0, uint16_t h = 0) {
    flush_run();
    emit(Paragraph::make_image(key, w, h));
  }

  // Store a float image to be merged inline with the next text paragraph.
  void set_pending_inline_image(uint16_t key, uint16_t w, uint16_t h) {
    pending_inline_image_ = ImageRef(key, w, h);
  }

  void push_hr(std::optional<uint16_t> spacing_before = std::nullopt, std::optional<uint8_t> width_pct = std::nullopt) {
    flush_run();
    auto p = Paragraph::make_hr();
    p.spacing_before = spacing_before;
    p.hr_width_pct = width_pct;
    emit(std::move(p));
  }

  void push_page_break() {
    flush_run();
    // Only emit if there is already content (avoid leading blank page)
    if (has_emitted_ || !paragraphs_.empty()) {
      emit(Paragraph::make_page_break());
    }
  }

  std::vector<Paragraph> finish() {
    flush_run();
    return std::move(paragraphs_);
  }

  // Style stack management
  uint8_t depth = 0;
  struct BoolEntry {
    bool prev_value;
    uint8_t depth;
  };
  std::vector<BoolEntry> bold_stack_;
  std::vector<BoolEntry> italic_stack_;

  struct SizeEntry {
    uint8_t prev_value;
    uint8_t depth;
  };
  std::vector<SizeEntry> size_stack_;

  struct VerticalAlignEntry {
    VerticalAlign prev_value;
    uint8_t depth;
  };
  std::vector<VerticalAlignEntry> vertical_align_stack_;
  VerticalAlign vertical_align_ = VerticalAlign::Baseline;

  std::optional<uint8_t> float_depth;
  std::optional<ImageRef> pending_inline_image_;  // float image awaiting merge
  std::optional<uint8_t> cell_depth;              // inside <td>/<th>: suppress inner block flushes
  std::optional<uint8_t> pre_depth_;              // inside <pre>: preserve whitespace
  bool merge_after_float_ = false;                // suppress next block flush after float close
  bool float_had_text_ = false;                   // true if text was pushed during a float
  std::optional<uint8_t> drop_cap_depth_;         // inside <span float:left> awaiting single-char drop cap
  std::optional<char32_t> drop_cap_char_;         // captured drop cap codepoint
  std::optional<uint8_t> drop_cap_size_pct_;      // font-size to apply (150% or CSS override)
  struct AlignmentEntry {
    std::optional<Alignment> prev_value;
    uint8_t depth;
  };
  std::vector<AlignmentEntry> alignment_stack_;
  std::optional<Alignment> alignment_;
  std::optional<int16_t> indent_;

  // margin_left uses a stack for additive nesting (e.g. div.poem 30% + span 2em)
  struct MarginEntry {
    uint16_t prev_value;
    uint8_t depth;
  };
  std::vector<MarginEntry> margin_left_stack_;
  uint16_t margin_left_ = 0;
  std::vector<MarginEntry> margin_right_stack_;
  uint16_t margin_right_ = 0;

  // List tracking for bullet/number prefixes
  struct ListContext {
    bool ordered;      // true = <ol>, false = <ul>
    uint16_t counter;  // next item number (for <ol>)
    uint8_t depth;
    bool no_prefix;  // true if list-style-type: none on <ul>/<ol>
  };
  std::vector<ListContext> list_stack_;

  // Deferred list prefix: set on <li>, flushed into the first push_text of that item.
  // This prevents the bullet from landing in its own paragraph when <li> contains <p>.
  std::string pending_list_prefix_;

  // Hyperlink tracking: href for the innermost <a href="..."> element.
  std::string current_href_;  // empty = no active link
  uint8_t href_depth_ = 0;    // depth at which the <a> was opened

  // page-break-after tracking: emit a page break when these depths close
  std::vector<uint8_t> page_break_after_depths_;

  // Vertical margin collapsing: pending bottom margin from previous element
  std::optional<uint16_t> pending_margin_bottom_;
  std::optional<uint16_t> pending_margin_top_;  // margin-top for the NEXT paragraph emitted

  // Track margin-bottom values for elements, emit when they close
  struct MarginBottomEntry {
    uint16_t value;
    uint8_t depth;
  };
  std::vector<MarginBottomEntry> margin_bottom_stack_;

  // text-transform tracking
  struct TransformEntry {
    TextTransform prev_value;
    uint8_t depth;
  };
  std::vector<TransformEntry> transform_stack_;
  TextTransform text_transform_ = TextTransform::None;
  bool small_caps_ = false;  // true when font-variant:small-caps is active

  struct SmallCapsEntry {
    bool prev_value;
    uint8_t depth;
  };
  std::vector<SmallCapsEntry> small_caps_stack_;

  // line-height tracking (percentage of default, 100 = normal)
  struct LineHeightEntry {
    uint8_t prev_value;
    uint8_t depth;
  };
  std::vector<LineHeightEntry> line_height_stack_;
  uint8_t line_height_pct_ = 100;

  FontStyle style() const {
    if (bold_ && italic_)
      return FontStyle::BoldItalic;
    if (bold_)
      return FontStyle::Bold;
    if (italic_)
      return FontStyle::Italic;
    return FontStyle::Regular;
  }

  // Ensure a word boundary exists (space separator) without starting a new paragraph.
  // Used for table cells so adjacent <td> content doesn't concatenate.
  void ensure_word_break() {
    if (!current_run_.empty() && !std::isspace(static_cast<unsigned char>(current_run_.back()))) {
      current_run_ += ' ';
    }
    has_trailing_space_ = true;
  }

  void flush_text(bool breaking) {
    if (!current_run_.empty()) {
      Run r{std::move(current_run_), style(), font_size_pct_, breaking};
      r.vertical_align = vertical_align_;
      r.margin_left = margin_left_;
      r.margin_right = margin_right_;
      if (!current_href_.empty())
        r.href = current_href_;
      // On ESP32: if runs_ is at capacity, emit the accumulated runs as a partial
      // paragraph now rather than letting the vector double in size (which may fail
      // on a fragmented heap). The capacity >= 16 guard prevents splitting tiny
      // paragraphs when capacity was never pre-reserved (e.g. desktop/test paths).
#ifdef ESP_PLATFORM
      if (runs_.size() == runs_.capacity() && runs_.capacity() >= 16)
        flush_run();
#endif
      runs_.push_back(std::move(r));
      current_run_.clear();
    }
  }

  void apply_text_transform_inplace(std::string& s, size_t from) const {
    if (text_transform_ == TextTransform::Uppercase) {
      for (size_t k = from; k < s.size(); ++k)
        s[k] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[k])));
    } else if (text_transform_ == TextTransform::Lowercase) {
      if (!small_caps_) {
        for (size_t k = from; k < s.size(); ++k)
          s[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
      }
      // If small_caps_ is active, lowercase is a fallback hint we ignore.
    } else if (text_transform_ == TextTransform::Capitalize) {
      bool after_space = (from == 0) || (from > 0 && std::isspace(static_cast<unsigned char>(s[from - 1])));
      for (size_t k = from; k < s.size(); ++k) {
        if (std::isspace(static_cast<unsigned char>(s[k]))) {
          after_space = true;
        } else if (after_space) {
          s[k] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[k])));
          after_space = false;
        }
      }
    }
  }

  std::string apply_text_transform(std::string s) const {
    if (text_transform_ == TextTransform::None)
      return s;
    if (text_transform_ == TextTransform::Uppercase) {
      for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (text_transform_ == TextTransform::Lowercase) {
      if (!small_caps_) {
        for (auto& c : s)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    } else if (text_transform_ == TextTransform::Capitalize) {
      bool after_space = true;
      for (auto& c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
          after_space = true;
        } else if (after_space) {
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          after_space = false;
        }
      }
    }
    return s;
  }

  void push_preformatted_text(const char* text, size_t len) {
    std::string decoded = decode_entities(std::string(text, len));
    // Split on newlines, emit each line as text with break_line() between
    size_t pos = 0;
    while (pos < decoded.size()) {
      size_t nl = decoded.find('\n', pos);
      if (nl == std::string::npos) {
        current_run_ += apply_text_transform(decoded.substr(pos));
        if (current_run_.size() >= 2048 && !current_run_.empty() && current_run_.back() == ' ')
          flush_text(false);
        break;
      }
      if (nl > pos) {
        current_run_ += apply_text_transform(decoded.substr(pos, nl - pos));
      }
      break_line(true);
      pos = nl + 1;
    }
    has_trailing_space_ = false;
  }

  std::vector<Paragraph> paragraphs_;
  std::vector<Run> runs_;
  std::string current_run_;
  // normalized_ removed — push_text() writes directly into current_run_
  std::string pending_utf8_;  // incomplete UTF-8 lead byte(s) from end of previous text event
  bool bold_ = false;
  bool italic_ = false;
  uint8_t font_size_pct_ = 100;
  bool has_trailing_space_ = false;

  Sink sink_ = nullptr;
  void* sink_ctx_ = nullptr;
  bool has_emitted_ = false;
  uint32_t emitted_count_ = 0;
  using IdCallback = void (*)(void* ctx, const char* id, size_t id_len, uint32_t para_idx);
  IdCallback id_cb_ = nullptr;
  void* id_cb_ctx_ = nullptr;
  void set_id_callback(IdCallback cb, void* ctx) {
    id_cb_ = cb;
    id_cb_ctx_ = ctx;
  }

#ifdef ESP_PLATFORM
  int64_t* emit_us_ = nullptr;  // accumulator for time spent in sink callbacks
#endif

  void emit(Paragraph&& p) {
    has_emitted_ = true;
    ++emitted_count_;
    if (sink_) {
#ifdef ESP_PLATFORM
      if (emit_us_) {
        int64_t t0 = esp_timer_get_time();
        sink_(sink_ctx_, std::move(p));
        *emit_us_ += esp_timer_get_time() - t0;
      } else {
        sink_(sink_ctx_, std::move(p));
      }
#else
      sink_(sink_ctx_, std::move(p));
#endif
    } else {
      paragraphs_.push_back(std::move(p));
    }
  }
};

}  // anonymous namespace

// Internal helper: parse XML events from an already-opened XmlReader.
// Parses inline <style> elements and link-referenced stylesheets, skips to
// <body>, then processes body events.
//
// css_ctx: if non-null, <link rel="stylesheet"> tags in the head are resolved
// and loaded from the cache. extern_css (if any) is added as a base layer.
static EpubError parse_xhtml_events(XmlReader& reader, const CssStylesheet* inline_css, const CssStylesheet* extern_css,
                                    const CssContext* css_ctx, const std::string& base_dir, const ZipReader& zip,
                                    BodyParser& parser) {
  // Effective config: from extern_css first, then css_ctx, then default.
  const CssConfig& eff_config =
      extern_css ? extern_css->config() : (css_ctx ? css_ctx->config : CssConfig{});

  // Skip to <body>
  CssStylesheet parsed_inline_css(eff_config);

  // Aggregates all external stylesheets that apply to this chapter.
  // Populated from extern_css (if any) plus any <link>-referenced sheets
  // loaded from the cache during the head scan below.
  CssSheets eff_ext;
  if (extern_css)
    eff_ext.add(extern_css);
  XmlEvent ev;

#ifdef ESP_PLATFORM
  int64_t xml_us = 0;   // time in next_event (decompress + tokenize)
  int64_t css_us = 0;   // time in CSS get()
  int64_t text_us = 0;  // time in push_text (entity decode + whitespace normalize)
  int64_t elem_us = 0;  // time in StartElement dispatch (tag matching, style stacks)
  int64_t end_us = 0;   // time in EndElement dispatch
  int64_t emit_us = 0;  // time in sink callback (Paragraph → write_paragraph)
  int64_t head_us = 0;  // time seeking to <body> (includes decompressing head)
  size_t text_bytes = 0;
  size_t event_count = 0;
  parser.emit_us_ = &emit_us;
  int64_t head_t0 = esp_timer_get_time();
#endif
  for (;;) {
    XmlError xerr = reader.next_event(ev);
    if (xerr == XmlError::BufferTooSmall) {
      reader.skip_element();
      continue;
    }
    if (xerr != XmlError::Ok)
      break;
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "style")) {
        // Consume all text events inside <style>...</style>, then skip to </style>.
        // With streaming XML buffers the CSS may arrive as multiple Text events.
        for (;;) {
          XmlEvent inner;
          XmlError serr = reader.next_event(inner);
          if (serr != XmlError::Ok)
            break;
          if (inner.type == XmlEventType::Text || inner.type == XmlEventType::CData) {
            parsed_inline_css.extend_from_sheet(inner.content.data, inner.content.length);
          } else if (inner.type == XmlEventType::EndElement) {
            break;  // </style>
          } else if (inner.type == XmlEventType::EndOfFile) {
            break;
          }
        }
      } else if (sv_eq(ev.name, "link") && css_ctx) {
        // Load <link rel="stylesheet" href="..."> into the cache.
        auto rel = ev.attrs.get("rel");
        auto href = ev.attrs.get("href");
        if (sv_eq(rel, "stylesheet") && href.length > 0) {
          std::string css_path = Epub::resolve_path(*css_ctx->base_dir, sv_to_string(href));
          const CssStylesheet* s =
              css_ctx->cache->get_or_load(*css_ctx->file, *css_ctx->zip, css_path, css_ctx->config,
                                         css_ctx->chapter_start_gen,
                                         css_ctx->work_buf, css_ctx->work_buf_size);
          eff_ext.add(s);
        }
      } else if (sv_eq(ev.name, "body")) {
        break;
      }
    }
  }

  const CssStylesheet* effective_inline = inline_css ? inline_css : &parsed_inline_css;

#ifdef ESP_PLATFORM
  head_us = esp_timer_get_time() - head_t0;
#endif

#ifdef MICROREADER_DIAG_STREAMING
  size_t event_count = 0;
  size_t start_count = 0;
  size_t end_count = 0;
  size_t text_count = 0;
  XmlError last_err = XmlError::Ok;
  const char* exit_reason = "unknown";
#endif

#ifdef ESP_PLATFORM
  XmlEventType prev_type = XmlEventType::EndOfFile;
  int64_t dispatch_t0 = 0;
#endif

  for (;;) {
#ifdef ESP_PLATFORM
    // Attribute previous iteration's dispatch time
    if (dispatch_t0 != 0) {
      int64_t elapsed = esp_timer_get_time() - dispatch_t0;
      if (prev_type == XmlEventType::StartElement)
        elem_us += elapsed;
      else if (prev_type == XmlEventType::EndElement)
        end_us += elapsed;
      else if (prev_type == XmlEventType::Text)
        text_us += elapsed;
    }
    int64_t t0 = esp_timer_get_time();
#endif
    XmlError xerr = reader.next_event(ev);
#ifdef ESP_PLATFORM
    xml_us += esp_timer_get_time() - t0;
    event_count++;
#endif
    if (xerr == XmlError::BufferTooSmall) {
      reader.skip_element();
      continue;
    }
    if (xerr != XmlError::Ok) {
#ifdef MICROREADER_DIAG_STREAMING
      last_err = xerr;
      exit_reason = "error";
#endif
      break;
    }
    if (ev.type == XmlEventType::EndOfFile) {
#ifdef MICROREADER_DIAG_STREAMING
      exit_reason = "eof";
#endif
      break;
    }
    if (ev.type == XmlEventType::EndElement && sv_eq(ev.name, "body")) {
#ifdef MICROREADER_DIAG_STREAMING
      exit_reason = "body";
#endif
      break;
    }

#ifdef MICROREADER_DIAG_STREAMING
    event_count++;
    if (ev.type == XmlEventType::StartElement) {
      start_count++;
      // Track if we see unexpected elements
    } else if (ev.type == XmlEventType::EndElement)
      end_count++;
    else if (ev.type == XmlEventType::Text)
      text_count++;
#endif

#ifdef ESP_PLATFORM
    prev_type = ev.type;
    dispatch_t0 = esp_timer_get_time();
#endif

    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "img") || sv_eq(ev.name, "image")) {
        const char* attr = sv_eq(ev.name, "img") ? "src" : "xlink:href";
        auto src = sv_to_string(ev.attrs.get(attr));
        bool found_image = false;
        if (!src.empty()) {
          // Read width/height attributes if present
          uint16_t img_w = 0, img_h = 0;
          auto w_sv = ev.attrs.get("width");
          auto h_sv = ev.attrs.get("height");
          if (!w_sv.empty()) {
            img_w = static_cast<uint16_t>(std::atoi(sv_to_string(w_sv).c_str()));
          }
          if (!h_sv.empty()) {
            img_h = static_cast<uint16_t>(std::atoi(sv_to_string(h_sv).c_str()));
          }
          std::string full_path = Epub::resolve_path(base_dir, src);
          for (size_t i = 0; i < zip.entry_count(); ++i) {
            if (zip.entry(i).name == full_path) {
              if (parser.float_depth.has_value()) {
                // Inside a float: store as pending inline image for text merge
                parser.set_pending_inline_image(static_cast<uint16_t>(i), img_w, img_h);
              } else {
                parser.push_image(static_cast<uint16_t>(i), img_w, img_h);
              }
              found_image = true;
              break;
            }
          }
        }
        // Fallback: use alt text if image not found (e.g. initial-cap floats)
        if (!found_image) {
          auto alt = sv_to_string(ev.attrs.get("alt"));
          if (!alt.empty()) {
            parser.push_text(alt.c_str(), alt.size());
          }
        }
        parser.depth++;  // balance synthetic EndElement for self-closing tag
        continue;
      }

      if (sv_eq(ev.name, "hr")) {
        auto hr_id_sv = ev.attrs.get("id");
        auto hr_class_sv = ev.attrs.get("class");
        auto hr_style_sv = ev.attrs.get("style");
        CssRule hr_inline;
        if (!hr_style_sv.empty())
          hr_inline =
              CssRule::parse(hr_style_sv.data, hr_style_sv.length, eff_ext.config_or(CssConfig{}));
        const char* hr_id_p = hr_id_sv.data ? hr_id_sv.data : nullptr;
        const char* hr_cls_p = hr_class_sv.data ? hr_class_sv.data : nullptr;
        CssRule hr_style =
            hr_inline + effective_inline->get("hr", 2, hr_id_p, hr_id_sv.length, hr_cls_p, hr_class_sv.length) +
            eff_ext.get("hr", 2, hr_id_p, hr_id_sv.length, hr_cls_p, hr_class_sv.length);
        std::optional<uint8_t> hr_width;
        if (hr_style.has_width_pct_)
          hr_width = hr_style.width_pct;
        parser.push_hr(std::nullopt, hr_width);
        parser.depth++;  // balance synthetic EndElement for self-closing tag
        continue;
      }

      // Hyperlink: capture href attribute for inline link annotation.
      // Only track the outermost <a> (nested <a> is invalid HTML, ignore inner ones).
      if (sv_eq(ev.name, "a") && parser.current_href_.empty()) {
        auto href_sv = ev.attrs.get("href");
        if (!href_sv.empty() && !(href_sv.length > 0 && href_sv.data[0] == '#')) {
          // Flush any text accumulated before this <a> so it gets no href.
          parser.flush_text(false);
          std::string resolved = Epub::resolve_path(base_dir, sv_to_string(href_sv));
          // Strip the fragment (everything from '#' onwards) for the stored href.
          // We keep the full resolved path so the reader can map filename → chapter index.
          auto frag_pos = resolved.find('#');
          std::string path_part = (frag_pos != std::string::npos) ? resolved.substr(0, frag_pos) : resolved;
          std::string frag_part = (frag_pos != std::string::npos) ? resolved.substr(frag_pos + 1) : "";
          // Store as "path|fragment" (fragment may be empty).
          parser.current_href_ = path_part + "|" + frag_part;
          parser.href_depth_ = parser.depth + 1;  // depth after the push below
        }
      }

      // Get CSS rule (before block flush, so we can check for float)
      auto id_sv = ev.attrs.get("id");
      auto class_sv = ev.attrs.get("class");
      auto style_sv = ev.attrs.get("style");

      CssRule inline_rule;
      if (!style_sv.empty()) {
        inline_rule = CssRule::parse(style_sv.data, style_sv.length, eff_ext.config_or(CssConfig{}));
      }

      const char* el_p = ev.name.data ? ev.name.data : "";
      size_t el_len = ev.name.length;
      const char* id_p = id_sv.data ? id_sv.data : nullptr;
      size_t id_len = id_sv.length;
      const char* cls_p = class_sv.data ? class_sv.data : nullptr;
      size_t cls_len = class_sv.length;

#ifdef ESP_PLATFORM
      int64_t css_t0 = esp_timer_get_time();
#endif
      CssRule style = inline_rule + effective_inline->get(el_p, el_len, id_p, id_len, cls_p, cls_len) +
                      eff_ext.get(el_p, el_len, id_p, id_len, cls_p, cls_len);
#ifdef ESP_PLATFORM
      css_us += esp_timer_get_time() - css_t0;
#endif

      // Apply browser-default margins for elements that usually have them.
      // Only apply if no explicit margin-left was set by CSS.
      if (!style.has_margin_left_) {
        if (sv_eq(ev.name, "blockquote")) {
          style.set_margin_left(36);  // ~3em default indent for blockquotes
        } else if (sv_eq(ev.name, "dd")) {
          style.set_margin_left(24);  // ~2em default indent for definition descriptions
        } else if (sv_eq(ev.name, "ul") || sv_eq(ev.name, "ol")) {
          // Only indent nested lists (level 2+); top-level lists are flush left
          if (!parser.list_stack_.empty())
            style.set_margin_left(16);  // ~1.3em default indent for nested lists
        }
      }

      // figcaption defaults: small italic centered
      if (sv_eq(ev.name, "figcaption")) {
        if (!style.has_font_size_pct_)
          style.set_font_size_pct(80);
        if (!style.has_italic_)
          style.set_italic(true);
        if (!style.has_alignment_)
          style.set_alignment(Alignment::Center);
      }
      // figure defaults: centered
      if (sv_eq(ev.name, "figure")) {
        if (!style.has_alignment_)
          style.set_alignment(Alignment::Center);
      }

      bool is_floated = style.has_is_float_ && style.is_float;
      bool is_drop_cap_span = is_floated && !is_block_element(ev.name);
      bool is_hidden = style.has_is_hidden_ && style.is_hidden;

      // Skip hidden elements entirely
      if (is_hidden) {
        // Skip all content until matching end element
        int skip_depth = 1;
        while (reader.next_event(ev) == XmlError::Ok) {
          if (ev.type == XmlEventType::EndOfFile)
            break;
          if (ev.type == XmlEventType::StartElement)
            ++skip_depth;
          else if (ev.type == XmlEventType::EndElement) {
            --skip_depth;
            if (skip_depth == 0)
              break;
          }
        }
        continue;
      }

      // Table cells: inject a space separator instead of a paragraph break
      // so that <tr> content stays on one line (e.g. TOC rows).
      if (sv_eq(ev.name, "td") || sv_eq(ev.name, "th")) {
        parser.ensure_word_break();
        parser.cell_depth = parser.depth + 1;  // will be incremented below
      }

      // page-break-before: always
      if (style.has_page_break_before_ && style.page_break_before) {
        parser.push_page_break();
      }

      // Float elements (e.g. .figleft initial-cap images) are treated as inline.
      // Suppress flush for ALL block boundaries after a float closes until text arrives.
      // Inside a table cell, suppress block flushes so inner divs don't split the row.
      if (is_block_element(ev.name) && !is_floated && !parser.float_depth.has_value() &&
          !parser.cell_depth.has_value()) {
        if (!parser.merge_after_float_) {
          parser.flush_run();
        }
      }

      // Record element ID for TOC fragment resolution (after flush so emitted_count_ is up-to-date).
      if (id_sv.data && id_sv.length > 0 && parser.id_cb_) {
        parser.id_cb_(parser.id_cb_ctx_, id_sv.data, id_sv.length, parser.emitted_count_);
      }

      // A block element with border-top acts as a decorative rule (like <hr>).
      if (style.has_border_top_ && style.border_top && is_block_element(ev.name) && !parser.cell_depth.has_value()) {
        parser.push_hr(style.margin_top, style.has_width_pct_ ? std::optional<uint8_t>(style.width_pct) : std::nullopt);
        style.has_margin_top_ = false;
        ;  // consumed by Hr spacing_before; don't re-apply to pending_margin_top_
      }

      parser.depth++;

      // page-break-after: track depth to emit on close
      if (style.has_page_break_after_ && style.page_break_after) {
        parser.page_break_after_depths_.push_back(parser.depth);
      }

      // Track list context for bullet/number prefixes
      if (sv_eq(ev.name, "ul")) {
        bool no_pfx = style.has_list_style_none_ && style.list_style_none;
        parser.list_stack_.push_back({false, 0, parser.depth, no_pfx});
      } else if (sv_eq(ev.name, "ol")) {
        uint16_t start_val = 1;
        auto start_sv = ev.attrs.get("start");
        if (!start_sv.empty()) {
          start_val =
              static_cast<uint16_t>(std::max(1, std::atoi(std::string(start_sv.data, start_sv.length).c_str())));
        }
        bool no_pfx = style.has_list_style_none_ && style.list_style_none;
        parser.list_stack_.push_back({true, start_val, parser.depth, no_pfx});
      } else if (sv_eq(ev.name, "li") && !parser.list_stack_.empty()) {
        auto& ctx = parser.list_stack_.back();
        // Per-<li> CSS can also suppress via list-style-type: none
        bool no_pfx = ctx.no_prefix || (style.has_list_style_none_ && style.list_style_none);
        if (!no_pfx) {
          if (ctx.ordered) {
            parser.pending_list_prefix_ = std::to_string(ctx.counter) + ". ";
          } else {
            parser.pending_list_prefix_ = "\xe2\x80\xa2 ";  // "• " (U+2022 bullet + space)
          }
        }
        if (ctx.ordered)
          ctx.counter++;
      }

      if (is_bold(ev.name)) {
        if (!style.has_bold_) {
          parser.bold_stack_.push_back({parser.bold_, parser.depth});
          parser.set_bold(true);
        }
        uint8_t hs_pct = heading_size_pct(ev.name);
        if (hs_pct != 100) {
          parser.size_stack_.push_back({parser.font_size_pct_, parser.depth});
          parser.set_size_pct(hs_pct);
        }
      } else if (is_italic(ev.name)) {
        if (!style.has_italic_) {
          parser.italic_stack_.push_back({parser.italic_, parser.depth});
          parser.set_italic(true);
        }
      } else if (is_small_element(ev.name)) {
        parser.size_stack_.push_back({parser.font_size_pct_, parser.depth});
        parser.set_size_pct(80);  // Small
        if (sv_eq(ev.name, "sup")) {
          parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
          parser.set_vertical_align(VerticalAlign::Super);
        } else if (sv_eq(ev.name, "sub")) {
          parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
          parser.set_vertical_align(VerticalAlign::Sub);
        }
      } else if (is_breaking(ev.name)) {
        parser.break_line(sv_eq(ev.name, "br"));
      }

      // <pre>: preserve whitespace and line breaks
      if (sv_eq(ev.name, "pre") && !parser.pre_depth_.has_value()) {
        parser.pre_depth_ = parser.depth;
      }

      if (style.has_italic_) {
        parser.italic_stack_.push_back({parser.italic_, parser.depth});
        parser.set_italic(style.italic);
      }
      if (style.has_bold_) {
        parser.bold_stack_.push_back({parser.bold_, parser.depth});
        parser.set_bold(style.bold);
      }
      if (style.has_font_size_pct_) {
        parser.size_stack_.push_back({parser.font_size_pct_, parser.depth});
        parser.set_size_pct(style.font_size_pct);
      }
      if (style.has_alignment_ && !parser.cell_depth.has_value()) {
        parser.alignment_stack_.push_back({parser.alignment_, parser.depth});
        parser.alignment_ = std::make_optional<Alignment>(static_cast<Alignment>(style.alignment));
      }
      if (style.has_indent_ && !parser.cell_depth.has_value()) {
        parser.indent_ = style.indent;
      }
      if (style.has_margin_left_ && !parser.cell_depth.has_value()) {
        parser.margin_left_stack_.push_back({parser.margin_left_, parser.depth});
        parser.set_margin_left(parser.margin_left_ + style.margin_left);
      }
      if (style.has_margin_right_ && !parser.cell_depth.has_value()) {
        parser.margin_right_stack_.push_back({parser.margin_right_, parser.depth});
        parser.set_margin_right(parser.margin_right_ + style.margin_right);
      }
      if (style.has_margin_top_ && !parser.cell_depth.has_value()) {
        uint16_t mt = style.margin_top;
        // Collapse with any pending top margin (take max)
        if (parser.pending_margin_top_.has_value())
          parser.pending_margin_top_ = std::max(*parser.pending_margin_top_, mt);
        else
          parser.pending_margin_top_ = mt;
      }
      if (style.has_margin_bottom_ && !parser.cell_depth.has_value()) {
        parser.margin_bottom_stack_.push_back({style.margin_bottom, parser.depth});
      }
      if (style.has_font_variant_small_caps_) {
        parser.small_caps_stack_.push_back({parser.small_caps_, parser.depth});
        parser.small_caps_ = style.font_variant_small_caps;
      }
      if (style.has_text_transform_) {
        parser.transform_stack_.push_back({parser.text_transform_, parser.depth});
        parser.text_transform_ = style.text_transform;
      }
      if (style.has_line_height_pct_) {
        parser.line_height_stack_.push_back({parser.line_height_pct_, parser.depth});
        parser.line_height_pct_ = style.line_height_pct;
      }
      if (style.has_vertical_align_) {
        parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
        parser.set_vertical_align(style.vertical_align);
      }
      if (style.has_is_float_ && style.is_float) {
        if (is_drop_cap_span) {
          // <span float:left> — treat as text-based drop cap (single oversized char).
          // Don't enter float mode; just mark the depth and wait for a single character.
          parser.drop_cap_depth_ = parser.depth;
          parser.drop_cap_char_.reset();
          parser.drop_cap_size_pct_ = style.has_font_size_pct_
                                          ? std::min(style.font_size_pct, static_cast<uint8_t>(150))
                                          : static_cast<uint8_t>(150);
        } else {
          // Block float (e.g. <div class="figleft">) — existing float-image pipeline.
          if (parser.merge_after_float_) {
            parser.flush_run();
            parser.merge_after_float_ = false;
          }
          parser.float_depth = parser.depth;
          parser.float_had_text_ = false;
        }
      }

    } else if (ev.type == XmlEventType::EndElement) {
      if (is_block_element(ev.name) && !parser.float_depth.has_value() && !parser.merge_after_float_ &&
          !parser.cell_depth.has_value()) {
        parser.flush_run();
      }

      if (parser.depth > 0)
        parser.depth--;

      // Clear hyperlink context when the <a> element closes.
      if (!parser.current_href_.empty() && parser.depth < parser.href_depth_) {
        parser.flush_text(false);  // flush any pending text before clearing href
        parser.current_href_.clear();
        parser.href_depth_ = 0;
      }
      while (!parser.italic_stack_.empty() && parser.depth < parser.italic_stack_.back().depth) {
        parser.set_italic(parser.italic_stack_.back().prev_value);
        parser.italic_stack_.pop_back();
      }
      while (!parser.bold_stack_.empty() && parser.depth < parser.bold_stack_.back().depth) {
        parser.set_bold(parser.bold_stack_.back().prev_value);
        parser.bold_stack_.pop_back();
      }
      while (!parser.size_stack_.empty() && parser.depth < parser.size_stack_.back().depth) {
        parser.set_size_pct(parser.size_stack_.back().prev_value);
        parser.size_stack_.pop_back();
      }
      while (!parser.vertical_align_stack_.empty() && parser.depth < parser.vertical_align_stack_.back().depth) {
        parser.set_vertical_align(parser.vertical_align_stack_.back().prev_value);
        parser.vertical_align_stack_.pop_back();
      }
      if (parser.drop_cap_depth_.has_value() && parser.depth < *parser.drop_cap_depth_) {
        parser.drop_cap_depth_.reset();
        parser.drop_cap_char_.reset();
        parser.drop_cap_size_pct_.reset();
      }
      if (parser.float_depth.has_value() && parser.depth < *parser.float_depth) {
        parser.float_depth.reset();
        parser.has_trailing_space_ = false;
        // Only merge with next paragraph if this float had no text (image-only, like drop caps).
        // Floats with captions should flush as separate paragraphs.
        if (!parser.float_had_text_) {
          parser.merge_after_float_ = true;
        } else {
          parser.flush_run();
        }
      }
      if (parser.cell_depth.has_value() && parser.depth < *parser.cell_depth) {
        parser.cell_depth.reset();
      }
      if (parser.pre_depth_.has_value() && parser.depth < *parser.pre_depth_) {
        parser.pre_depth_.reset();
      }
      while (!parser.alignment_stack_.empty() && parser.depth < parser.alignment_stack_.back().depth) {
        parser.alignment_ = parser.alignment_stack_.back().prev_value;
        parser.alignment_stack_.pop_back();
      }
      while (!parser.margin_left_stack_.empty() && parser.depth < parser.margin_left_stack_.back().depth) {
        parser.set_margin_left(parser.margin_left_stack_.back().prev_value);
        parser.margin_left_stack_.pop_back();
      }
      while (!parser.margin_right_stack_.empty() && parser.depth < parser.margin_right_stack_.back().depth) {
        parser.set_margin_right(parser.margin_right_stack_.back().prev_value);
        parser.margin_right_stack_.pop_back();
      }
      while (!parser.list_stack_.empty() && parser.depth < parser.list_stack_.back().depth) {
        parser.list_stack_.pop_back();
      }
      while (!parser.small_caps_stack_.empty() && parser.depth < parser.small_caps_stack_.back().depth) {
        parser.small_caps_ = parser.small_caps_stack_.back().prev_value;
        parser.small_caps_stack_.pop_back();
      }
      while (!parser.transform_stack_.empty() && parser.depth < parser.transform_stack_.back().depth) {
        parser.text_transform_ = parser.transform_stack_.back().prev_value;
        parser.transform_stack_.pop_back();
      }
      while (!parser.line_height_stack_.empty() && parser.depth < parser.line_height_stack_.back().depth) {
        parser.line_height_pct_ = parser.line_height_stack_.back().prev_value;
        parser.line_height_stack_.pop_back();
      }
      while (!parser.page_break_after_depths_.empty() && parser.depth < parser.page_break_after_depths_.back()) {
        parser.page_break_after_depths_.pop_back();
        parser.push_page_break();
      }
      while (!parser.margin_bottom_stack_.empty() && parser.depth < parser.margin_bottom_stack_.back().depth) {
        uint16_t mb = parser.margin_bottom_stack_.back().value;
        parser.margin_bottom_stack_.pop_back();
        // Collapse: take max of pending bottom margins
        if (parser.pending_margin_bottom_.has_value())
          parser.pending_margin_bottom_ = std::max(*parser.pending_margin_bottom_, mb);
        else
          parser.pending_margin_bottom_ = mb;
      }

    } else if (ev.type == XmlEventType::Text) {
      // Skip whitespace-only text nodes inside floats and during merge-after-float
      if (parser.float_depth.has_value() || parser.merge_after_float_) {
        bool all_ws = true;
        for (size_t i = 0; i < ev.content.length; ++i) {
          if (!std::isspace(static_cast<unsigned char>(ev.content.data[i]))) {
            all_ws = false;
            break;
          }
        }
        if (all_ws)
          continue;
        // Non-whitespace text found: end merge-after-float mode
        parser.merge_after_float_ = false;
        if (parser.float_depth.has_value()) {
          parser.float_had_text_ = true;
        }
      }
      // Drop cap: capture single character inside <span float:left>
      if (parser.drop_cap_depth_.has_value() && !parser.drop_cap_char_.has_value()) {
        const char* d = ev.content.data;
        const char* end = d + ev.content.length;
        char32_t cp = 0;
        size_t cp_len = 0;
        if (end > d) {
          unsigned char b = static_cast<unsigned char>(*d);
          if (b < 0x80) {
            cp = b;
            cp_len = 1;
          } else if ((b & 0xE0) == 0xC0 && end - d >= 2) {
            cp = (b & 0x1F) << 6 | (d[1] & 0x3F);
            cp_len = 2;
          } else if ((b & 0xF0) == 0xE0 && end - d >= 3) {
            cp = ((b & 0x0F) << 12) | ((d[1] & 0x3F) << 6) | (d[2] & 0x3F);
            cp_len = 3;
          } else if ((b & 0xF8) == 0xF0 && end - d >= 4) {
            cp = ((b & 0x07) << 18) | ((d[1] & 0x3F) << 12) | ((d[2] & 0x3F) << 6) | (d[3] & 0x3F);
            cp_len = 4;
          }
        }
        // Accept only if the text node is exactly this one codepoint (no extra text)
        const char* rest = d + cp_len;
        bool rest_is_ws = true;
        for (const char* p = rest; p < end; ++p) {
          if (!std::isspace(static_cast<unsigned char>(*p))) {
            rest_is_ws = false;
            break;
          }
        }
        if (cp > 0 && cp_len > 0 && rest_is_ws) {
          parser.drop_cap_char_ = cp;
          parser.flush_text(false);
          Run dc;
          dc.text.assign(d, cp_len);
          dc.style = parser.style();
          dc.size_pct = *parser.drop_cap_size_pct_;
          parser.runs_.push_back(std::move(dc));
          // Push any trailing whitespace as normal text
          for (const char* p = rest; p < end; ++p)
            parser.push_text(p, 1);
          continue;
        }
        // Not a single-char drop cap — fall through to normal text handling
        parser.drop_cap_depth_.reset();
      }
      parser.push_text(ev.content.data, ev.content.length);
#ifdef ESP_PLATFORM
      text_bytes += ev.content.length;
#endif
    }
  }

#ifdef ESP_PLATFORM
  parser.emit_us_ = nullptr;
  ESP_LOGD("perf", "  head=%ldms xml=%ldms css=%ldms elem=%ldms end=%ldms text=%ldms emit=%ldms  events=%u txtB=%u",
           (long)(head_us / 1000), (long)(xml_us / 1000), (long)(css_us / 1000), (long)(elem_us / 1000),
           (long)(end_us / 1000), (long)(text_us / 1000), (long)(emit_us / 1000), (unsigned)event_count,
           (unsigned)text_bytes);
#endif

#ifdef MICROREADER_DIAG_STREAMING
  fprintf(stderr, "DIAG: events=%zu start=%zu end=%zu text=%zu exit=%s err=%d\n", event_count, start_count, end_count,
          text_count, exit_reason, (int)last_err);
#endif

  return EpubError::Ok;
}

EpubError parse_xhtml_body(const uint8_t* data, size_t size, const CssStylesheet* inline_css,
                           const CssStylesheet* extern_css, const std::string& base_dir, const ZipReader& zip,
                           std::vector<Paragraph>& out) {
  MemoryXmlInput input(data, size);
  // Use a buffer large enough for the full XHTML to avoid streaming boundary issues.
  std::vector<uint8_t> buf(std::max(size + 1, size_t(4096)));
  XmlReader reader;
  if (reader.open(input, buf.data(), buf.size()) != XmlError::Ok) {
    return EpubError::XmlError;
  }

  BodyParser parser;
  EpubError err = parse_xhtml_events(reader, inline_css, extern_css, nullptr, base_dir, zip, parser);
  if (err != EpubError::Ok)
    return err;

  out = parser.finish();
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Epub::parse_chapter
// ---------------------------------------------------------------------------

EpubError Epub::parse_chapter(IZipFile& file, size_t index, Chapter& out) const {
  if (index >= spine_.size())
    return EpubError::InvalidData;

  auto& spine_item = spine_[index];
  auto& entry = zip_.entry(spine_item.file_idx);

  // Find title from TOC
  out.title.reset();
  for (auto& toc_entry : toc_.entries) {
    if (toc_entry.file_idx == spine_item.file_idx) {
      out.title = toc_entry.label.to_string(toc_.pool);
      break;
    }
  }

  // Extract chapter XHTML
  std::vector<uint8_t> data;
  if (zip_.extract(file, entry, data) != ZipError::Ok) {
    return EpubError::ZipError;
  }

  // Determine base directory
  std::string base_dir;
  auto slash = entry.name.rfind('/');
  if (slash != std::string_view::npos) {
    base_dir = entry.name.substr(0, slash + 1);
  }

  // Allocate a work buffer for CSS extraction (desktop/test — heap is not fragmented here).
  static constexpr size_t kCssWorkBufSize = ZipEntryInput::kMinWorkBufSize + 1024;
  std::vector<uint8_t> css_work_buf(kCssWorkBufSize);
  CssContext ctx{&file, &zip_, &css_cache_, css_config_, &base_dir, css_cache_.current_gen(),
                 css_work_buf.data(), kCssWorkBufSize};

  MemoryXmlInput input(data.data(), data.size());
  std::vector<uint8_t> xml_buf_vec(std::max(data.size() + 1, size_t(4096)));
  XmlReader reader;
  if (reader.open(input, xml_buf_vec.data(), xml_buf_vec.size()) != XmlError::Ok)
    return EpubError::XmlError;

  BodyParser parser;
  EpubError err = parse_xhtml_events(reader, nullptr, nullptr, &ctx, base_dir, zip_, parser);
  if (err != EpubError::Ok)
    return err;

  out.paragraphs = parser.finish();
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Epub::parse_chapter_streaming
// ---------------------------------------------------------------------------

EpubError Epub::parse_chapter_streaming(IZipFile& file, size_t index, ParagraphSink sink, void* sink_ctx,
                                        uint8_t* work_buf, uint8_t* xml_buf, IdSink id_sink, void* id_sink_ctx) const {
  if (index >= spine_.size())
    return EpubError::InvalidData;

  auto& spine_item = spine_[index];
  auto& entry = zip_.entry(spine_item.file_idx);

  // Determine base directory
  std::string base_dir;
  auto slash = entry.name.rfind('/');
  if (slash != std::string_view::npos) {
    base_dir = entry.name.substr(0, slash + 1);
  }

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 2048;
  static constexpr size_t kXmlBufSize = 16384;

  uint8_t* work_ptr = work_buf;
  uint8_t* xml_ptr = xml_buf;

  // --- Pass 1: pre-scan the XHTML head to collect <link rel="stylesheet"> hrefs.
  // The ZipEntryInput is opened and closed here so that work_ptr is free for
  // CSS extraction afterwards — the same framebuffer work_buf serves both roles,
  // just not simultaneously.
  static constexpr size_t kMaxCssLinks = CssCache::kMaxEntries;
  static constexpr size_t kMaxPathLen = 128;
  char css_link_paths[kMaxCssLinks][kMaxPathLen];
  size_t css_link_lens[kMaxCssLinks] = {};
  size_t css_link_count = 0;

  {
    ZipEntryInput zip_scan;
    if (zip_scan.open(file, entry, work_ptr, kWorkBufSize) == ZipError::Ok) {
      XmlReader scan_reader;
      if (scan_reader.open(zip_scan, xml_ptr, kXmlBufSize) == XmlError::Ok) {
        XmlEvent ev;
        for (;;) {
          XmlError xerr = scan_reader.next_event(ev);
          if (xerr != XmlError::Ok && xerr != XmlError::BufferTooSmall)
            break;
          if (ev.type == XmlEventType::EndOfFile)
            break;
          if (ev.type == XmlEventType::StartElement) {
            if (sv_eq(ev.name, "body"))
              break;
            if (sv_eq(ev.name, "link") && css_link_count < kMaxCssLinks) {
              auto rel = ev.attrs.get("rel");
              auto href = ev.attrs.get("href");
              if (sv_eq(rel, "stylesheet") && href.length > 0) {
                std::string path = Epub::resolve_path(base_dir, sv_to_string(href));
                size_t len = std::min(path.size(), kMaxPathLen - 1);
                std::memcpy(css_link_paths[css_link_count], path.c_str(), len);
                css_link_lens[css_link_count] = len;
                ++css_link_count;
              }
            }
          }
        }
      }
    }
  }  // zip_scan destroyed → work_ptr is now free

  // Load CSS files using work_buf (free between the two streaming passes).
  uint32_t protect_gen = css_cache_.current_gen();
  for (size_t i = 0; i < css_link_count; ++i) {
    std::string path(css_link_paths[i], css_link_lens[i]);
    css_cache_.get_or_load(file, zip_, path, css_config_, protect_gen, work_ptr, kWorkBufSize);
  }

  // --- Pass 2: full XHTML streaming parse with CSS already cached.
  // ZipEntryInput uses work_ptr again; get_or_load calls during the head scan
  // will be cache hits and never call zip.extract, so no conflict.
  ZipEntryInput zip_input;
  if (zip_input.open(file, entry, work_ptr, kWorkBufSize) != ZipError::Ok)
    return EpubError::ZipError;

  XmlReader reader;
  if (reader.open(zip_input, xml_ptr, kXmlBufSize) != XmlError::Ok)
    return EpubError::XmlError;

  BodyParser parser;
  parser.set_sink(sink, sink_ctx);
  if (id_sink)
    parser.set_id_callback(id_sink, id_sink_ctx);
  parser.reserve_runs(512);

  // work_buf=nullptr: CSS is already cached; any remaining cache miss skips silently.
  CssContext ctx{&file, &zip_, &css_cache_, css_config_, &base_dir, protect_gen, nullptr, 0};

  EpubError err = parse_xhtml_events(reader, nullptr, nullptr, &ctx, base_dir, zip_, parser);
  if (err != EpubError::Ok)
    return err;

  parser.finish();
  return EpubError::Ok;
}

}  // namespace microreader
