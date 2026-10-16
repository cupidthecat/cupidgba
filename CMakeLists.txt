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
set(CUPID_SERIAL_SUITE_ROM "" CACHE FILEPATH "Locally built serial timing and register suite ROM")
set(CUPID_SERIAL_PROBE_DIR "" CACHE PATH "Directory containing checksum-verified serial cartridge probes")
set(CUPID_HALT_PROBE_DIR "" CACHE PATH "Directory containing checksum-verified firmware HALT probes")
set(CUPID_AUDIO_TEST_ROM "" CACHE FILEPATH "Locally built audio signal cartridge ROM")
set(CUPID_FIFO_PROBE_DIR "" CACHE PATH "Directory containing checksum-verified FIFO DMA cartridges")
set(CUPID_FIFO_SOURCE_ROM "" CACHE FILEPATH "Checksum-verified FIFO base cartridge rebuilt from its pinned source")
set(CUPID_TEST_BIOS "" CACHE FILEPATH "External BIOS for local hardware suite checks")

add_library(cupid_build_options INTERFACE)
if(CUPID_SANITIZERS AND CUPID_THREAD_SANITIZER)
    message(FATAL_ERROR "ThreadSanitizer requires a separate build from AddressSanitizer")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cupid_build_options INTERFACE -Wall -Wextra -Werror)
    if(CUPID_SANITIZERS)
        target_compile_options(cupid_build_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
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
    src/gba/hle-bios.c src/gba/bios/file.c src/gba/savedata.c src/gba/save/file.c
    src/gba/serial/serial.c src/gba/serial/joybus.c
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
    find_package(SDL2 2.24 REQUIRED CONFIG)
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

    add_executable(test_reset_entry tests/arm/test_reset_entry.c)
    target_link_libraries(test_reset_entry PRIVATE cupid_core)
    foreach(group external fast irq repeat)
        add_test(NAME arm.reset-entry-${group} COMMAND test_reset_entry ${group})
        set_tests_properties(arm.reset-entry-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_cp14 tests/arm/test_cp14.c)
    target_link_libraries(test_cp14 PRIVATE cupid_core)
    foreach(group read write flags conditions undefined pipeline)
        add_test(NAME arm.cp14-${group} COMMAND test_cp14 ${group})
        set_tests_properties(arm.cp14-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_undefined tests/arm/test_undefined.c)
    target_link_libraries(test_undefined PRIVATE cupid_core)
    foreach(group timed untimed state conditions phase dma)
        add_test(NAME arm.undefined-${group} COMMAND test_undefined ${group})
        set_tests_properties(arm.undefined-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_io tests/gba/test_io.c)
    target_link_libraries(test_io PRIVATE cupid_core)
    add_test(NAME gba.io COMMAND test_io)
    set_tests_properties(gba.io PROPERTIES TIMEOUT 10)

    add_executable(test_bios_file tests/bios/test_file.c)
    target_link_libraries(test_bios_file PRIVATE cupid_core)
    foreach(group builtin external valid nonregular)
        add_test(NAME gba.bios-file-${group} COMMAND test_bios_file ${group} ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(gba.bios-file-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME runner.bios-startup COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_bios_startup.py
        --runner $<TARGET_FILE:cupidgba-headless>)
    set_tests_properties(runner.bios-startup PROPERTIES TIMEOUT 10)
    add_test(NAME runner.checkpoint COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_runner_checkpoint.py
        --runner $<TARGET_FILE:cupidgba-headless>)
    set_tests_properties(runner.checkpoint PROPERTIES TIMEOUT 10)
    add_test(NAME runner.observations COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_runner_observations.py
        --runner $<TARGET_FILE:cupidgba-headless>)
    set_tests_properties(runner.observations PROPERTIES TIMEOUT 10)
    add_executable(test_observer tests/arm/test_observer.c)
    target_link_libraries(test_observer PRIVATE cupid_core)
    foreach(group stream boundaries)
        add_test(NAME arm.observer-${group} COMMAND test_observer ${group})
        set_tests_properties(arm.observer-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    add_test(NAME runner.fifo-comparisons COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_fifo_comparisons.py
        --runner $<TARGET_FILE:cupidgba-headless>)
    set_tests_properties(runner.fifo-comparisons PROPERTIES TIMEOUT 10)

    add_executable(test_cartridge_image tests/gba/cartridge/test_image.c)
    target_link_libraries(test_cartridge_image PRIVATE cupid_core)
    foreach(group decoder dma sizes replacement)
        add_test(NAME gba.cartridge-image-${group} COMMAND test_cartridge_image ${group} ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(gba.cartridge-image-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_rom_bus tests/gba/test_rom_bus.c)
    target_link_libraries(test_rom_bus PRIVATE cupid_core)
    foreach(group reads cpu dma eeprom)
        add_test(NAME gba.rom-bus-${group} COMMAND test_rom_bus ${group})
        set_tests_properties(gba.rom-bus-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_save_file tests/gba/save/test_file.c)
    target_link_libraries(test_save_file PRIVATE cupid_core)
    add_test(NAME gba.save-file COMMAND test_save_file ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gba.save-file PROPERTIES TIMEOUT 10)

    add_executable(test_eeprom tests/gba/save/test_eeprom.c)
    target_link_libraries(test_eeprom PRIVATE cupid_core)
    foreach(group addresses capacity reads dma)
        add_test(NAME gba.eeprom-${group} COMMAND test_eeprom ${group})
        set_tests_properties(gba.eeprom-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    add_test(NAME gba.eeprom-persistence COMMAND test_eeprom persistence ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gba.eeprom-persistence PROPERTIES TIMEOUT 10)

    add_executable(test_audio tests/gba/test_audio.c)
    target_link_libraries(test_audio PRIVATE cupid_core)
    add_test(NAME gba.audio COMMAND test_audio)
    set_tests_properties(gba.audio PROPERTIES TIMEOUT 10)

    add_executable(test_fifo_power tests/gba/audio/test_fifo_power.c)
    target_link_libraries(test_fifo_power PRIVATE cupid_core)
    foreach(group writes cpu dma phases transitions)
        add_test(NAME gba.fifo-power-${group} COMMAND test_fifo_power ${group})
        set_tests_properties(gba.fifo-power-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(test_fifo_refill tests/gba/audio/test_fifo_refill.c)
    target_link_libraries(test_fifo_refill PRIVATE cupid_core)
    foreach(group levels chronology selection)
        add_test(NAME gba.fifo-refill-${group} COMMAND test_fifo_refill ${group})
        set_tests_properties(gba.fifo-refill-${group} PROPERTIES TIMEOUT 10)
    endforeach()

    if(CUPID_FIFO_PROBE_DIR)
        foreach(case base prefetch irq threshold overflow irq-latch disable-3 disable-4 disable-5)
            add_test(NAME rom.fifo-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_fifo_probe.py
                --directory ${CUPID_FIFO_PROBE_DIR} --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.fifo-${case} PROPERTIES TIMEOUT 100)
            if(CUPID_TEST_BIOS)
                add_test(NAME rom.fifo-${case}-external COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_fifo_probe.py
                    --directory ${CUPID_FIFO_PROBE_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                    --test ${case} --bios ${CUPID_TEST_BIOS})
                set_tests_properties(rom.fifo-${case}-external PROPERTIES TIMEOUT 100)
            endif()
        endforeach()
    endif()

    if(CUPID_FIFO_SOURCE_ROM)
        add_test(NAME rom.fifo-base-source COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_fifo_probe.py
            --rom ${CUPID_FIFO_SOURCE_ROM} --runner $<TARGET_FILE:cupidgba-headless> --test base-source)
        set_tests_properties(rom.fifo-base-source PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.fifo-base-source-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_fifo_probe.py
                --rom ${CUPID_FIFO_SOURCE_ROM} --runner $<TARGET_FILE:cupidgba-headless>
                --test base-source --bios ${CUPID_TEST_BIOS})
            set_tests_properties(rom.fifo-base-source-external PROPERTIES TIMEOUT 100)
        endif()
    endif()

    add_executable(test_serial tests/gba/serial/test_serial.c)
    target_link_libraries(test_serial PRIVATE cupid_core)
    add_test(NAME gba.serial COMMAND test_serial)
    set_tests_properties(gba.serial PROPERTIES TIMEOUT 10)
    add_executable(test_multiplayer tests/gba/serial/test_multiplayer.c)
    target_link_libraries(test_multiplayer PRIVATE cupid_core)
    add_test(NAME gba.multiplayer COMMAND test_multiplayer)
    set_tests_properties(gba.multiplayer PROPERTIES TIMEOUT 10)
    add_executable(test_joybus tests/gba/serial/test_joybus.c)
    target_link_libraries(test_joybus PRIVATE cupid_core)
    foreach(group commands payloads validation registers interrupts chronology lifecycle guest)
        add_test(NAME gba.joybus-${group} COMMAND test_joybus ${group})
        set_tests_properties(gba.joybus-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.joybus-guest-external COMMAND test_joybus guest ${CUPID_TEST_BIOS})
        set_tests_properties(gba.joybus-guest-external PROPERTIES TIMEOUT 10)
    endif()

    if(CUPID_BUILD_FRONTEND)
        add_test(NAME platform.bios-startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/check_bios_startup.py
            --frontend $<TARGET_FILE:cupidgba>)
        set_tests_properties(platform.bios-startup PROPERTIES TIMEOUT 10)
        add_executable(test_input tests/platform/test_input.c src/platform/input.c)
        target_link_libraries(test_input PRIVATE cupid_core SDL2::SDL2)
        foreach(group bindings mixed axes lifecycle irq)
            add_test(NAME platform.input-${group} COMMAND test_input ${group})
            set_tests_properties(platform.input-${group} PROPERTIES TIMEOUT 10)
        endforeach()
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

    add_executable(test_bios_music tests/bios/test_music.c)
    target_link_libraries(test_bios_music PRIVATE cupid_core)
    add_test(NAME gba.bios-music COMMAND test_bios_music)
    set_tests_properties(gba.bios-music PROPERTIES TIMEOUT 10)
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-music-external COMMAND test_bios_music --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-music-external PROPERTIES TIMEOUT 10)
    endif()

    add_executable(test_bios_checksum tests/bios/test_checksum.c)
    target_link_libraries(test_bios_checksum PRIVATE cupid_core)
    foreach(bank RANGE 0 6)
        add_test(NAME gba.bios-checksum-bank-${bank} COMMAND test_bios_checksum --bank ${bank})
        set_tests_properties(gba.bios-checksum-bank-${bank} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-checksum-bank-${bank}-external COMMAND test_bios_checksum --bank ${bank} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-checksum-bank-${bank}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    foreach(source RANGE 0 1)
        add_test(NAME gba.bios-checksum-source-${source} COMMAND test_bios_checksum --source ${source})
        set_tests_properties(gba.bios-checksum-source-${source} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-checksum-source-${source}-external COMMAND test_bios_checksum --source ${source} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-checksum-source-${source}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    foreach(group words interactions)
        add_test(NAME gba.bios-checksum-${group} COMMAND test_bios_checksum --${group})
        set_tests_properties(gba.bios-checksum-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-checksum-${group}-external COMMAND test_bios_checksum --${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-checksum-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()

    add_executable(test_bios_sound_driver tests/bios/test_sound_driver.c)
    target_link_libraries(test_bios_sound_driver PRIVATE cupid_core)
    foreach(group mode vsync clear pcm rates-0 rates-1 rates-2 chronology init
                  frames-0 frames-1 frames-2 frames-3 hooks mix-irq playback
                  mix-rates-0 mix-rates-1 mix-rates-2)
        add_test(NAME gba.bios-sound-${group} COMMAND test_bios_sound_driver ${group})
        set_tests_properties(gba.bios-sound-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-sound-${group}-external COMMAND test_bios_sound_driver ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-sound-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()

    add_executable(test_bios_music_player tests/bios/test_music_player.c)
    target_link_libraries(test_bios_music_player PRIVATE cupid_core)
    foreach(group open open-guards discovery start stop continue fade guards sequence irq
                  clear-entry unlink-entry controllers-0 controllers-1 controllers-2 controllers-3
                  controllers-4 fine-entry flow-entry port-entry rate-entry-0 rate-entry-1 rate-entry-2
                  stop-entry fade-entry tie-entry source-guard track-entry songs-0 songs-1 songs-2 songs-3
                  songs-4 hooks-arm hooks-thumb playback-0 playback-1 playback-2 progress)
        add_test(NAME gba.bios-music-player-${group} COMMAND test_bios_music_player ${group})
        set_tests_properties(gba.bios-music-player-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-music-player-${group}-external COMMAND test_bios_music_player ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-music-player-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()

    add_executable(test_bios_multiboot tests/bios/test_multiboot.c)
    target_link_libraries(test_bios_multiboot PRIVATE cupid_core)
    foreach(group guards normal-0 normal-1 normal-2 normal-3 multi-0 multi-1
                  crc-reject offset-reject client-wait client-reject disconnect
                  source-iwram source-rom-page bus-0 bus-1
                  guard-matrix-0 guard-matrix-1 guard-matrix-2 guard-readonly guard-dma
                  multi-2 multi-sparse multi-unselected multi-offset-reject multi-crc-reject)
        add_test(NAME gba.bios-multiboot-${group} COMMAND test_bios_multiboot ${group})
        set_tests_properties(gba.bios-multiboot-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-multiboot-${group}-external COMMAND test_bios_multiboot ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-multiboot-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()

    # Full-size protocol execution is a separate end-to-end profile. Existing
    # small-download guards and ten-second deadlines remain unchanged.
    add_test(NAME gba.bios-multiboot-maximum COMMAND test_bios_multiboot maximum)
    set_tests_properties(gba.bios-multiboot-maximum PROPERTIES TIMEOUT 180 LABELS extended)
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-multiboot-maximum-external COMMAND test_bios_multiboot maximum --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-multiboot-maximum-external PROPERTIES TIMEOUT 180 LABELS extended)
    endif()

    add_executable(test_bios_power tests/bios/test_power.c)
    target_link_libraries(test_bios_power PRIVATE cupid_core)
    foreach(batch RANGE 0 3)
        add_test(NAME gba.bios-power-controls-${batch} COMMAND test_bios_power --controls ${batch})
        set_tests_properties(gba.bios-power-controls-${batch} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-power-controls-${batch}-external COMMAND test_bios_power --controls ${batch} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-power-controls-${batch}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    add_test(NAME gba.bios-power-state COMMAND test_bios_power --state)
    set_tests_properties(gba.bios-power-state PROPERTIES TIMEOUT 10)
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-power-state-external COMMAND test_bios_power --state --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-power-state-external PROPERTIES TIMEOUT 10)
    endif()

    add_executable(test_bios_data_services tests/bios/test_data_services.c)
    target_link_libraries(test_bios_data_services PRIVATE cupid_core)
    add_test(NAME gba.bios-data-services COMMAND test_bios_data_services --ewram)
    set_tests_properties(gba.bios-data-services PROPERTIES TIMEOUT 10)
    add_test(NAME gba.bios-data-internal COMMAND test_bios_data_services --iwram)
    set_tests_properties(gba.bios-data-internal PROPERTIES TIMEOUT 10)
    add_test(NAME gba.bios-data-cartridge COMMAND test_bios_data_services --cartridge)
    set_tests_properties(gba.bios-data-cartridge PROPERTIES TIMEOUT 10)
    add_test(NAME gba.bios-bias COMMAND test_bios_data_services --bias)
    set_tests_properties(gba.bios-bias PROPERTIES TIMEOUT 10)

    add_executable(test_bios_affine tests/bios/test_affine.c)
    add_executable(test_bios_soft_reset tests/bios/test_soft_reset.c)
    add_executable(test_bios_hard_reset tests/bios/test_hard_reset.c)
    target_link_libraries(test_bios_hard_reset PRIVATE cupid_core)
    foreach(group RANGE 0 3)
        add_test(NAME gba.bios-hard-reset-prefix-${group} COMMAND test_bios_hard_reset --prefix ${group})
        set_tests_properties(gba.bios-hard-reset-prefix-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-hard-reset-prefix-${group}-external COMMAND test_bios_hard_reset --prefix ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-hard-reset-prefix-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    foreach(group RANGE 0 7)
        add_test(NAME gba.bios-hard-reset-complete-${group} COMMAND test_bios_hard_reset --complete ${group})
        set_tests_properties(gba.bios-hard-reset-complete-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    foreach(group chronology repeat post-byte startup-irq)
        add_test(NAME gba.bios-hard-reset-${group} COMMAND test_bios_hard_reset --${group})
        set_tests_properties(gba.bios-hard-reset-${group} PROPERTIES TIMEOUT 10)
    endforeach()
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-hard-reset-chronology-external COMMAND test_bios_hard_reset --chronology --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-hard-reset-chronology-external PROPERTIES TIMEOUT 10)
    endif()
    target_link_libraries(test_bios_soft_reset PRIVATE cupid_core)
    foreach(group RANGE 0 3)
        add_test(NAME gba.bios-soft-reset-${group} COMMAND test_bios_soft_reset --bytes ${group})
        set_tests_properties(gba.bios-soft-reset-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-soft-reset-${group}-external COMMAND test_bios_soft_reset --bytes ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-soft-reset-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    foreach(part states dma)
        add_test(NAME gba.bios-soft-reset-${part} COMMAND test_bios_soft_reset --${part})
        set_tests_properties(gba.bios-soft-reset-${part} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-soft-reset-${part}-external COMMAND test_bios_soft_reset --${part} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-soft-reset-${part}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    add_executable(test_bios_register_reset tests/bios/test_register_reset.c)
    target_link_libraries(test_bios_register_reset PRIVATE cupid_core)
    foreach(group RANGE 0 15)
        add_test(NAME gba.bios-register-reset-${group} COMMAND test_bios_register_reset --group ${group})
        set_tests_properties(gba.bios-register-reset-${group} PROPERTIES TIMEOUT 10)
        if(CUPID_TEST_BIOS)
            add_test(NAME gba.bios-register-reset-${group}-external COMMAND test_bios_register_reset --group ${group} --external-bios ${CUPID_TEST_BIOS})
            set_tests_properties(gba.bios-register-reset-${group}-external PROPERTIES TIMEOUT 10)
        endif()
    endforeach()
    add_test(NAME gba.bios-register-reset-interactions COMMAND test_bios_register_reset --interactions)
    set_tests_properties(gba.bios-register-reset-interactions PROPERTIES TIMEOUT 10)
    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-register-reset-interactions-external COMMAND test_bios_register_reset --interactions --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-register-reset-interactions-external PROPERTIES TIMEOUT 10)
    endif()
    target_link_libraries(test_bios_affine PRIVATE cupid_core)
    if(WIN32)
        target_link_libraries(test_bios_affine PRIVATE m)
    endif()
    add_test(NAME gba.bios-affine-background COMMAND test_bios_affine --background)
    add_test(NAME gba.bios-affine-object COMMAND test_bios_affine --object)
    set_tests_properties(gba.bios-affine-background gba.bios-affine-object PROPERTIES TIMEOUT 10)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME gba.bios-affine-table COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bios/generate_affine_table.py --check)

    if(CUPID_TEST_BIOS)
        add_test(NAME gba.bios-affine-background-external COMMAND test_bios_affine --background --external-bios ${CUPID_TEST_BIOS})
        add_test(NAME gba.bios-affine-object-external COMMAND test_bios_affine --object --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-affine-background-external gba.bios-affine-object-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-external COMMAND test_bios --external-bios-waits ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-math-copy-external COMMAND test_bios_math_copy --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-math-copy-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-data-services-external COMMAND test_bios_data_services --ewram --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-data-services-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-data-internal-external COMMAND test_bios_data_services --iwram --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-data-internal-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-data-cartridge-external COMMAND test_bios_data_services --cartridge --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-data-cartridge-external PROPERTIES TIMEOUT 10)
        add_test(NAME gba.bios-bias-external COMMAND test_bios_data_services --bias --external-bios ${CUPID_TEST_BIOS})
        set_tests_properties(gba.bios-bias-external PROPERTIES TIMEOUT 10)
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

    if(CUPID_HALT_PROBE_DIR AND CUPID_TEST_BIOS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(case halt_pc halt_pc_2 halt_pc_3 halt_pc_4)
            add_test(NAME rom.halt-probe-${case}-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_serial_probe.py
                --directory ${CUPID_HALT_PROBE_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                --bios ${CUPID_TEST_BIOS} --test ${case})
            set_tests_properties(rom.halt-probe-${case}-external PROPERTIES TIMEOUT 100)
        endforeach()
    endif()

    if(CUPID_SERIAL_PROBE_DIR)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(case serial_time serial_time_2 serial_time_3 serial_time_2M serial_time_2M_2
                     serial_time_2M_3 serial_read_data serial_time_start_bit serial_time_start_bit_2)
            add_test(NAME rom.serial-probe-${case} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_serial_probe.py
                --directory ${CUPID_SERIAL_PROBE_DIR} --runner $<TARGET_FILE:cupidgba-headless> --test ${case})
            set_tests_properties(rom.serial-probe-${case} PROPERTIES TIMEOUT 100)
            if(CUPID_TEST_BIOS)
                add_test(NAME rom.serial-probe-${case}-external COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_serial_probe.py
                    --directory ${CUPID_SERIAL_PROBE_DIR} --runner $<TARGET_FILE:cupidgba-headless>
                    --bios ${CUPID_TEST_BIOS} --test ${case})
                set_tests_properties(rom.serial-probe-${case}-external PROPERTIES TIMEOUT 100)
            endif()
        endforeach()
    endif()

    if(CUPID_SERIAL_SUITE_ROM)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME rom.serial-suite COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_serial_suite.py
            --rom ${CUPID_SERIAL_SUITE_ROM} --runner $<TARGET_FILE:cupidgba-headless>)
        set_tests_properties(rom.serial-suite PROPERTIES TIMEOUT 100)
        if(CUPID_TEST_BIOS)
            add_test(NAME rom.serial-suite-external COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/roms/check_serial_suite.py
                --rom ${CUPID_SERIAL_SUITE_ROM} --bios ${CUPID_TEST_BIOS}
                --runner $<TARGET_FILE:cupidgba-headless>)
            set_tests_properties(rom.serial-suite-external PROPERTIES TIMEOUT 100)
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
