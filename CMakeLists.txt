cmake_minimum_required(VERSION 3.20)
project(abc_sdl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL3_IMAGE REQUIRED IMPORTED_TARGET sdl3-image)
pkg_check_modules(SDL3_MIXER REQUIRED IMPORTED_TARGET sdl3-mixer)
pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)

add_executable(abc-sdl
  src/main.c
  src/util.c
  src/dfm.c
  src/audio.c
  src/scene.c
  src/modules.c
  src/font.c
)

target_include_directories(abc-sdl PRIVATE src)
target_link_libraries(abc-sdl PRIVATE
  PkgConfig::SDL3
  PkgConfig::SDL3_IMAGE
  PkgConfig::SDL3_MIXER
  m
)

if(FLUIDSYNTH_FOUND)
  target_compile_definitions(abc-sdl PRIVATE HAVE_FLUIDSYNTH=1)
  target_include_directories(abc-sdl PRIVATE ${FLUIDSYNTH_INCLUDE_DIRS})
  target_link_libraries(abc-sdl PRIVATE PkgConfig::FLUIDSYNTH)
  message(STATUS "fluidsynth found - MIDI music enabled")
else()
  message(STATUS "fluidsynth not found - MIDI music disabled")
endif()
