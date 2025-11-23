cmake_minimum_required(VERSION 3.10)

project(TextLayoutTest)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories
include_directories(${CMAKE_SOURCE_DIR}/../src)
include_directories(${CMAKE_SOURCE_DIR})  # For Arduino.h mock and font files

# Add the test executable
add_executable(textlayout_test
    windows_test_simple.cpp
    Adafruit_GFX.cpp
    ../src/KnuthPlassLayoutStrategy.cpp
    ../src/TextLayout.cpp
)

# Define DEBUG_LAYOUT for debugging
target_compile_definitions(textlayout_test PRIVATE DEBUG_LAYOUT)

# Link math library if needed
if(UNIX)
    target_link_libraries(textlayout_test m)
endif()

# Enable warnings
if(MSVC)
    target_compile_options(textlayout_test PRIVATE /W4)
else()
    target_compile_options(textlayout_test PRIVATE -Wall -Wextra)
endif()
