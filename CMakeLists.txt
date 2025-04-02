cmake_minimum_required(VERSION 3.10)
project(TLEScope)

set(CMAKE_CXX_STANDARD 17)

# Include raylib (assumes you have the raylib source in external/raylib)
add_subdirectory(external/raylib)
add_subdirectory(external/sgp4)

# Add your source files
add_executable(TLEScope
    src/main.cpp
)

# Link raylib to your executable
target_link_libraries(TLEScope raylib)
target_link_libraries(TLEScope sgp4)

# Include directories for your project's headers (and raylib if needed)
target_include_directories(TLEScope PRIVATE include)

# Post-build command: copy resources folder to the executable directory
add_custom_command(TARGET TLEScope POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:TLEScope>/resources
)

