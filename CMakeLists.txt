cmake_minimum_required(VERSION 3.12)
project(ConcurrentEventDispatcher CXX)

# Force C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings for clean code hygiene
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Find Threading support
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Define the core dispatcher static library
add_library(dispatcher_lib STATIC
    src/event_bus.cpp
    src/handler_registry.cpp
    src/thread_pool.cpp
    src/stats_sampler.cpp
)
target_link_libraries(dispatcher_lib PRIVATE Threads::Threads)

# Define main simulation executable
add_executable(event_dispatcher src/main.cpp)
target_link_libraries(event_dispatcher PRIVATE dispatcher_lib Threads::Threads)

# Define Test targets
add_executable(test_priority_ordering tests/test_priority_ordering.cpp)
target_link_libraries(test_priority_ordering PRIVATE dispatcher_lib Threads::Threads)

add_executable(test_no_starvation tests/test_no_starvation.cpp)
target_link_libraries(test_no_starvation PRIVATE dispatcher_lib Threads::Threads)

add_executable(test_latency_under_load tests/test_latency_under_load.cpp)
target_link_libraries(test_latency_under_load PRIVATE dispatcher_lib Threads::Threads)
