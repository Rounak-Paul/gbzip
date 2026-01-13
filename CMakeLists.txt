cmake_minimum_required(VERSION 3.16)
project(gbzip VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0600)
endif()

# Find required packages
# Try to find libzip using different methods
find_package(PkgConfig QUIET)

# Method 1: Try pkg-config if available
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBZIP libzip)
endif()

# Method 2: Try to find libzip directly if pkg-config failed
if(NOT LIBZIP_FOUND)
    find_path(LIBZIP_INCLUDE_DIR 
        NAMES zip.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include /opt/local/include
        PATH_SUFFIXES libzip)
    
    find_library(LIBZIP_LIBRARY
        NAMES zip libzip
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /opt/local/lib)
    
    if(LIBZIP_INCLUDE_DIR AND LIBZIP_LIBRARY)
        set(LIBZIP_FOUND TRUE)
        set(LIBZIP_INCLUDE_DIRS ${LIBZIP_INCLUDE_DIR})
        set(LIBZIP_LIBRARIES ${LIBZIP_LIBRARY})
        set(LIBZIP_LIBRARY_DIRS "")
        set(LIBZIP_CFLAGS_OTHER "")
        message(STATUS "Found libzip: ${LIBZIP_LIBRARY}")
    endif()
endif()

# Check if we found libzip
if(NOT LIBZIP_FOUND)
    message(FATAL_ERROR "libzip not found. Please install libzip development package:\n"
                        "  macOS: brew install libzip\n"
                        "  Ubuntu/Debian: sudo apt-get install libzip-dev\n" 
                        "  CentOS/RHEL: sudo yum install libzip-devel\n"
                        "  Windows: vcpkg install libzip")
endif()

# Find zlib (required for parallel compression)
find_package(ZLIB REQUIRED)
if(NOT ZLIB_FOUND)
    message(FATAL_ERROR "zlib not found. Please install zlib development package:\n"
                        "  macOS: brew install zlib (usually pre-installed)\n"
                        "  Ubuntu/Debian: sudo apt-get install zlib1g-dev\n"
                        "  CentOS/RHEL: sudo yum install zlib-devel")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${LIBZIP_INCLUDE_DIRS})
include_directories(${ZLIB_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.c
    src/zip.c
    src/zipignore.c
    src/diff.c
    src/utils.c
    src/logging.c
)

# Header files
set(HEADERS
    include/gbzip.h
    include/gbzip_zip.h
    include/zipignore.h
    include/diff.h
    include/utils.h
    include/logging.h
)

# Create executable
add_executable(gbzip ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(gbzip ${LIBZIP_LIBRARIES} ${ZLIB_LIBRARIES})
target_link_directories(gbzip PRIVATE ${LIBZIP_LIBRARY_DIRS})
target_compile_options(gbzip PRIVATE ${LIBZIP_CFLAGS_OTHER})

# Platform-specific libraries
if(WIN32)
    target_link_libraries(gbzip shlwapi)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(gbzip m pthread)
elseif(APPLE)
    target_link_libraries(gbzip pthread)
endif()

# Install target
install(TARGETS gbzip DESTINATION bin)

# Enable testing
enable_testing()
add_subdirectory(tests)