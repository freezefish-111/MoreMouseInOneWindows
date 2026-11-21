cmake_minimum_required(VERSION 3.16)
project(MultiCursor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MultiCursor WIN32
    src/common.cpp
    src/cursor_image.cpp
    src/overlay.cpp
    src/system_cursor.cpp
    src/app.cpp
    src/main.cpp
)

target_compile_definitions(MultiCursor PRIVATE
    UNICODE
    _UNICODE
    OEMRESOURCE
)

target_link_libraries(MultiCursor PRIVATE
    user32
    gdi32
    shell32
    wtsapi32
)

if (MINGW)
    target_link_options(MultiCursor PRIVATE -municode -static -s)
    target_compile_options(MultiCursor PRIVATE
        -Wall -Wextra -Wno-cast-function-type -finput-charset=UTF-8)
endif()

if (MSVC)
    target_compile_options(MultiCursor PRIVATE /utf-8 /W4)
endif()
