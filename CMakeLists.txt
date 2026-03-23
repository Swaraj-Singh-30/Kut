cmake_minimum_required(VERSION 3.15)
project(kut)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kut
    main.cpp
    editor.cpp
    terminal.cpp
)

if(UNIX AND NOT APPLE)
  target_compile_definitions(kut PRIVATE _DEFAULT_SOURCE _BSD_SOURCE _GNU_SOURCE)
endif()

target_compile_options(kut PRIVATE -Wall -Wextra -pedantic)