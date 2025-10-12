cmake_minimum_required(VERSION 3.15)
project(COD)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(CMAKE_BUILD_TYPE MATCHES Debug)
    if(MSVC)
        set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /DDEBUG")
    else()
        set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    endif()
endif()


find_package(OpenGL REQUIRED)


find_package(Threads REQUIRED)


find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found via find_package, trying manual search...")


    find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
        PATHS
            /usr/include
            /usr/local/include
            /opt/homebrew/include
            /usr/local/opt/glfw/include
            "$ENV{PROGRAMFILES}/GLFW/include"
    )

    find_library(GLFW_LIBRARY
        NAMES glfw glfw3
        PATHS
            /usr/lib
            /usr/local/lib
            /opt/homebrew/lib
            /usr/local/opt/glfw/lib
            "$ENV{PROGRAMFILES}/GLFW/lib"
    )

    if(GLFW_INCLUDE_DIR AND GLFW_LIBRARY)
        message(STATUS "Found GLFW: ${GLFW_LIBRARY}")
        add_library(glfw UNKNOWN IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "GLFW not found. Install with:\n"
                            "  Linux: sudo apt-get install libglfw3-dev\n"
                            "  macOS: brew install glfw\n"
                            "  Windows: Download from https://www.glfw.org/")
    endif()
endif()


find_package(Freetype REQUIRED)


file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE LIB_SOURCES "lib/*.c")


add_executable(${PROJECT_NAME} ${SOURCES} ${LIB_SOURCES})


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${FREETYPE_INCLUDE_DIRS}
)


target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    Freetype::Freetype
    OpenGL::GL
    Threads::Threads
)


if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
elseif(UNIX)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        m
        ${CMAKE_DL_LIBS}
    )
elseif(WIN32)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
    )
endif()


if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)

    target_compile_options(${PROJECT_NAME} PRIVATE /wd4100 /wd4189)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable)
endif()
