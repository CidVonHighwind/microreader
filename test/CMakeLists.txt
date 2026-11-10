cmake_minimum_required(VERSION 3.16)
project(microreader_tests CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Auto-generate version.h
execute_process(COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_version.py)

# Fix MSVC parallel PDB writes
if(MSVC)
  add_compile_options(/FS)
endif()

# Allow older subprojects (miniz) to configure without error on CMake 4.x
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched at configure time)
# ---------------------------------------------------------------------------
include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# miniz (vendored in third_party/miniz/ — all .c files compiled as separate TUs)
set(MINIZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniz)
add_library(miniz STATIC
    ${MINIZ_DIR}/miniz.c
    ${MINIZ_DIR}/miniz_tdef.c
    ${MINIZ_DIR}/miniz_tinfl.c
    ${MINIZ_DIR}/miniz_zip.c
)
target_include_directories(miniz PUBLIC ${MINIZ_DIR})

# Google Benchmark
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# ---------------------------------------------------------------------------
# Core content library (the code under test)
# ---------------------------------------------------------------------------
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader)

add_library(microreader_content STATIC
    ${LIB_DIR}/content/HtmlEntities.cpp
    ${LIB_DIR}/content/ZipReader.cpp
    ${LIB_DIR}/content/XmlReader.cpp
    ${LIB_DIR}/content/CssParser.cpp
    ${LIB_DIR}/content/EpubParser.cpp
    ${LIB_DIR}/content/ImageDecoder.cpp
    ${LIB_DIR}/content/JpegDecoder.cpp
    ${LIB_DIR}/content/PngDecoder.cpp
    ${LIB_DIR}/content/BmpSleepConverter.cpp
    ${LIB_DIR}/content/Book.cpp
    ${LIB_DIR}/content/BookIndex.cpp
    ${LIB_DIR}/content/TextLayout.cpp
    ${LIB_DIR}/content/hyphenation/Hyphenation.cpp
    ${LIB_DIR}/content/mrb/MrbWriter.cpp
    ${LIB_DIR}/content/mrb/MrbReader.cpp
    ${LIB_DIR}/content/mrb/MrbConverter.cpp
    # Reading statistics (platform-agnostic, no screen deps)
    ${LIB_DIR}/stats/ReadingStats.cpp
    ${LIB_DIR}/library/FinishedBooks.cpp
)
target_include_directories(microreader_content PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib
)
target_link_libraries(microreader_content PUBLIC miniz)

# ---------------------------------------------------------------------------
# Platform-agnostic core library used by tests needing screens / reader logic
# ---------------------------------------------------------------------------
add_library(microreader_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/Application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/Loop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/demo/BouncingBallDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/demo/GrayscaleDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/ListMenuScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/MainMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/ReaderScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/SettingsScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/ChapterSelectScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/ReaderOptionsScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/LinksScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/StatsCalendarScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/DateTimeScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/FinishedBooksScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/microreader/screens/SleepScreens.cpp
)
target_include_directories(microreader_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib
)
target_link_libraries(microreader_core PUBLIC microreader_content)

# ---------------------------------------------------------------------------
# Fast unit tests (no real EPUB files, runs in milliseconds)
# ---------------------------------------------------------------------------
add_executable(unit_tests
    unit/ZipReaderTest.cpp
    unit/XmlReaderTest.cpp
    unit/CssParserTest.cpp
    unit/EpubParserTest.cpp
    unit/ImageDecoderTest.cpp
    unit/TextLayoutParaTest.cpp
    unit/LaidOutParagraphTest.cpp
    unit/PageLayoutTest.cpp
    unit/BitmapFontTest.cpp
    unit/MrbFormatTest.cpp
    unit/MrbLayoutComparisonTest.cpp
    unit/StreamingPipelineTest.cpp
    unit/NavigationTest.cpp
    unit/HyphenationTest.cpp
    unit/BmpSleepConverterTest.cpp
    unit/BookIndexTest.cpp
    unit/ReadingStatsTest.cpp
    unit/FinishedBooksTest.cpp
)
target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(unit_tests PRIVATE
    microreader_content
    gtest
    gtest_main
)
target_compile_definitions(unit_tests PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    SMOKE_TESTS_ONLY
)

# ---------------------------------------------------------------------------
# Full test executable (includes slow book integration tests)
# ---------------------------------------------------------------------------
add_executable(microreader_tests
    unit/ZipReaderTest.cpp
    unit/XmlReaderTest.cpp
    unit/CssParserTest.cpp
    unit/EpubParserTest.cpp
    unit/ImageDecoderTest.cpp
    unit/TextLayoutParaTest.cpp
    unit/LaidOutParagraphTest.cpp
    unit/PageLayoutTest.cpp
    unit/BitmapFontTest.cpp
    unit/MrbFormatTest.cpp
    unit/MrbLayoutComparisonTest.cpp
    unit/StreamingPipelineTest.cpp
    unit/NavigationTest.cpp
    unit/BmpSleepConverterTest.cpp
    integration/RealBookTest.cpp
    integration/BulkBookTest.cpp
    integration/HtmlExportTest.cpp
    integration/DebugLayoutTest.cpp
    integration/DitherComparisonTest.cpp
    integration/LinkNavigationTest.cpp
    integration/HrBackwardTest.cpp
    unit/LinksScreenTest.cpp
    unit/BookIndexTest.cpp
    unit/ReadingStatsTest.cpp
    unit/FinishedBooksTest.cpp
    unit/StatsScreensTest.cpp
    unit/NewFeaturesTest.cpp
)
target_include_directories(microreader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
target_link_libraries(microreader_tests PRIVATE
    microreader_content
    microreader_core
    gtest
    gtest_main
)

# Make test fixture path available as a compile definition
target_compile_definitions(microreader_tests PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

# Register with CTest
enable_testing()
add_test(NAME microreader_tests COMMAND microreader_tests)

# ---------------------------------------------------------------------------
# Benchmarks (Google Benchmark)
# ---------------------------------------------------------------------------
add_executable(conversion_bench
    bench/ConversionBench.cpp
)
target_include_directories(conversion_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
target_link_libraries(conversion_bench PRIVATE
    microreader_content
    benchmark::benchmark
)
target_compile_definitions(conversion_bench PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
