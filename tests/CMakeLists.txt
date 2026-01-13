cmake_minimum_required(VERSION 3.16)

# Source files needed for tests (excluding main.c to avoid multiple main functions)
set(TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/utils.c
    ${CMAKE_SOURCE_DIR}/src/zipignore.c
    ${CMAKE_SOURCE_DIR}/src/zip.c
    ${CMAKE_SOURCE_DIR}/src/diff.c
    ${CMAKE_SOURCE_DIR}/src/logging.c
)

# Add a simple test
add_executable(test_gbzip test_main.c ${TEST_SOURCES})
target_link_libraries(test_gbzip ${LIBZIP_LIBRARIES} ${ZLIB_LIBRARIES})
target_link_directories(test_gbzip PRIVATE ${LIBZIP_LIBRARY_DIRS})
target_compile_options(test_gbzip PRIVATE ${LIBZIP_CFLAGS_OTHER})
target_include_directories(test_gbzip PRIVATE ${CMAKE_SOURCE_DIR}/include ${LIBZIP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

# Platform-specific libraries (same as main executable)
if(WIN32)
    target_link_libraries(test_gbzip shlwapi)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(test_gbzip m pthread)
elseif(APPLE)
    target_link_libraries(test_gbzip pthread)
endif()

add_test(NAME basic_test COMMAND test_gbzip)