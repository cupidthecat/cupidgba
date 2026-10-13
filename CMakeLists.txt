cmake_minimum_required(VERSION 3.16)
project(CupidGBA VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(CUPID_BUILD_FRONTEND "Build the SDL2 frontend" ON)
option(CUPID_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(CUPID_THREAD_SANITIZER "Enable the thread sanitizer" OFF)
set(CUPID_TEST_ROM_DIR "" CACHE PATH "Directory containing checksum-verified external test ROMs")
set(CUPID_TIMER_SUITE_ROM "" CACHE FILEPATH "Locally built timer and IRQ suite ROM")
set(CUPID_DMA_SUITE_ROM "" CACHE FILEPATH "Locally built DMA transfer suite ROM")
set(CUPID_BUS_SUITE_ROM "" CACHE FILEPATH "Locally built CPU and bus timing suite ROM")
set(CUPID_BIOS_MATH_SUITE_ROM "" CACHE FILEPATH "Locally built BIOS math register suite ROM")
set(CUPID_IO_SUITE_ROM "" CACHE FILEPATH "Locally built I/O register-read suite ROM")
set(CUPID_AUDIO_TEST_ROM "" CACHE FILEPATH "Locally built audio signal cartridge ROM")
set(CUPID_TEST_BIOS "" CACHE FILEPATH "External BIOS for local hardware suite checks")

add_library(cupid_build_options INTERFACE)
if(CUPID_SANITIZERS AND CUPID_THREAD_SANITIZER)
    message(FATAL_ERROR "ThreadSanitizer requires a separate build from AddressSanitizer")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cupid_build_options INTERFACE -Wall -Wextra -Werror)
    if(CUPID_SANITIZERS)
        target_compile_options(cupid_build_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
            $<$<CONFIG:Debug>:-O1>)
        target_link_options(cupid_build_options INTERFACE -fsanitize=address,undefined)
    endif()
    if(CUPID_THREAD_SANITIZER)
        target_compile_options(cupid_build_options INTERFACE
            -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(cupid_build_options INTERFACE -fsanitize=thread)
    endif()
endif()

add_library(cupid_core STATIC
    src/arm/arm.c src/arm/isa-arm.c src/arm/isa-thumb.c
    src/core/timing.c
    src/gba/gba.c src/gba/memory.c src/gba/bus.c src/gba/io.c src/gba/irq.c src/gba/video.c
    src/gba/audio.c src/gba/dma.c src/gba/prefetch.c src/gba/timer.c src/gba/bios.c
    src/gba/hle-bios.c src/gba/savedata.c src/gba/save/file.c
    src/gba/render/fetch.c src/gba/render/scanout.c src/gba/render/object-fetch.c)
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

    add_executable(test_io tests/gba/test_io.c)
    target_link_libraries(test_io PRIVATE cupid_core)
    add_test(NAME gba.io COMMAND test_io)
    set_tests_properties(gba.io PROPERTIES TIMEOUT 10)

    add_executable(test_save_file tests/gba/save/test_file.c)
    target_link_libraries(test_save_file PRIVATE cupid_core)
    add_test(NAME gba.save-file COMMAND test_save_file ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gba.save-file PROPERTIES TIMEOUT 10)

    add_executable(test_audio tests/gba/test_audio.c)
    target_link_libraries(test_audio PRIVATE cupid_core)
    add_test(NAME gba.audio COMMAND test_audio)
    set_tests_properties(gba.audio PROPERTIES TIMEOUT 10)

    if(CUPID_BUILD_FRONTEND)
        add_executable(test_audio_output tests/platform/test_audio_output.c src/platform/audio.c)
        target_link_libraries(test_audio_output PRIVATE cupid_core SDL2::SDL2)
        add_test(NAME platform.audio COMMAND test_audio_output)
        set_tests_properties(platform.audio PROPERTIES TIMEOUT 10)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME platform.save-startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_save_startup.py
            --frontend $<TARGET_FILE:cupidgba>)
        set_tests_properties(platform.save-startup PROPERTIES TIMEOUT 10)
    endif()

    add_executable(test_timer tests/gba/test_timer.c)
    target_link_libraries(test_timer PRIVATE cupid_core)
    add_test(NAME gba.timer COMMAND test_timer)
    set_tests_properties(gba.timer PROPERTIES TIMEOUT 10)

    add_executable(test_irq tests/gba/test_irq.c)
    target_link_libraries(test_irq PRIVATE cupid_core)
    add_test(NAME gba.irq COMMAND test_irq)
    set_tests_properties(gba.irq PROPERTIES TIMEOUT 10)

    add_executable(test_dma tests/gba/test_dma.c)
    target_link_libraries(test_dma PRIVATE cupid_core)
    add_test(NAME gba.dma COMMAND test_dma)
    set_tests_properties(gba.dma PROPERTIES TIMEOUT 10)

    add_executable(test_prefetch tests/gba/test_prefetch.c)
    target_link_libraries(test_prefetch PRIVATE cupid_core)
    add_test(NAME gba.prefetch COMMAND test_prefetch)
    set_tests_properties(gba.prefetch PROPERTIES TIMEOUT 10)

    add_executable(test_video tests/gba/test_video.c)
    target_link_libraries(test_video PRIVATE cupid_core)
    add_test(NAME gba.video COMMAND test_video)
    set_tests_properties(gba.video PROPERTIES TIMEOUT 10)

    add_executable(test_render tests/gba/test_render.c)
    target_link_libraries(test_render PRIVATE cupid_core)
    add_test(NAME gba.render COMMAND test_render)
    set_tests_properties(gba.render PROPERTIES TIMEOUT 10)

    add_executable(test_cpu tests/arm/test_cpu.c)
    target_link_libraries(test_cpu PRIVATE cupid_core)
    add_test(NAME arm.cpu COMMAND test_cpu)
    set_tests_properties(arm.cpu PROPERTIES TIMEOUT 10)

    add_executable(test_bios tests/bios/test_bios.c)
    target_link_libraries(test_bios PRIVATE cupid_core)
    add_test(NAME gba.bios COMMAND test_bios)
    set_tests_properties(gba.bios PROPERTIES TIMEOUT 10)

    add_executable(test_bios_math_copy tests/bios/test_math_copy.c)
    target_link_libraries(test_bios_math_copy PRIVATE cupid_core)
    add_test(NAME gba.bios-math-copy COMMAND test_bios_math_copy)
    set_tests_properties(gba.bios-math-copy PROPERTIES TIMEOUT 10)

    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-external COMMAND test_bios --external-bios-waits ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-math-copy-external COMMAND test_bios_math_copy --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-math-copy-external PROPERTIES TIMEOUT 10)
    endif()

    if(CUPID_TEST_ROM_DIR)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.input-download COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/test_fetch_inputs.py)
        set_tests_properties(rom.input-download PROPERTIES TIMEOUT 10)
        foreach(case arm thumb memory)
            add_test(NAME rom.${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_rom.py
                --directory ${CUPID_TEST_ROM_DIR}
                --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.${case} PROPERTIES TIMEOUT 50)
        endforeach()
        foreach(case timer-reload timer-start-stop timer-tick-before-reload
                     dma-start-delay dma-nonseq dma-latch dma-burst vram-mirror)
            add_test(NAME rom.${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_console.py
                --directory ${CUPID_TEST_ROM_DIR}
                --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.${case} PROPERTIES TIMEOUT 50)
            if(CUPID_TEST_BIOS AND (case MATCHES "^dma-" OR case STREQUAL "vram-mirror"))
                add_test(NAME rom.${case}-external COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_console.py
                    --directory ${CUPID_TEST_ROM_DIR} --bios ${CUPID_TEST_BIOS}
                    --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
                set_tests_properties(rom.${case}-external PROPERTIES TIMEOUT 50)
            endif()
        endforeach()
        foreach(case status vcount irq dma)
            add_test(NAME rom.ppu-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_ppu.py
                --directory ${CUPID_TEST_ROM_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                --test ${case})
            set_tests_properties(rom.ppu-${case} PROPERTIES TIMEOUT 130)
            if(CUPID_TEST_BIOS)
                add_test(NAME rom.ppu-${case}-external COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_ppu.py
                    --directory ${CUPID_TEST_ROM_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                    --bios ${CUPID_TEST_BIOS} --test ${case})
                set_tests_properties(rom.ppu-${case}-external PROPERTIES TIMEOUT 130)
            endif()
        endforeach()
        foreach(case bitmap text-4 text-8 affine-2 affine-both palette-single
                     palette-direct palette-alpha object-oam object-vram)
            add_test(NAME rom.ppu-access-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_ppu_access.py
                --directory ${CUPID_TEST_ROM_DIR} --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.ppu-access-${case} PROPERTIES TIMEOUT 130)
        endforeach()
        foreach(case green-swap affine-line-step sprite-mosaic)
            add_test(NAME rom.raster-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_raster.py
                --directory ${CUPID_TEST_ROM_DIR} --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.raster-${case} PROPERTIES TIMEOUT 50)
            if(CUPID_TEST_BIOS)
                add_test(NAME rom.raster-${case}-external COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_raster.py
                    --directory ${CUPID_TEST_ROM_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                    --bios ${CUPID_TEST_BIOS} --test ${case})
                set_tests_properties(rom.raster-${case}-external PROPERTIES TIMEOUT 50)
            endif()
        endforeach()
    endif()

    if(CUPID_BUS_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.bus-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_bus_suite.py
            --rom ${CUPID_BUS_SUITE_ROM}
            --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.bus-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.bus-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_bus_suite.py
                --rom ${CUPID_BUS_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.bus-suite-external PROPERTIES TIMEOUT 100)
        endif()
    endif()

    if(CUPID_BIOS_MATH_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.bios-math-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_bios_math_suite.py
            --rom ${CUPID_BIOS_MATH_SUITE_ROM}
            --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.bios-math-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.bios-math-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_bios_math_suite.py
                --rom ${CUPID_BIOS_MATH_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.bios-math-suite-external PROPERTIES TIMEOUT 100)
        endif()
    endif()

    if(CUPID_DMA_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.dma-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_dma_suite.py
            --rom ${CUPID_DMA_SUITE_ROM} --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.dma-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.dma-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_dma_suite.py
                --rom ${CUPID_DMA_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.dma-suite-external PROPERTIES TIMEOUT 100)
        endif()
    endif()

    if(CUPID_IO_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.io-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_io_suite.py
            --rom ${CUPID_IO_SUITE_ROM} --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.io-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.io-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_io_suite.py
                --rom ${CUPID_IO_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.io-suite-external PROPERTIES TIMEOUT 100)
        endif()
    endif()

    if(CUPID_AUDIO_TEST_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(case tone stereo sweep steps dynamic noise wave bias fifo silence)
            add_test(NAME rom.audio-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_audio.py
                --rom ${CUPID_AUDIO_TEST_ROM} --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.audio-${case} PROPERTIES TIMEOUT 130)
        endforeach()
    endif()

    if(CUPID_TIMER_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.timer-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_timer_suite.py
            --rom ${CUPID_TIMER_SUITE_ROM}
            --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.timer-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.timer-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_timer_suite.py
                --rom ${CUPID_TIMER_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.timer-suite-external PROPERTIES TIMEOUT 100)
        endif()
    endif()
endif()
