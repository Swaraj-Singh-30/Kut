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
  terminal.cpp
  gui_renderer.cpp
)

find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
target_link_libraries(kut sfml-graphics sfml-window sfml-system)

# warnings
target_compile_options(kut PRIVATE -Wall -Wextra -pedantic)

# Linux-specific defines
if(UNIX AND NOT APPLE)
  target_compile_definitions(kut PRIVATE _DEFAULT_SOURCE _BSD_SOURCE _GNU_SOURCE)
endif()
