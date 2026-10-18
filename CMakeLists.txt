cmake_minimum_required(VERSION 3.10.0)
project(coroutine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

include_directories(src/debug)


add_library(coroutine SHARED
    src/thread/thread.cpp
    src/fiber/fiber.cpp
    src/scheduler/scheduler.cpp
    src/scheduler/scheduler.h
    src/timer/timer.cpp
    src/iomanager/iomanager.h
    src/iomanager/iomanager.cpp
)

# 头文件按 "fiber.h" / "thread.h" / "scheduler.h" / "debug.h" 直接包含
target_include_directories(coroutine PUBLIC
    src/fiber
    src/thread
    src/scheduler
    src/debug
)

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE coroutine)
