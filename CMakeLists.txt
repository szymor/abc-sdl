cmake_minimum_required(VERSION 3.20)
project(abc_sdl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

set(ABC_SOURCES
  src/main.c
  src/util.c
  src/dfm.c
  src/audio.c
  src/scene.c
  src/modules.c
  src/font.c
  src/video.c
  src/platform/platform.c
  src/ui/hud.c
)

if(ANDROID)
  find_package(SDL3 REQUIRED CONFIG)
  find_package(SDL3_image REQUIRED CONFIG)
  find_package(SDL3_mixer REQUIRED CONFIG)

  add_library(main SHARED ${ABC_SOURCES})
  target_compile_definitions(main PRIVATE ABC_ANDROID=1 __ANDROID__=1)
  target_include_directories(main PRIVATE src)
  target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    android
    log
    m
  )

  set(ABC_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/${ANDROID_ABI}")
  if(EXISTS "${ABC_FFMPEG_ROOT}/lib/libavcodec.a")
    target_compile_definitions(main PRIVATE HAVE_FFMPEG=1)
    target_include_directories(main PRIVATE "${ABC_FFMPEG_ROOT}/include")
    target_link_libraries(main PRIVATE
      "${ABC_FFMPEG_ROOT}/lib/libavformat.a"
      "${ABC_FFMPEG_ROOT}/lib/libavcodec.a"
      "${ABC_FFMPEG_ROOT}/lib/libswresample.a"
      "${ABC_FFMPEG_ROOT}/lib/libswscale.a"
      "${ABC_FFMPEG_ROOT}/lib/libavutil.a"
      z
    )
    message(STATUS "Android FFmpeg found for ${ANDROID_ABI}")
  else()
    message(STATUS "Android FFmpeg not found for ${ANDROID_ABI}; video disabled")
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  pkg_check_modules(SDL3_IMAGE REQUIRED IMPORTED_TARGET sdl3-image)
  pkg_check_modules(SDL3_MIXER REQUIRED IMPORTED_TARGET sdl3-mixer)
  pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
  pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

  add_executable(abc-sdl ${ABC_SOURCES})
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

  if(FFMPEG_FOUND)
    target_compile_definitions(abc-sdl PRIVATE HAVE_FFMPEG=1)
    target_link_libraries(abc-sdl PRIVATE PkgConfig::FFMPEG)
    message(STATUS "FFmpeg found - AVI playback enabled")
  else()
    message(STATUS "FFmpeg not found - AVI playback disabled")
  endif()
endif()
