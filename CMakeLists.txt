cmake_minimum_required(VERSION 2.6)
project(openxr-example)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)

INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)

# uncomment to use an openxr/build directory that is next to the openxr-example directory
include_directories("${CMAKE_SOURCE_DIR}/../OpenXR-SDK-Source/build/include/" ${SDL2_INCLUDE_DIR})
link_directories("${CMAKE_SOURCE_DIR}/../OpenXR-SDK-Source/build/src/loader/")

add_executable(openxr-example main.c glimpl.c)

target_link_libraries(openxr-example openxr_loader Xrandr ${X11_LIBRARIES} ${OPENGL_LIBRARIES} ${SDL2_LIBRARIES} m)

if(MSVC)
  target_compile_options(openxr-example PRIVATE /W4 /WX)
else(MSVC)
  target_compile_options(openxr-example PRIVATE -pedantic -Wall -Wextra -Wno-unused-parameter)
endif(MSVC)


install(TARGETS openxr-example RUNTIME DESTINATION bin)
