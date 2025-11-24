cmake_minimum_required(VERSION 3.5)
project(StringWordProviderTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer our test include directory so the minimal WString.h is used
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(StringWordProviderTest
  StringWordProviderTest.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/screens/text view/StringWordProvider.cpp"
)

target_compile_definitions(StringWordProviderTest PRIVATE TEST_BUILD)
target_include_directories(StringWordProviderTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/../src)
