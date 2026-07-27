cmake_minimum_required(VERSION 3.20)
project(TodoListWin32 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(TodoListWin32 WIN32
    src/main.cpp
)

target_compile_definitions(TodoListWin32 PRIVATE UNICODE _UNICODE)
target_compile_options(TodoListWin32 PRIVATE /utf-8)
target_link_libraries(TodoListWin32 PRIVATE comctl32)
