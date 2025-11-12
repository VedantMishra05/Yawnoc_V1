cmake_minimum_required(VERSION 3.10)
project(Yawnoc_Proto)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source
add_executable(Yawnoc_Proto src/main.cpp)

# Find SFML (3.x)
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

target_link_libraries(Yawnoc_Proto PRIVATE sfml-graphics sfml-window sfml-system)

# Copy assets folder to build directory after build
add_custom_command(TARGET Yawnoc_Proto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Yawnoc_Proto>/assets)
