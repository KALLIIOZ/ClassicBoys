cmake_minimum_required(VERSION 3.20)
project(ClassicBoy VERSION 2.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacion" FORCE)
endif()

option(CB_BUILD_TESTS "Compilar las pruebas del nucleo" ON)

# ---------------------------------------------------------------------------
# SDL2: usa la copia incluida en third_party si existe.
# ---------------------------------------------------------------------------
set(CB_BUNDLED_SDL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.32.10/x86_64-w64-mingw32")

if(WIN32 AND EXISTS "${CB_BUNDLED_SDL_DIR}/lib/cmake/SDL2")
    list(PREPEND CMAKE_PREFIX_PATH "${CB_BUNDLED_SDL_DIR}")
    set(SDL2_DIR "${CB_BUNDLED_SDL_DIR}/lib/cmake/SDL2"
        CACHE PATH "Directorio de configuracion de SDL2")
endif()

find_package(SDL2 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Fuentes
# ---------------------------------------------------------------------------
set(CB_CORE_SOURCES
    src/apu.c
    src/cart.c
    src/common.c
    src/cpu.c
    src/gb.c
    src/joypad.c
    src/mmu.c
    src/ppu.c
    src/timer.c
)

set(CB_FRONTEND_SOURCES
    frontend/main.c
    frontend/pages.c
    frontend/roms.c
    frontend/ui.c
    frontend/font.c
    frontend/stb_truetype_impl.c
)

set(CB_TEST_SOURCES
    tests/test_main.c
    tests/test_util.c
    tests/test_cpu.c
    tests/test_memory.c
    tests/test_ppu.c
    tests/test_apu.c
    tests/test_cart.c
    tests/test_cgb.c
    tests/test_integration.c
    tests/test_stability.c
)

# ---------------------------------------------------------------------------
# Aplicacion
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(classicboy WIN32 ${CB_CORE_SOURCES} ${CB_FRONTEND_SOURCES})
else()
    add_executable(classicboy ${CB_CORE_SOURCES} ${CB_FRONTEND_SOURCES})
endif()

target_include_directories(classicboy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

target_link_libraries(classicboy PRIVATE SDL2::SDL2main SDL2::SDL2)
if(NOT WIN32)
    target_link_libraries(classicboy PRIVATE m)
endif()

if(MSVC)
    target_compile_options(classicboy PRIVATE /W4)
else()
    target_compile_options(classicboy PRIVATE -Wall -Wextra)
endif()

set_target_properties(classicboy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(classicboy PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# Copia la DLL y los recursos junto al ejecutable.
if(WIN32)
    add_custom_command(TARGET classicboy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:classicboy>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:classicboy>/assets"
        COMMENT "Copiando SDL2.dll y assets junto al ejecutable"
    )
endif()

# ---------------------------------------------------------------------------
# Pruebas
# ---------------------------------------------------------------------------
if(CB_BUILD_TESTS)
    add_executable(classicboy_tests ${CB_CORE_SOURCES} ${CB_TEST_SOURCES})
    target_include_directories(classicboy_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    if(NOT WIN32)
        target_link_libraries(classicboy_tests PRIVATE m)
    endif()
    if(MSVC)
        target_compile_options(classicboy_tests PRIVATE /W4)
    else()
        target_compile_options(classicboy_tests PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(classicboy_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(classicboy_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
    endforeach()

    enable_testing()
    add_test(NAME classicboy_tests COMMAND classicboy_tests)
endif()
