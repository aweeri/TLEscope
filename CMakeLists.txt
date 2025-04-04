cmake_minimum_required(VERSION 3.10)
project(TLEScope)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(external/raylib)
add_subdirectory(external/sgp4)

include_directories(${CMAKE_SOURCE_DIR}/include)

if(WIN32)
    list(APPEND SOURCES resources/resources.rc)
endif()

add_executable(TLEScope
    ${SOURCES}
    src/main.cpp
    src/camera.cpp
    src/point.cpp
    src/conversions.cpp
)

# lib
target_link_libraries(TLEScope raylib)
target_link_libraries(TLEScope sgp4)

# headers
target_include_directories(TLEScope PRIVATE include)

# Post build copy resources/
add_custom_command(TARGET TLEScope POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:TLEScope>/resources
)

