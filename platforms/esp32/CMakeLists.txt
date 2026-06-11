FILE(GLOB app_sources
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader)

set(lib_sources
    ${LIB}/Application.cpp
    ${LIB}/Loop.cpp
    ${LIB}/screens/demo/BouncingBallDemo.cpp
    ${LIB}/screens/demo/GrayscaleDemo.cpp
    ${LIB}/screens/ListMenuScreen.cpp
    ${LIB}/screens/MainMenu.cpp
    ${LIB}/screens/ReaderScreen.cpp
    ${LIB}/screens/SettingsScreen.cpp
    ${LIB}/screens/ChapterSelectScreen.cpp
    ${LIB}/screens/DeleteConfirmScreen.cpp
    ${LIB}/screens/LinksScreen.cpp
    ${LIB}/screens/ReaderOptionsScreen.cpp
    ${LIB}/content/ZipReader.cpp
    ${LIB}/content/XmlReader.cpp
    ${LIB}/content/CssParser.cpp
    ${LIB}/content/EpubParser.cpp
    ${LIB}/content/TextLayout.cpp
    ${LIB}/content/hyphenation/Hyphenation.cpp
    ${LIB}/content/Book.cpp
    ${LIB}/content/BookIndex.cpp
    ${LIB}/content/mrb/MrbWriter.cpp
    ${LIB}/content/mrb/MrbReader.cpp
    ${LIB}/content/mrb/MrbConverter.cpp
    ${LIB}/content/ImageDecoder.cpp
    ${LIB}/content/JpegDecoder.cpp
    ${LIB}/content/PngDecoder.cpp
    ${LIB}/content/BmpSleepConverter.cpp)

# miniz: vendored in third_party/miniz/. Compiled as C++ via miniz_wrapper.cpp
# to avoid IDF applying C++-only flags (-fuse-cxa-atexit) to .c files.
set(MINIZ_DIR ${CMAKE_CURRENT_LIST_DIR}/../../third_party/miniz)

idf_component_register(
    SRCS ${app_sources} ${lib_sources} ${CMAKE_CURRENT_LIST_DIR}/miniz_wrapper.cpp
         ${CMAKE_CURRENT_LIST_DIR}/assets_embedded.S
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../lib" "${MINIZ_DIR}"
    REQUIRES fatfs driver wear_levelling)

# IDF 5.x adds -Wno-old-style-declaration (C-only) globally; GCC -Werror rejects
# it when compiling C++ files. Remove both flags from the entire IDF build so that:
#   - C++ files: no longer get an invalid-for-C++ flag error
#   - C files with old-style declarations (e.g. esp_ota_ops.c): no longer error out
idf_build_get_property(compile_opts COMPILE_OPTIONS)
list(REMOVE_ITEM compile_opts "-Wno-old-style-declaration" "-Werror=old-style-declaration")
idf_build_set_property(COMPILE_OPTIONS "${compile_opts}")



