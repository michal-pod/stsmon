cmake_minimum_required(VERSION 3.10)

project(stsmon LANGUAGES C)

set(CMAKE_C_STANDARD 99)

set(VERSION "git" CACHE STRING "Version of stsmon")
string(REGEX MATCHALL "[0-9]+" _VERSION_PARTS "${VERSION}")
list(LENGTH _VERSION_PARTS _VERSION_PARTS_LEN)

if(_VERSION_PARTS_LEN EQUAL 0)
    set(VERSION_Y 0)
    set(VERSION_M 0)
    set(VERSION_D 0)
    set(VERSION_B 0)
else()
    list(GET _VERSION_PARTS 0 VERSION_Y)
    if(_VERSION_PARTS_LEN GREATER 1)
        list(GET _VERSION_PARTS 1 VERSION_M)
    else()
        set(VERSION_M 0)
    endif()
    if(_VERSION_PARTS_LEN GREATER 2)
        list(GET _VERSION_PARTS 2 VERSION_D)
    else()
        set(VERSION_D 0)
    endif()
    if(_VERSION_PARTS_LEN GREATER 3)
        list(GET _VERSION_PARTS 3 VERSION_B)
    else()
        set(VERSION_B 0)
    endif()
endif()

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wshadow)
endif()

if(CMAKE_COMPILER_ID MATCHES "GNU|Clang")
    option(ENABLE_COVERAGE "Enable code coverage flags" OFF)

    if(ENABLE_COVERAGE)
        message(STATUS "Code coverage enabled")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -O0")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
add_definitions(-DVERSION=\"${VERSION}\")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.cmake ${CMAKE_BINARY_DIR}/src/version.rc @ONLY)
    set(STSMON_WINDOWS_SOURCES ${CMAKE_BINARY_DIR}/src/version.rc)
endif()
add_executable(stsmon
    src/main.c 
    src/monitor.c
    src/pat.c
    src/sdt.c
    src/services.c
    src/dvb.c
    src/pmt.c
    src/output.c
    ${STSMON_WINDOWS_SOURCES}
)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        test-tsg
        tsg/test-tsg.c
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_compile_definitions(stsmon PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(stsmon ws2_32)
endif()