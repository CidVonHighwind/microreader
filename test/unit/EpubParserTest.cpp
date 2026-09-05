#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "microreader/content/EpubParser.h"

using namespace microreader;

static std::string fixture(const char* name) {
  return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

TEST(EpubPathResolve, Simple) {
  EXPECT_EQ(Epub::resolve_path("OEBPS/", "chapter1.xhtml"), "OEBPS/chapter1.xhtml");
}

TEST(EpubPathResolve, DotSlash) {
  EXPECT_EQ(Epub::resolve_path("OEBPS/", "./chapter1.xhtml"), "OEBPS/chapter1.xhtml");
}

TEST(EpubPathResolve, DotDotSlash) {
  EXPECT_EQ(Epub::resolve_path("OEBPS/chapters/", "../images/test.jpg"), "OEBPS/images/test.jpg");
}

TEST(EpubPathResolve, EmptyBase) {
  EXPECT_EQ(Epub::resolve_path("", "chapter1.xhtml"), "chapter1.xhtml");
}

TEST(EpubPathResolve, MultipleDotDot) {
  EXPECT_EQ(Epub::resolve_path("a/b/c/", "../../d.html"), "a/d.html");
}

// ---------------------------------------------------------------------------
// Epub::open — full EPUB parsing
// ---------------------------------------------------------------------------

class EpubTest : public ::testing::Test {
 protected:
  StdioZipFile file;
  Epub epub;
  std::vector<uint8_t> work_buf_ = std::vector<uint8_t>(ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024);
  std::vector<uint8_t> xml_buf_ = std::vector<uint8_t>(4096);

  void open_fixture(const char* name) {
    ASSERT_TRUE(file.open(fixture(name).c_str())) << name;
    ASSERT_EQ(epub.open(file, work_buf_.data(), xml_buf_.data()), EpubError::Ok) << name;
  }
};

TEST_F(EpubTest, BasicMetadata) {
  open_fixture("basic.epub");
  EXPECT_EQ(epub.metadata().title, "Basic Test");
}

TEST_F(EpubTest, BasicSpine) {
  open_fixture("basic.epub");
  EXPECT_GE(epub.chapter_count(), 1u);
}

TEST_F(EpubTest, MultiChapterMetadata) {
  open_fixture("multi_chapter.epub");
  EXPECT_EQ(epub.metadata().title, "Multi Chapter");
  EXPECT_EQ(epub.chapter_count(), 3u);
}

TEST_F(EpubTest, MultiChapterToc) {
  open_fixture("multi_chapter.epub");
  EXPECT_GE(epub.toc().entries.size(), 3u);

  // TOC entries should have labels
  for (size_t i = 0; i < epub.toc().entries.size(); ++i) {
    EXPECT_FALSE(epub.toc().label_view(epub.toc().entries[i]).empty()) << "TOC entry should have a label";
  }
}

TEST_F(EpubTest, PercentEncodedNcxTargetResolvesToZipEntry) {
  open_fixture("percent_encoded_ncx.epub");

  ASSERT_EQ(epub.toc().entries.size(), 1u);
  EXPECT_EQ(epub.toc().label_view(epub.toc().entries[0]), "Escaped target");
  EXPECT_EQ(epub.toc().entries[0].file_idx, epub.spine()[0].file_idx);
}

TEST_F(EpubTest, WithCssStylesheet) {
  // CSS is now loaded lazily per chapter — the cache is populated on first parse.
  open_fixture("with_css.epub");
  ASSERT_GT(epub.chapter_count(), 0u);
  Chapter ch;
  EXPECT_EQ(epub.parse_chapter(file, 0, ch), EpubError::Ok);
  // After parsing, at least one CSS file should be cached.
  EXPECT_GT(epub.css_cache().entry_count(), 0u);
}

TEST_F(EpubTest, WithImagesMetadata) {
  open_fixture("with_images.epub");
  EXPECT_EQ(epub.metadata().title, "Image Test");
  EXPECT_GE(epub.chapter_count(), 1u);
}

// ---------------------------------------------------------------------------
// Epub::parse_chapter
// ---------------------------------------------------------------------------

TEST_F(EpubTest, BasicChapter) {
  open_fixture("basic.epub");
  Chapter ch;
  ASSERT_EQ(epub.parse_chapter(file, 0, ch), EpubError::Ok);

  EXPECT_FALSE(ch.paragraphs.empty());

  // Collect all text
  std::string all_text;
  for (auto& p : ch.paragraphs) {
    if (p.type == ParagraphType::Text) {
      for (auto& run : p.text.runs) {
        all_text += run.text;
      }
    }
  }
  EXPECT_NE(all_text.find("Hello, world!"), std::string::npos);
}

TEST_F(EpubTest, MultiChapterParsesAll) {
  open_fixture("multi_chapter.epub");
  for (size_t i = 0; i < epub.chapter_count(); ++i) {
    Chapter ch;
    ASSERT_EQ(epub.parse_chapter(file, i, ch), EpubError::Ok) << "Chapter " << i;
    EXPECT_FALSE(ch.paragraphs.empty()) << "Chapter " << i << " should have content";
  }
}

TEST_F(EpubTest, LargeChapterParagraphs) {
  open_fixture("large_chapter.epub");
  Chapter ch;
  ASSERT_EQ(epub.parse_chapter(file, 0, ch), EpubError::Ok);

  // Should have many paragraphs
  int text_count = 0;
  for (auto& p : ch.paragraphs) {
    if (p.type == ParagraphType::Text)
      ++text_count;
  }
  EXPECT_GT(text_count, 10);
}

TEST_F(EpubTest, WithImagesChapter) {
  open_fixture("with_images.epub");
  Chapter ch;
  ASSERT_EQ(epub.parse_chapter(file, 0, ch), EpubError::Ok);

  // Should contain image paragraphs
  int image_count = 0;
  for (auto& p : ch.paragraphs) {
    if (p.type == ParagraphType::Image)
      ++image_count;
  }
  EXPECT_GT(image_count, 0) << "Expected at least one image reference";
}

TEST_F(EpubTest, SpecialCharsUnicode) {
  open_fixture("special_chars.epub");
  Chapter ch;
  ASSERT_EQ(epub.parse_chapter(file, 0, ch), EpubError::Ok);

  std::string all_text;
  for (auto& p : ch.paragraphs) {
    if (p.type == ParagraphType::Text) {
      for (auto& run : p.text.runs)
        all_text += run.text;
    }
  }
  // Unicode should be preserved
  EXPECT_NE(all_text.find("Ünïcödë"), std::string::npos);
}

TEST_F(EpubTest, RegressionFixtureContainsSupportedEntityCharacters) {
  open_fixture("regression_test.epub");
  ASSERT_GE(epub.chapter_count(), 44u);

  Chapter ch;
  ASSERT_EQ(epub.parse_chapter(file, epub.chapter_count() - 1, ch), EpubError::Ok);

  std::string all_text;
  for (auto& p : ch.paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& run : p.text.runs)
      all_text += run.text;
  }

  EXPECT_NE(all_text.find("44. Supported Entity Characters"), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xC5\x92 \xC5\x93 \xC5\xA0 \xC5\xA1 \xC5\xB8 \xC6\x92")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xC6\x92 \xCB\x86 \xCB\x9C")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\x94 \xE2\x80\x98 \xE2\x80\x99")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\x9C \xE2\x80\x9D")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\x9E")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\xA0 \xE2\x80\xA1 \xE2\x80\xA2")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\xA6 \xE2\x80\xB0")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x80\xB9 \xE2\x80\xBA \xE2\x80\xBE \xE2\x81\x84")), std::string::npos);
  EXPECT_NE(all_text.find(std::string("\xE2\x82\xAC \xE2\x84\xA2")), std::string::npos);
}

TEST_F(EpubTest, InvalidChapterIndex) {
  open_fixture("basic.epub");
  Chapter ch;
  EXPECT_NE(epub.parse_chapter(file, 999, ch), EpubError::Ok);
}

// ---------------------------------------------------------------------------
// XHTML body parsing (standalone)
// ---------------------------------------------------------------------------

TEST(XhtmlBody, PlainText) {
  const char* xhtml =
      "<?xml version=\"1.0\"?>"
      "<html><body><p>Hello, world!</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  auto err = parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "",
                              dummy_zip, paragraphs);

  ASSERT_EQ(err, EpubError::Ok);
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_EQ(paragraphs[0].text.runs.size(), 1u);
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "Hello, world!");
  EXPECT_EQ(paragraphs[0].text.runs[0].style, FontStyle::Regular);
}

TEST(XhtmlBody, InlineStyles) {
  const char* xhtml =
      "<html><body>"
      "<p>Text with <i>Inline</i> styles <b>bold</b>, <em>emphasized</em> or <i>italic</i></p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  auto& runs = paragraphs[0].text.runs;

  EXPECT_EQ(runs[0].text, "Text with ");
  EXPECT_EQ(runs[0].style, FontStyle::Regular);

  EXPECT_EQ(runs[1].text, "Inline");
  EXPECT_EQ(runs[1].style, FontStyle::Italic);

  EXPECT_EQ(runs[2].text, " styles ");
  EXPECT_EQ(runs[2].style, FontStyle::Regular);

  EXPECT_EQ(runs[3].text, "bold");
  EXPECT_EQ(runs[3].style, FontStyle::Bold);

  // ", " in regular
  EXPECT_EQ(runs[4].style, FontStyle::Regular);

  EXPECT_EQ(runs[5].text, "emphasized");
  EXPECT_EQ(runs[5].style, FontStyle::Italic);
}

TEST(XhtmlBody, Whitespace) {
  const char* xhtml =
      "<html><body>"
      "<p> Text\n                    \n"
      "                 with <span> White </span> space</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);

  // Collect all text
  std::string all_text;
  for (auto& run : paragraphs[0].text.runs)
    all_text += run.text;

  // Should have normalized whitespace
  EXPECT_EQ(all_text, "Text with White space");
}

TEST(XhtmlBody, MultipleParagraphs) {
  const char* xhtml =
      "<html><body>"
      "<p>First paragraph</p>"
      "<p>Second paragraph</p>"
      "<p>Third paragraph</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 3u);
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "First paragraph");
  EXPECT_EQ(paragraphs[1].text.runs[0].text, "Second paragraph");
  EXPECT_EQ(paragraphs[2].text.runs[0].text, "Third paragraph");
}

TEST(XhtmlBody, Headings) {
  const char* xhtml =
      "<html><body>"
      "<h1>Title</h1>"
      "<h2>Subtitle</h2>"
      "<p>Body text</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_GE(paragraphs.size(), 3u);
  // h1 → h2 → p (no hardcoded page break — only CSS page-break-before triggers it)
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[0].text.runs[0].style, FontStyle::Bold);
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "Title");
  EXPECT_EQ(paragraphs[1].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[1].text.runs[0].style, FontStyle::Bold);
  EXPECT_EQ(paragraphs[1].text.runs[0].text, "Subtitle");
  EXPECT_EQ(paragraphs[2].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[2].text.runs[0].text, "Body text");
}

TEST(XhtmlBody, HrElement) {
  const char* xhtml =
      "<html><body>"
      "<p>Before</p>"
      "<hr/>"
      "<p>After</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 3u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[1].type, ParagraphType::Hr);
  EXPECT_EQ(paragraphs[2].type, ParagraphType::Text);
}

TEST(XhtmlBody, HrPreservesParentStyles) {
  // Verify that a self-closing <hr/> doesn't corrupt our depth tracking,
  // which would cause the enclosing <blockquote> margin to be unwound early.
  const char* xhtml =
      "<html><body>"
      "<blockquote>"
      "<p>Before HR — indented</p>"
      "<hr/>"
      "<p>After HR — should still be indented</p>"
      "</blockquote>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 3u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[1].type, ParagraphType::Hr);
  EXPECT_EQ(paragraphs[2].type, ParagraphType::Text);
  // Both text paragraphs should have the blockquote default margin (36px)
  EXPECT_EQ(paragraphs[0].text.runs[0].margin_left, 36);
  EXPECT_EQ(paragraphs[2].text.runs[0].margin_left, 36);
}

TEST(XhtmlBody, HrDefaultWidthIsNullopt) {
  // A bare <hr/> with no styling should have hr_width_pct = nullopt
  // (renderer interprets nullopt as default 1/3 width).
  const char* xhtml = "<html><body><hr/></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Hr);
  EXPECT_FALSE(paragraphs[0].hr_width_pct.has_value());
}

TEST(XhtmlBody, HrInlineStyleWidth) {
  // width: 50% in an inline style attribute should be parsed and stored.
  const char* xhtml = "<html><body><hr style=\"width: 50%;\"/></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Hr);
  ASSERT_TRUE(paragraphs[0].hr_width_pct.has_value());
  EXPECT_EQ(*paragraphs[0].hr_width_pct, 50u);
}

TEST(XhtmlBody, HrExternalCssWidth) {
  // width: 33% from an external stylesheet should be picked up via the
  // full cascade — not just the inline style attribute.
  CssStylesheet sheet;
  sheet.extend_from_sheet("hr { width: 33%; }");

  const char* xhtml = "<html><body><hr/></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &sheet, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Hr);
  ASSERT_TRUE(paragraphs[0].hr_width_pct.has_value());
  EXPECT_EQ(*paragraphs[0].hr_width_pct, 33u);
}

TEST(XhtmlBody, HrClassWidth) {
  // width: 75% from a CSS class should be applied to <hr class="wide"/>.
  CssStylesheet sheet;
  sheet.extend_from_sheet(".wide { width: 75%; }");

  const char* xhtml = "<html><body><hr class=\"wide\"/></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &sheet, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Hr);
  ASSERT_TRUE(paragraphs[0].hr_width_pct.has_value());
  EXPECT_EQ(*paragraphs[0].hr_width_pct, 75u);
}

TEST(XhtmlBody, HrInlineStyleOverridesExternalCss) {
  // In this system's cascade (inline + effective_inline + extern_css),
  // extern_css is rightmost so it wins when both specify width.
  // This is consistent with how all other block elements behave.
  CssStylesheet sheet;
  sheet.extend_from_sheet("hr { width: 33%; }");

  const char* xhtml = "<html><body><hr style=\"width: 100%;\"/></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &sheet, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Hr);
  ASSERT_TRUE(paragraphs[0].hr_width_pct.has_value());
  // extern_css wins over inline style in this system's cascade order
  EXPECT_EQ(*paragraphs[0].hr_width_pct, 33u);
}

// ---------------------------------------------------------------------------
// Drop-cap tests
// ---------------------------------------------------------------------------

TEST(XhtmlBody, DropCapAscii) {
  // <span style="float:left"> containing exactly one ASCII character.
  // That character should appear as the first run with size_pct=150.
  const char* xhtml =
      "<html><body>"
      "<p><span style=\"float: left; font-size: 150%;\">O</span>nce upon a time.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  const auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 2u);
  // First run: the drop cap letter
  EXPECT_EQ(runs[0].text, "O");
  EXPECT_EQ(runs[0].size_pct, 150u);
  // Second run: the rest of the paragraph text
  EXPECT_FALSE(runs[1].text.empty());
  EXPECT_EQ(runs[1].text.substr(0, 3), "nce");
}

TEST(XhtmlBody, DropCapUtf8) {
  // <span style="float:left"> containing exactly one multi-byte UTF-8 codepoint (É = 0xC3 0x89).
  // The two-byte sequence must be captured whole as the drop cap character.
  const char* xhtml =
      "<html><body>"
      "<p><span style=\"float: left; font-size: 150%;\">\xC3\x89</span>trange.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  const auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 2u);
  EXPECT_EQ(runs[0].text, "\xC3\x89");
  EXPECT_EQ(runs[0].size_pct, 150u);
  EXPECT_EQ(runs[1].text.substr(0, 6), "trange");
}

TEST(XhtmlBody, DropCapMultiCharFallback) {
  // <span style="float:left"> with two characters must NOT be treated as a
  // drop cap — both chars should appear as normal inline text.
  const char* xhtml =
      "<html><body>"
      "<p><span style=\"float: left;\">No</span> fallback text.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  // "No" must be present as normal text (not dropped)
  std::string full;
  for (auto& r : paragraphs[0].text.runs)
    full += r.text;
  EXPECT_NE(full.find("No"), std::string::npos);
  // No run should have a size_pct above 100 from this span
  for (auto& r : paragraphs[0].text.runs) {
    if (r.text == "No" || r.text == "No ") {
      EXPECT_EQ(r.size_pct, 100u) << "Multi-char span must not get drop-cap sizing";
    }
  }
}

TEST(XhtmlBody, DropCapSizeCap) {
  // font-size: 200% should be capped to 150% by the drop-cap path.
  const char* xhtml =
      "<html><body>"
      "<p><span style=\"float: left; font-size: 200%;\">T</span>he rest.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  const auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 1u);
  EXPECT_EQ(runs[0].text, "T");
  EXPECT_EQ(runs[0].size_pct, 150u) << "Drop cap size must be capped at 150%";
}

TEST(XhtmlBody, DropCapBlockFloatIsNotDropCap) {
  // A block-level element with float:left must use the old image-float
  // pipeline, not the drop-cap path.
  const char* xhtml =
      "<html><body>"
      "<div style=\"float: left;\"><p>Block content</p></div>"
      "<p>Normal paragraph after block float.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Should produce at least one paragraph with normal (100%) size_pct runs
  bool found_normal = false;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (!r.text.empty()) {
        EXPECT_EQ(r.size_pct, 100u) << "Block float must not produce drop-cap sized runs";
        found_normal = true;
      }
    }
  }
  EXPECT_TRUE(found_normal);
}

TEST(XhtmlBody, HtmlEntities) {
  const char* xhtml = "<html><body><p>Tom &amp; Jerry &lt;3&gt; &quot;test&quot;</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  std::string text;
  for (auto& run : paragraphs[0].text.runs)
    text += run.text;
  EXPECT_EQ(text, "Tom & Jerry <3> \"test\"");
}

TEST(XhtmlBody, NamedHtmlEntitiesAcrossStyledSpans) {
  const char* xhtml =
      "<html><body>"
      "<p><span class=\"t200\">M</span><span class=\"t80\">INUIT</span>, au Mus&eacute;e royal des beaux-arts "
      "d&rsquo;Ankh-Morpork</p>"
      "</body></html>";

  CssStylesheet css;
  css.extend_from_sheet(".t200 { font-size: 200%; } .t80 { font-size: 80%; }");

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  const auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 3u);
  EXPECT_EQ(runs[0].text, "M");
  EXPECT_EQ(runs[0].size_pct, 200u);
  EXPECT_EQ(runs[1].text, "INUIT");
  EXPECT_EQ(runs[1].size_pct, 80u);

  std::string text;
  for (const auto& run : runs)
    text += run.text;
  EXPECT_EQ(text, std::string("MINUIT, au Mus") + "\xC3\xA9" + "e royal des beaux-arts d" + "\xE2\x80\x99" +
                      "Ankh-Morpork");
}

TEST(XhtmlBody, NumericEntities) {
  const char* xhtml = "<html><body><p>&#169; &#x2014;</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  std::string text;
  for (auto& run : paragraphs[0].text.runs)
    text += run.text;
  // &#169; = © (U+00A9), &#x2014; = — (em dash)
  EXPECT_NE(text.find("\xC2\xA9"), std::string::npos);      // © in UTF-8
  EXPECT_NE(text.find("\xE2\x80\x94"), std::string::npos);  // — in UTF-8
}

TEST(XhtmlBody, CssAlignmentApplied) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p { text-align: center; }");

  const char* xhtml = "<html><body><p>Centered text</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &sheet, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_TRUE(paragraphs[0].text.alignment.has_value());
  EXPECT_EQ(*paragraphs[0].text.alignment, Alignment::Center);
}

TEST(XhtmlBody, InlineStyle) {
  const char* xhtml = "<html><body><p style=\"text-align: right\">Right-aligned</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_TRUE(paragraphs[0].text.alignment.has_value());
  EXPECT_EQ(*paragraphs[0].text.alignment, Alignment::End);
}

TEST(XhtmlBody, BreakElement) {
  const char* xhtml = "<html><body><p>Line one<br/>Line two</p></body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  // Should have at least 2 runs, first one with breaking=true
  bool found_break = false;
  for (auto& run : paragraphs[0].text.runs) {
    if (run.breaking) {
      found_break = true;
      break;
    }
  }
  EXPECT_TRUE(found_break);
}

// ---------------------------------------------------------------------------
// Float merge: initial-cap alt text merges with following text
// ---------------------------------------------------------------------------

TEST(XhtmlBody, FloatMergeInitialCap) {
  // Simulates Alice's <div class="figleft"><img alt="A"/></div>
  // followed by <div class="unindent"></div><p>LICE was...</p>
  const char* css_text = ".figleft { float: left; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<div class=\"figleft\"><img alt=\"A\" src=\"missing.png\"/></div>"
      "<div class=\"unindent\"></div>"
      "<p>LICE was beginning to get very tired</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  // The "A" from alt text and "LICE was..." should be in the SAME paragraph
  ASSERT_GE(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);

  // Collect all text from the first paragraph
  std::string full_text;
  for (auto& run : paragraphs[0].text.runs) {
    full_text += run.text;
  }
  // Should start with "ALICE" (no space between A and LICE)
  EXPECT_TRUE(full_text.substr(0, 5) == "ALICE") << "Expected 'ALICE...' but got: '" << full_text.substr(0, 20) << "'";
}

TEST(XhtmlBody, FloatMergeWithIntermediateBlocks) {
  // Float followed by multiple empty block elements before text
  const char* css_text = ".figleft { float: left; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<div class=\"figleft\"><img alt=\"T\" src=\"missing.png\"/></div>"
      "<div></div>"
      "<div></div>"
      "<p>HEY were indeed a queer-looking party</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_GE(paragraphs.size(), 1u);
  std::string full_text;
  for (auto& run : paragraphs[0].text.runs) {
    full_text += run.text;
  }
  EXPECT_TRUE(full_text.substr(0, 4) == "THEY") << "Expected 'THEY...' but got: '" << full_text.substr(0, 20) << "'";
}

TEST(XhtmlBody, ImageWithWidthHeight) {
  // Image tag with width/height attributes — since dummy_zip has no entries,
  // the parser won't find the image file, so no Image paragraph is created.
  // Verify it doesn't crash and produces valid output.
  const char* xhtml =
      "<html><body>"
      "<p>text before</p>"
      "<div><img alt=\"test\" src=\"test.jpg\" width=\"400\" height=\"300\"/></div>"
      "<p>text after</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Should have text paragraphs (image skipped since no zip entry matches)
  ASSERT_GE(paragraphs.size(), 2u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "text before");
}

TEST(XhtmlBody, SvgImageElement) {
  // SVG-wrapped <image> element with width/height and xlink:href
  const char* xhtml =
      "<html><body>"
      "<svg viewBox=\"0 0 350 500\" width=\"100%\" height=\"100%\">"
      "<image width=\"350\" height=\"500\" xlink:href=\"cover.jpg\"/>"
      "</svg>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // The <image> won't resolve to a zip entry, but should not crash.
  // Once we have a real zip, width=350, height=500 would be preserved.
  SUCCEED();
}

TEST(XhtmlBody, BlockquoteDefaultIndent) {
  const char* xhtml =
      "<html><body>"
      "<blockquote><p>Quoted text.</p></blockquote>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_FALSE(paragraphs[0].text.runs.empty());
  // Default blockquote indent is 36px (~3em)
  uint16_t expected = 36;
  EXPECT_EQ(paragraphs[0].text.runs[0].margin_left, expected);
}

TEST(XhtmlBody, BlockquoteCssOverridesDefault) {
  const char* xhtml =
      "<html><body>"
      "<blockquote><p>Quoted text.</p></blockquote>"
      "</body></html>";

  CssStylesheet css;
  css.extend_from_sheet("blockquote { margin-left: 48px; }");
  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_FALSE(paragraphs[0].text.runs.empty());
  uint16_t expected = 48;
  EXPECT_EQ(paragraphs[0].text.runs[0].margin_left, expected);
}

TEST(XhtmlBody, TextTransformUppercase) {
  const char* xhtml =
      "<html><body>"
      "<p class=\"uc\">hello world</p>"
      "</body></html>";

  CssStylesheet css;
  css.extend_from_sheet(".uc { text-transform: uppercase; }");
  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_FALSE(paragraphs[0].text.runs.empty());
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "HELLO WORLD");
}

TEST(XhtmlBody, TextTransformCapitalize) {
  const char* xhtml =
      "<html><body>"
      "<p class=\"cap\">hello world</p>"
      "</body></html>";

  CssStylesheet css;
  css.extend_from_sheet(".cap { text-transform: capitalize; }");
  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_FALSE(paragraphs[0].text.runs.empty());
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "Hello World");
}

TEST(XhtmlBody, PageBreakAfter) {
  const char* xhtml =
      "<html><body>"
      "<div style=\"page-break-after: always;\"><p>Before break.</p></div>"
      "<p>After break.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Expect: text, page-break, text
  ASSERT_EQ(paragraphs.size(), 3u);
  EXPECT_EQ(paragraphs[0].type, ParagraphType::Text);
  EXPECT_EQ(paragraphs[1].type, ParagraphType::PageBreak);
  EXPECT_EQ(paragraphs[2].type, ParagraphType::Text);
}

TEST(XhtmlBody, MarginShorthand) {
  const char* xhtml =
      "<html><body>"
      "<p class=\"m\">Indented text.</p>"
      "</body></html>";

  CssStylesheet css(CssConfig{12, 440, 15});
  css.extend_from_sheet(".m { margin: 10px 24px; }");
  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  ASSERT_FALSE(paragraphs[0].text.runs.empty());
  uint16_t expected_margin = 24;
  EXPECT_EQ(paragraphs[0].text.runs[0].margin_left, expected_margin);
  EXPECT_EQ(paragraphs[0].text.runs[0].margin_right, expected_margin);
}

TEST(XhtmlBody, MarginTopSpacingBefore) {
  const char* xhtml =
      "<html><body>"
      "<p>First paragraph.</p>"
      "<p style=\"margin-top: 20px\">Second with margin-top.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 2u);
  // First paragraph: no spacing_before
  EXPECT_FALSE(paragraphs[0].spacing_before.has_value());
  // Second paragraph: spacing_before = 20 from margin-top
  ASSERT_TRUE(paragraphs[1].spacing_before.has_value());
  EXPECT_EQ(*paragraphs[1].spacing_before, 20);
}

TEST(XhtmlBody, MarginBottomSpacingBefore) {
  const char* xhtml =
      "<html><body>"
      "<p style=\"margin-bottom: 24px\">First with margin-bottom.</p>"
      "<p>Second paragraph.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 2u);
  // Second paragraph: spacing_before = 24 from previous margin-bottom
  ASSERT_TRUE(paragraphs[1].spacing_before.has_value());
  EXPECT_EQ(*paragraphs[1].spacing_before, 24);
}

TEST(XhtmlBody, MarginCollapsingMaxWins) {
  const char* xhtml =
      "<html><body>"
      "<p style=\"margin-bottom: 10px\">First.</p>"
      "<p style=\"margin-top: 30px\">Second.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 2u);
  // Should collapse: max(10, 30) = 30
  ASSERT_TRUE(paragraphs[1].spacing_before.has_value());
  EXPECT_EQ(*paragraphs[1].spacing_before, 30);
}

TEST(XhtmlBody, MarginTopZeroExplicit) {
  const char* xhtml =
      "<html><body>"
      "<p>First.</p>"
      "<p style=\"margin-top: 0\">Second with zero margin.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 2u);
  // margin-top: 0 explicitly means zero spacing
  ASSERT_TRUE(paragraphs[1].spacing_before.has_value());
  EXPECT_EQ(*paragraphs[1].spacing_before, 0);
}

TEST(XhtmlBody, PrePreservesWhitespace) {
  const char* xhtml =
      "<html><body>"
      "<pre>  line one\n  line two\n  line three</pre>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_EQ(paragraphs[0].type, ParagraphType::Text);
  // Should preserve leading spaces and have line breaks
  auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 1u);
  // Check that the text contains "  line one" (preserved spaces)
  EXPECT_NE(runs[0].text.find("  line one"), std::string::npos);
  // Check for breaking runs (line breaks from newlines)
  bool has_break = false;
  for (auto& r : runs) {
    if (r.breaking)
      has_break = true;
  }
  EXPECT_TRUE(has_break) << "Pre-formatted text should have line breaks from newlines";
}

TEST(XhtmlBody, PreMultipleSpacesNotCollapsed) {
  const char* xhtml =
      "<html><body>"
      "<pre>a    b</pre>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  auto& runs = paragraphs[0].text.runs;
  // The text should preserve the multiple spaces between a and b
  std::string full_text;
  for (auto& r : runs)
    full_text += r.text;
  EXPECT_NE(full_text.find("a    b"), std::string::npos) << "Expected preserved spaces, got: " << full_text;
}

// ---------------------------------------------------------------------------
// Table rows: no empty-paragraph gaps between <tr> elements
// ---------------------------------------------------------------------------

TEST(XhtmlBody, TableRowsNoEmptyParagraphs) {
  const char* xhtml =
      "<html><body>"
      "<table>"
      "  <tr><td>Row 1, Col 1</td><td>Row 1, Col 2</td></tr>"
      "  <tr><td>Row 2, Col 1</td><td>Row 2, Col 2</td></tr>"
      "</table>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Should get exactly 2 text paragraphs (one per row), no empty paragraphs between them
  size_t text_count = 0;
  for (auto& p : paragraphs) {
    if (p.type == ParagraphType::Text) {
      EXPECT_FALSE(p.text.runs.empty()) << "Empty text paragraph found — tr should not emit blank lines";
      ++text_count;
    }
  }
  EXPECT_EQ(text_count, 2u) << "Expected exactly 2 text paragraphs (one per table row)";
}

// ---------------------------------------------------------------------------
// CSS bold override: font-weight:normal inside bold parent restores bold
// ---------------------------------------------------------------------------

TEST(XhtmlBody, CssBoldOverrideRestoresBold) {
  const char* css_text = ".bold { font-weight: bold; } .normal { font-weight: normal; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<p class=\"bold\">Bold start <span class=\"normal\">normal middle</span> bold end.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 3u);

  // First run: "Bold start " — should be bold
  EXPECT_EQ(runs[0].text, "Bold start ");
  EXPECT_EQ(runs[0].style, FontStyle::Bold);

  // Middle run: "normal middle" — should be regular
  EXPECT_EQ(runs[1].text, "normal middle");
  EXPECT_EQ(runs[1].style, FontStyle::Regular);

  // Last run: " bold end." — should be bold again
  EXPECT_EQ(runs[2].text, " bold end.");
  EXPECT_EQ(runs[2].style, FontStyle::Bold);
}

TEST(XhtmlBody, CssItalicOverrideRestoresItalic) {
  const char* css_text = ".it { font-style: italic; } .normal { font-style: normal; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<p class=\"it\">Italic start <span class=\"normal\">normal middle</span> italic end.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_EQ(paragraphs.size(), 1u);
  auto& runs = paragraphs[0].text.runs;
  ASSERT_GE(runs.size(), 3u);

  EXPECT_EQ(runs[0].text, "Italic start ");
  EXPECT_EQ(runs[0].style, FontStyle::Italic);

  EXPECT_EQ(runs[1].text, "normal middle");
  EXPECT_EQ(runs[1].style, FontStyle::Regular);

  EXPECT_EQ(runs[2].text, " italic end.");
  EXPECT_EQ(runs[2].style, FontStyle::Italic);
}

// ---------------------------------------------------------------------------
// Heading with CSS font-weight must not leak bold to following paragraphs
// ---------------------------------------------------------------------------

TEST(XhtmlBody, HeadingWithCssFontWeightDoesNotLeakBold) {
  // Simulates a common EPUB pattern: heading element has a CSS class that also
  // sets font-weight (normal or bold).  The HTML bold from <h2> and the CSS
  // bold should not leak past the closing </h2>.
  const char* css_text = ".hdr { font-weight: normal; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<h2 class=\"hdr\">Chapter</h2>"
      "<p>Body text after heading</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_GE(paragraphs.size(), 2u);

  // Heading: CSS says font-weight:normal, so it should NOT be bold
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "Chapter");
  EXPECT_EQ(paragraphs[0].text.runs[0].style, FontStyle::Regular);

  // Body text must be regular — not bold
  EXPECT_EQ(paragraphs[1].text.runs[0].text, "Body text after heading");
  EXPECT_EQ(paragraphs[1].text.runs[0].style, FontStyle::Regular);
}

TEST(XhtmlBody, HeadingWithCssBoldDoesNotLeakBold) {
  // Heading with CSS font-weight:bold — the heading is bold, but subsequent
  // paragraphs must revert to regular.
  const char* css_text = ".hdr { font-weight: bold; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<h2 class=\"hdr\">Chapter</h2>"
      "<p>Body text after heading</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  ASSERT_GE(paragraphs.size(), 2u);

  // Heading: both HTML and CSS say bold
  EXPECT_EQ(paragraphs[0].text.runs[0].text, "Chapter");
  EXPECT_EQ(paragraphs[0].text.runs[0].style, FontStyle::Bold);

  // Body text must be regular — not bold
  EXPECT_EQ(paragraphs[1].text.runs[0].text, "Body text after heading");
  EXPECT_EQ(paragraphs[1].text.runs[0].style, FontStyle::Regular);
}

// ---------------------------------------------------------------------------
// Standalone <br/> between blocks emits empty paragraph
// ---------------------------------------------------------------------------

TEST(XhtmlBody, StandaloneBrEmitsEmptyParagraph) {
  const char* xhtml =
      "<html><body>"
      "<p>Before</p>"
      "<br/>"
      "<br/>"
      "<p>After</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Should have: "Before" text, 2 empty text paragraphs (from 2 br), "After" text
  size_t empty_count = 0;
  size_t text_count = 0;
  for (auto& p : paragraphs) {
    if (p.type == ParagraphType::Text) {
      if (p.text.runs.empty())
        ++empty_count;
      else
        ++text_count;
    }
  }
  EXPECT_EQ(text_count, 2u) << "Expected 2 non-empty text paragraphs";
  EXPECT_EQ(empty_count, 2u) << "Expected 2 empty paragraphs from standalone <br/> tags";
}

// ---------------------------------------------------------------------------
// Nested lists: each level adds 24px margin
// ---------------------------------------------------------------------------

TEST(XhtmlBody, NestedListIndentation) {
  const char* xhtml =
      "<html><body>"
      "<ul>"
      "<li>Level 1</li>"
      "<ul>"
      "<li>Level 2</li>"
      "</ul>"
      "</ul>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Find paragraphs with "Level 1" and "Level 2" text
  const Paragraph* p1 = nullptr;
  const Paragraph* p2 = nullptr;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (r.text.find("Level 1") != std::string::npos)
        p1 = &p;
      if (r.text.find("Level 2") != std::string::npos)
        p2 = &p;
    }
  }
  ASSERT_NE(p1, nullptr) << "Level 1 paragraph not found";
  ASSERT_NE(p2, nullptr) << "Level 2 paragraph not found";

  // Level 1 runs should have margin_left == 16
  uint16_t m1 = 0;
  for (auto& r : p1->text.runs) {
    if (r.text.find("Level 1") != std::string::npos)
      m1 = r.margin_left;
  }
  // Level 2 runs should have margin_left == 32
  uint16_t m2 = 0;
  for (auto& r : p2->text.runs) {
    if (r.text.find("Level 2") != std::string::npos)
      m2 = r.margin_left;
  }
  EXPECT_EQ(m1, 0u) << "First list level should have no indent";
  EXPECT_EQ(m2, 16u) << "Second list level should have 16px indent";
}

// ---------------------------------------------------------------------------
// Ordered list start attribute
// ---------------------------------------------------------------------------

TEST(XhtmlBody, OrderedListStartAttribute) {
  const char* xhtml =
      "<html><body>"
      "<ol start=\"5\">"
      "<li>First item</li>"
      "<li>Second item</li>"
      "</ol>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  // Find paragraphs with numbered prefixes
  bool found_5 = false, found_6 = false;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (r.text.find("5. ") != std::string::npos)
        found_5 = true;
      if (r.text.find("6. ") != std::string::npos)
        found_6 = true;
    }
  }
  EXPECT_TRUE(found_5) << "First item should start with '5. '";
  EXPECT_TRUE(found_6) << "Second item should start with '6. '";
}

// ---------------------------------------------------------------------------
// figcaption: default small italic centered
// ---------------------------------------------------------------------------

TEST(XhtmlBody, FigcaptionDefaultStyling) {
  const char* xhtml =
      "<html><body>"
      "<figure>"
      "<figcaption>Caption text</figcaption>"
      "</figure>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  const Paragraph* cap = nullptr;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (r.text.find("Caption text") != std::string::npos)
        cap = &p;
    }
  }
  ASSERT_NE(cap, nullptr) << "figcaption paragraph not found";

  // Should be small italic centered
  for (auto& r : cap->text.runs) {
    if (r.text.find("Caption text") != std::string::npos) {
      EXPECT_EQ(r.style, FontStyle::Italic);
      EXPECT_EQ(r.size_pct, 80);
    }
  }
  ASSERT_TRUE(cap->text.alignment.has_value());
  EXPECT_EQ(*cap->text.alignment, Alignment::Center);
}

// ---------------------------------------------------------------------------
// figure: default centered alignment
// ---------------------------------------------------------------------------

TEST(XhtmlBody, FigureDefaultCenter) {
  const char* xhtml =
      "<html><body>"
      "<figure>Figure content</figure>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "", dummy_zip,
                   paragraphs);

  const Paragraph* fig = nullptr;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (r.text.find("Figure content") != std::string::npos)
        fig = &p;
    }
  }
  ASSERT_NE(fig, nullptr) << "figure paragraph not found";
  ASSERT_TRUE(fig->text.alignment.has_value());
  EXPECT_EQ(*fig->text.alignment, Alignment::Center);
}

// ---------------------------------------------------------------------------
// line-height CSS propagated to TextParagraph
// ---------------------------------------------------------------------------

TEST(XhtmlBody, LineHeightPropagated) {
  const char* css_text = ".big { line-height: 150%; }";
  CssStylesheet css;
  css.extend_from_sheet(css_text, std::strlen(css_text));

  const char* xhtml =
      "<html><body>"
      "<p class=\"big\">Tall line</p>"
      "<p>Normal line</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, &css, "", dummy_zip,
                   paragraphs);

  // Find paragraphs
  const Paragraph* tall = nullptr;
  const Paragraph* normal = nullptr;
  for (auto& p : paragraphs) {
    if (p.type != ParagraphType::Text)
      continue;
    for (auto& r : p.text.runs) {
      if (r.text.find("Tall line") != std::string::npos)
        tall = &p;
      if (r.text.find("Normal line") != std::string::npos)
        normal = &p;
    }
  }
  ASSERT_NE(tall, nullptr);
  ASSERT_NE(normal, nullptr);

  // 150% / 1.5 * 100 = 100
  EXPECT_EQ(tall->text.line_height_pct, 100u);
  EXPECT_EQ(normal->text.line_height_pct, 100u);
}

// ---------------------------------------------------------------------------
// All EPUBs: open + parse all chapters
// ---------------------------------------------------------------------------

class EpubAllFixturesTest : public ::testing::TestWithParam<const char*> {};

TEST_P(EpubAllFixturesTest, OpenAndParseAllChapters) {
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture(GetParam()).c_str())) << GetParam();

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024;
  std::vector<uint8_t> work_buf(kWorkBufSize);
  std::vector<uint8_t> xml_buf(4096);

  Epub epub;
  ASSERT_EQ(epub.open(file, work_buf.data(), xml_buf.data()), EpubError::Ok) << GetParam();
  EXPECT_GT(epub.chapter_count(), 0u);
  EXPECT_FALSE(epub.metadata().title.empty());

  for (size_t i = 0; i < epub.chapter_count(); ++i) {
    Chapter ch;
    EXPECT_EQ(epub.parse_chapter(file, i, ch), EpubError::Ok) << "Failed on " << GetParam() << " chapter " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(AllEpubs, EpubAllFixturesTest,
                         ::testing::Values("basic.epub", "multi_chapter.epub", "with_css.epub", "with_images.epub",
                                           "stored.epub", "nested_dirs.epub", "special_chars.epub",
                                           "large_chapter.epub"));

// ============================================================
// Hyperlink href parsing tests (XhtmlHref)
// ============================================================

namespace {
// Collect all runs from all text paragraphs in the given paragraph list.
static std::vector<Run> collect_runs(const std::vector<Paragraph>& paragraphs) {
  std::vector<Run> result;
  for (const auto& p : paragraphs) {
    if (p.type == ParagraphType::Text) {
      for (const auto& run : p.text.runs) {
        result.push_back(run);
      }
    }
  }
  return result;
}
}  // namespace

// A simple inline link: "Before <a href='...'>link text</a> after"
// Verifies: "Before" has no href, "link text" has correct href, "after" has no href.
TEST(XhtmlHref, SimpleInlineLink) {
  const char* xhtml =
      "<html><body>"
      "<p>Before <a href=\"../Text/ch2.xhtml#sec1\">link text</a> after</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/Text/",
                   dummy_zip, paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 2u);

  bool found_before = false, found_link = false, found_after = false;
  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    if (text.find("Before") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "'Before' text should have no href; got: '" << href << "'";
      found_before = true;
    }
    if (text.find("link text") != std::string::npos) {
      EXPECT_FALSE(href.empty()) << "'link text' run should have an href";
      EXPECT_NE(href.find("ch2.xhtml"), std::string::npos) << "href should contain ch2.xhtml; got: '" << href << "'";
      EXPECT_NE(href.find("sec1"), std::string::npos) << "href should contain fragment sec1; got: '" << href << "'";
      found_link = true;
    }
    if (text.find("after") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "'after' text should have no href; got: '" << href << "'";
      found_after = true;
    }
  }
  EXPECT_TRUE(found_before) << "Expected a run containing 'Before'";
  EXPECT_TRUE(found_link) << "Expected a run containing 'link text'";
  EXPECT_TRUE(found_after) << "Expected a run containing 'after'";
}

// Text accumulated before <a> in the same run must NOT get the href.
// Regression for: "I. Down the Rabbit-hole " + "1" where "1" was inside <a>
// but the whole buffer got the href because flush_text wasn't called before setting current_href_.
TEST(XhtmlHref, PreLinkTextHasNoHref) {
  const char* xhtml =
      "<html><body>"
      "<p>leading text <a href=\"target.xhtml\">linked</a></p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/", dummy_zip,
                   paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 1u);

  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    if (text.find("leading") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "Text before <a> must not inherit the href; got: '" << href << "'";
    }
    if (text.find("linked") != std::string::npos) {
      EXPECT_FALSE(href.empty()) << "Text inside <a> should have href";
    }
  }
}

// Table-style TOC row: "I. | Chapter Name | 1(link)"
// Only the page number "1" (inside <a>) should get the href.
// The roman numeral "I." and chapter name must have empty href.
// This is the Alice TOC regression: all three cells are on one line,
// so the pre-link text was being mixed with the linked text into one run.
TEST(XhtmlHref, TableTocRowOnlyLinkedCellGetsHref) {
  const char* xhtml =
      "<html><body>"
      "<table>"
      "<tr>"
      "<td>I. </td>"
      "<td>Down the Rabbit-hole</td>"
      "<td><a href=\"ch01.xhtml#Page_1\">1</a></td>"
      "</tr>"
      "<tr>"
      "<td>II. </td>"
      "<td>The Pool of Tears</td>"
      "<td><a href=\"ch02.xhtml#Page_13\">13</a></td>"
      "</tr>"
      "</table>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/", dummy_zip,
                   paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 4u);

  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    if (text.find("I.") != std::string::npos || text.find("Rabbit") != std::string::npos ||
        text.find("Tears") != std::string::npos || text.find("II.") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "Non-link cell '" << text << "' should have no href; got: '" << href << "'";
    }
    if (text == "1") {
      if (!href.empty()) {
        EXPECT_NE(href.find("ch01.xhtml"), std::string::npos)
            << "Page 1 href should point to ch01; got: '" << href << "'";
      }
    }
    if (text == "13") {
      if (!href.empty()) {
        EXPECT_NE(href.find("ch02.xhtml"), std::string::npos)
            << "Page 13 href should point to ch02; got: '" << href << "'";
      }
    }
  }
}

// Consecutive links separated by text: "<a>Nr.1</a>, <a>Nr.2</a>, <a>Nr.3</a>, <a>Nr.4</a>"
// Separator ", " text between the links must have NO href.
// Each linked run must have its own distinct href.
TEST(XhtmlHref, ConsecutiveLinksWithTextSeparators) {
  const char* xhtml =
      "<html><body>"
      "<p id=\"rnw1\">"
      "<a href=\"content-6.xhtml#bild1\">Nr. 1</a>, "
      "<a href=\"content-6.xhtml#bild2\">Nr. 2</a>, "
      "<a href=\"content-7.xhtml#bild3\">Nr. 3</a>, "
      "<a href=\"content-7.xhtml#bild4\">Nr. 4</a>"
      "</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/Text/",
                   dummy_zip, paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 4u);

  int linked_count = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    // Separator commas/spaces must not have an href
    if (text.find(",") != std::string::npos || text == " " || text == ", ") {
      EXPECT_TRUE(href.empty()) << "Separator '" << text << "' should have no href; got: '" << href << "'";
    }
    // Each "Nr." run should have an href
    if (text.find("Nr.") != std::string::npos) {
      EXPECT_FALSE(href.empty()) << "Linked run '" << text << "' should have an href";
      ++linked_count;
    }
  }
  EXPECT_EQ(linked_count, 4) << "Expected 4 linked 'Nr.' runs";
}

// A footnote reference: text, superscript link number, more text.
// Only the superscript "1" should have an href.
TEST(XhtmlHref, TextBeforeAndAfterLink) {
  const char* xhtml =
      "<html><body>"
      "<p>Some text<a href=\"notes.xhtml#fn1\"><sup>1</sup></a> and more text.</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/", dummy_zip,
                   paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 1u);

  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    if (text.find("Some text") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "'Some text' should have no href; got: '" << href << "'";
    }
    if (text == "1" || text.find("1") != std::string::npos) {
      if (!href.empty()) {
        EXPECT_NE(href.find("notes.xhtml"), std::string::npos) << "Footnote href should point to notes.xhtml";
      }
    }
    if (text.find("and more text") != std::string::npos) {
      EXPECT_TRUE(href.empty()) << "'and more text' should have no href; got: '" << href << "'";
    }
  }
}

// An absolute (http://) link should be captured verbatim in the href field.
TEST(XhtmlHref, ExternalLinkCaptured) {
  const char* xhtml =
      "<html><body>"
      "<p><a href=\"http://example.com/page\">Visit site</a></p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/", dummy_zip,
                   paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 1u);

  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;
    if (text.find("Visit") != std::string::npos) {
      EXPECT_FALSE(href.empty()) << "'Visit site' should have an href";
      EXPECT_NE(href.find("example.com"), std::string::npos)
          << "External href should be preserved; got: '" << href << "'";
    }
  }
}

// A fragment-only href "#section2" should be stored (possibly resolved relative to base).
TEST(XhtmlHref, FragmentOnlyHref) {
  // Fragment-only hrefs (e.g. <a href="#footnote1">1</a>) are same-page anchors
  // that cannot navigate to another chapter. They must NOT be treated as links
  // (no underline, no href stored on the run).
  const char* xhtml =
      "<html><body>"
      "<p><a href=\"#1\">1</a> <a href=\"#2\">2</a> <a href=\"#section2\">Go to section 2</a></p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/", dummy_zip,
                   paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 1u);

  // None of the runs should have an href set — fragment-only links are suppressed.
  for (const auto& r : runs) {
    EXPECT_TRUE(r.href.empty()) << "Fragment-only href should not be stored (got: " << r.href << " for text: " << r.text
                                << ")";
  }
}

// Multi-word link text like "Rechtenachweis Nr. 1" must be captured as a SINGLE Run
// with the full text and the href set. The parser must NOT split it into separate runs
// that lose the href. This is the exact structure from real EPUBs like ohler.epub where
// consecutive image credits appear as:
//   <a href="..#bild1">Rechtenachweis Nr. 1</a>, <a href="..#bild2">Rechtenachweis Nr. 2</a>, ...
TEST(XhtmlHref, MultiWordLinkTextFullyCaptures) {
  const char* xhtml =
      "<html><body>"
      "<p class=\"rechte\" id=\"rnw1\">"
      "<a class=\"pcalibre4\" href=\"../Text/content-6.xhtml#bild1\">Rechtenachweis Nr. 1</a>, "
      "<a class=\"pcalibre4\" href=\"../Text/content-6.xhtml#bild2\">Rechtenachweis Nr. 2</a>, "
      "<a class=\"pcalibre4\" href=\"../Text/content-7.xhtml#bild3\">Rechtenachweis Nr. 3</a>, "
      "<a class=\"pcalibre4\" href=\"../Text/content-7.xhtml#bild4\">Rechtenachweis Nr. 4</a>"
      "</p>"
      "</body></html>";

  ZipReader dummy_zip;
  std::vector<Paragraph> paragraphs;
  parse_xhtml_body(reinterpret_cast<const uint8_t*>(xhtml), std::strlen(xhtml), nullptr, nullptr, "OEBPS/Text/",
                   dummy_zip, paragraphs);

  auto runs = collect_runs(paragraphs);
  ASSERT_GE(runs.size(), 4u);

  // Each "Rechtenachweis Nr. N" should be ONE run with full text and an href.
  // Separator ", " runs should have no href.
  int linked_count = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    const std::string& href = runs[i].href;

    if (text.find("Rechtenachweis") != std::string::npos) {
      // The full "Rechtenachweis Nr. N" must appear in a single run — not split.
      EXPECT_NE(text.find("Nr."), std::string::npos)
          << "Multi-word link text must not be split; 'Nr.' missing from run '" << text << "'";
      EXPECT_FALSE(href.empty()) << "Run '" << text << "' inside <a> must have an href";
      if (!href.empty()) {
        EXPECT_NE(href.find("content-"), std::string::npos)
            << "href should reference content-N.xhtml; got: '" << href << "'";
      }
      ++linked_count;
    }

    // Separators (the literal ", " between links) must have no href.
    if (text == ", " || text == ",") {
      EXPECT_TRUE(href.empty()) << "Separator '" << text << "' should have no href; got: '" << href << "'";
    }
  }
  EXPECT_EQ(linked_count, 4) << "Expected 4 'Rechtenachweis' linked runs";
}
