cmake_minimum_required(VERSION 3.10)
project(NTRIP-Analyser C)

# Set the C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Collect all source files
file(GLOB SOURCES
    "src/*.c"
    "src/*.h"
)

# Create the executable
add_executable(ntrip-analyser ${SOURCES})

# Add any libraries you need here, for example:
# target_link_libraries(ntrip-analyser pthread)

# Optionally, specify include directories if your headers are not in src/
# target_include_directories(ntrip-analyser PRIVATE src)
