cmake_minimum_required(VERSION 3.10)
project(miniexpat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(miniexpat STATIC expat_impl.cpp)
target_include_directories(miniexpat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_executable(miniexpat_tests tests.cpp)
target_link_libraries(miniexpat_tests miniexpat)
add_test(NAME miniexpat_tests COMMAND miniexpat_tests)
