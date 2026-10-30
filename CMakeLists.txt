cmake_minimum_required(VERSION 3.20)
project(DFDRepairMod C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Игра 64-битная: нужен x86_64 тулчейн")
endif ()

set(MOD_VERSION "0.1")
set(GAME_DIR "C:/Games/Terminator Dark Fate - Defiance" CACHE PATH "Папка игры (файлы для сборки мода, deploy)")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Common build settings for the mod DLLs: no dependency on the MinGW runtime (libstdc++/libgcc/winpthread).
function(dfd_flags target)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-cast-function-type -mcrtdll=ucrt)
    # Всё статически; CRT - системный UCRT (статическая libwinpthread этого MinGW собрана под UCRT).
    target_link_options(${target} PRIVATE -static -mcrtdll=ucrt $<$<CONFIG:Release>:-s>)
endfunction()

# A mod DLL: common settings + a name without the lib prefix.
function(dfd_dll target)
    set_target_properties(${target} PROPERTIES PREFIX "")
    dfd_flags(${target})
endfunction()

# ---- winhttp.dll: прокси + загрузчик плагинов (bin\winhttp.dll)
add_library(winhttp SHARED
        loader/dllmain.cpp
        loader/winhttp_exports.S
        loader/winhttp.def)
dfd_dll(winhttp)

# ---- плагины (bin\plugins\*.dll)
# Plugin sources - shared by the DLL and the tests (tests/). core/ - the game-agnostic rules (no Windows, no
# logic.dll), logic_dll/ - the binding to the current game build.
set(SPARES_SUPPLY_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/repair.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/spares.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/supply.cpp")
set(SPARES_SUPPLY_SOURCES ${SPARES_SUPPLY_CORE_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/logic_dll/common.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/logic_dll/spares.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/logic_dll/repair.cpp")
add_library(SparesSupply SHARED ${SPARES_SUPPLY_SOURCES})
dfd_dll(SparesSupply)
target_compile_definitions(SparesSupply PRIVATE "SPARES_SUPPLY_VERSION=\"${MOD_VERSION}\"")

# ---- mod: собрать папку мода (данные) из актуальных файлов игры -> <build>/stage/mods/HEMTT_Spares
set(MOD_STAGE "${CMAKE_BINARY_DIR}/stage/mods/HEMTT_Spares")
set(GAME_SRC "${CMAKE_BINARY_DIR}/stage/_game_src")
add_custom_target(mod
        COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${MOD_STAGE}"
                --cache "${GAME_SRC}" --version ${MOD_VERSION}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Build mod data -> ${MOD_STAGE}"
        USES_TERMINAL VERBATIM)

# ---- deploy: поставить в игру DLL и мод (игра должна быть закрыта)
add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/bin/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:winhttp> "${GAME_DIR}/bin/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SparesSupply> "${GAME_DIR}/bin/plugins/"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAME_DIR}/mods/HEMTT_Spares"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${MOD_STAGE}" "${GAME_DIR}/mods/HEMTT_Spares"
        DEPENDS winhttp SparesSupply mod
        COMMENT "Install DLLs and mod into ${GAME_DIR}"
        VERBATIM)

# ---- release: «собрать релиз» -> dist/HEMTT_Spares_v<версия>.zip (+ обложка)
# DLL всегда собираются отдельной вложенной сборкой Release, независимо от профиля CLion.
if (NOT DFD_NESTED_RELEASE)
    set(REL_DIR "${CMAKE_BINARY_DIR}/release")
    add_custom_target(release
            COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${REL_DIR}" -G "${CMAKE_GENERATOR}"
                    -DCMAKE_BUILD_TYPE=Release -DDFD_NESTED_RELEASE=ON
                    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}"
                    "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}" "-DGAME_DIR=${GAME_DIR}"
            COMMAND ${CMAKE_COMMAND} --build "${REL_DIR}" --target winhttp SparesSupply
            COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${REL_DIR}/stage/mods/HEMTT_Spares"
                    --cache "${REL_DIR}/stage/_game_src" --version ${MOD_VERSION}
            COMMAND ${Python3_EXECUTABLE} mod/package.py --mod "${REL_DIR}/stage/mods/HEMTT_Spares"
                    --winhttp "${REL_DIR}/winhttp.dll" --plugin "${REL_DIR}/SparesSupply.dll"
                    --game-src "${REL_DIR}/stage/_game_src/base" --version ${MOD_VERSION}
                    --out "${CMAKE_SOURCE_DIR}/dist"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Release HEMTT Spares ${MOD_VERSION} -> dist/"
            USES_TERMINAL VERBATIM)
endif ()

# ---- plugin tests (tests/): cmake --build <build> && ctest --test-dir <build>
option(DFD_TESTS "Build the SparesSupply plugin tests" ON)
if (DFD_TESTS AND NOT DFD_NESTED_RELEASE)
    enable_testing()
    add_subdirectory(tests)
endif ()
