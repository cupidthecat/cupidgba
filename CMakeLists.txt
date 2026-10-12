cmake_minimum_required(VERSION 3.16)
project(CupidGBA VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(CUPID_BUILD_FRONTEND "Build the SDL2 frontend" ON)
option(CUPID_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(CUPID_TEST_ROM_DIR "" CACHE PATH "Directory containing checksum-verified external test ROMs")

add_library(cupid_build_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cupid_build_options INTERFACE -Wall -Wextra -Werror)
    if(CUPID_SANITIZERS)
        target_compile_options(cupid_build_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(cupid_build_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_library(cupid_core STATIC
    src/arm/arm.c src/arm/isa-arm.c src/arm/isa-thumb.c
    src/core/timing.c
    src/gba/gba.c src/gba/memory.c src/gba/io.c src/gba/video.c
    src/gba/audio.c src/gba/dma.c src/gba/timer.c src/gba/bios.c
    src/gba/hle-bios.c src/gba/savedata.c
    src/gba/render/software.c src/gba/render/composite.c
    src/gba/render/mode0.c src/gba/render/mode2.c
    src/gba/render/mode3.c src/gba/render/mode4.c
    src/gba/render/mode5.c src/gba/render/obj.c)
target_include_directories(cupid_core PUBLIC include)
target_link_libraries(cupid_core PUBLIC cupid_build_options)
if(NOT WIN32)
    target_link_libraries(cupid_core PUBLIC m)
endif()

if(CUPID_BUILD_FRONTEND)
    find_package(SDL2 REQUIRED CONFIG)
    add_executable(cupidgba src/platform/main.c src/platform/audio.c src/platform/input.c)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(cupidgba PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(cupidgba PRIVATE cupid_core SDL2::SDL2)
endif()

add_executable(cupidgba-headless tests/rom_runner.c)
target_link_libraries(cupidgba-headless PRIVATE cupid_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_timing tests/core/test_timing.c)
    target_link_libraries(test_timing PRIVATE cupid_core)
    add_test(NAME core.timing COMMAND test_timing)
    set_tests_properties(core.timing PROPERTIES TIMEOUT 10)

    add_executable(test_system tests/gba/test_system.c)
    target_link_libraries(test_system PRIVATE cupid_core)
    add_test(NAME gba.system COMMAND test_system)
    set_tests_properties(gba.system PROPERTIES TIMEOUT 10)

    add_executable(test_cpu tests/arm/test_cpu.c)
    target_link_libraries(test_cpu PRIVATE cupid_core)
    add_test(NAME arm.cpu COMMAND test_cpu)
    set_tests_properties(arm.cpu PROPERTIES TIMEOUT 10)

    if(CUPID_TEST_ROM_DIR)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(case arm thumb memory)
            add_test(NAME rom.${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_rom.py
                --directory ${CUPID_TEST_ROM_DIR}
                --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.${case} PROPERTIES TIMEOUT 50)
        endforeach()
    endif()
endif()
