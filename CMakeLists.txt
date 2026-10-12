cmake_minimum_required(VERSION 3.16)
project(CupidGBA VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(CUPID_BUILD_FRONTEND "Build the SDL2 frontend" ON)
option(CUPID_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(CUPID_TEST_ROM_DIR "" CACHE PATH "Directory containing checksum-verified external test ROMs")
set(CUPID_TIMER_SUITE_ROM "" CACHE FILEPATH "Locally built timer and IRQ suite ROM")
set(CUPID_DMA_SUITE_ROM "" CACHE FILEPATH "Locally built DMA transfer suite ROM")
set(CUPID_BUS_SUITE_ROM "" CACHE FILEPATH "Locally built CPU and bus timing suite ROM")
set(CUPID_BIOS_MATH_SUITE_ROM "" CACHE FILEPATH "Locally built BIOS math register suite ROM")
set(CUPID_TEST_BIOS "" CACHE FILEPATH "External BIOS for local hardware suite checks")

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
    src/gba/gba.c src/gba/memory.c src/gba/bus.c src/gba/io.c src/gba/irq.c src/gba/video.c
    src/gba/audio.c src/gba/dma.c src/gba/prefetch.c src/gba/timer.c src/gba/bios.c
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
                     dma-start-delay dma-nonseq dma-latch dma-burst)
            add_test(NAME rom.${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_console.py
                --directory ${CUPID_TEST_ROM_DIR}
                --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.${case} PROPERTIES TIMEOUT 50)
            if(CUPID_TEST_BIOS AND case MATCHES "^dma-")
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
