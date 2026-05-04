cmake_minimum_required(VERSION 3.15)
project(kut)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# core sources
add_executable(kut
  main.cpp
  editor.cpp
  command.cpp
  buffermanager.cpp
  config.cpp
  #gui_renderer.cpp
)

# terminal layer -- Unix only
if(NOT WIN32)
  target_sources(kut PRIVATE terminal.cpp)
endif()

# warnings
target_compile_options(kut PRIVATE -Wall -Wextra -pedantic)

# Linux-specific defines
if(UNIX AND NOT APPLE)
  target_compile_definitions(kut PRIVATE _DEFAULT_SOURCE _BSD_SOURCE _GNU_SOURCE)
endif()

# SDL2 + SDL_ttf
# find_package(SDL2 REQUIRED)
# find_package(SDL2_ttf REQUIRED)
# target_link_libraries(kut SDL2::SDL2 SDL2_ttf::SDL2_ttf)