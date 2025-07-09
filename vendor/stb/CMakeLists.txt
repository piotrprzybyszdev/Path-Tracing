add_library(stb stb/stb_image.h implementation.cpp)

target_include_directories(stb PUBLIC ${CMAKE_SOURCE_DIR}/vendor/stb/stb)