cmake_minimum_required(VERSION 3.16)
project(lab04_sync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lab04_sync
    src/main.cpp
    src/parent.cpp
    src/participant.cpp
    src/shared_memory.cpp
    src/logger.cpp
    src/sync.cpp
    src/suite.cpp
)
target_include_directories(lab04_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lab04_sync PRIVATE winmm)
if (MSVC)
    target_compile_options(lab04_sync PRIVATE /W4 /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(lab04_sync PRIVATE -Wall -Wextra)
endif()
