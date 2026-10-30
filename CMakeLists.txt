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
# The standalone DLL (bin\plugins\SparesSupply.dll, loaded by our winhttp.dll): the shared sources + its entry
# point. The tests build the shared sources without it.
add_library(SparesSupply SHARED ${SPARES_SUPPLY_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/logic_dll/standalone.cpp")
dfd_dll(SparesSupply)
target_compile_definitions(SparesSupply PRIVATE "SPARES_SUPPLY_VERSION=\"${MOD_VERSION}\"")

# The same plugin for DefianceLoader (DefianceLoader\plugins\spares_repair.dll + spares_repair.plugin.json): the
# shared sources + the loader's entry point. The manifest is generated next to the DLL, with MOD_VERSION.
add_library(spares_repair SHARED ${SPARES_SUPPLY_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/logic_dll/defiance.cpp")
dfd_dll(spares_repair)
target_compile_definitions(spares_repair PRIVATE "SPARES_SUPPLY_VERSION=\"${MOD_VERSION}\"")
configure_file(plugins/spares_supply/spares_repair.plugin.json.in
        "${CMAKE_CURRENT_BINARY_DIR}/spares_repair.plugin.json" @ONLY)

# ---- mod: собрать данные из актуальных файлов игры -> <build>/stage/mods/: два мода - мини-мод фреймворка
# SparesSupplyRepairFramework (круги зоны, нужен любой машине на фреймворке) и мод-пример HEMTT_Spares (машины).
set(FRAMEWORK_MOD "SparesSupplyRepairFramework")
set(MOD_STAGE "${CMAKE_BINARY_DIR}/stage/mods/HEMTT_Spares")
set(FRAMEWORK_STAGE "${CMAKE_BINARY_DIR}/stage/mods/${FRAMEWORK_MOD}")
set(GAME_SRC "${CMAKE_BINARY_DIR}/stage/_game_src")
add_custom_target(mod
        COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${MOD_STAGE}"
                --framework-out "${FRAMEWORK_STAGE}" --cache "${GAME_SRC}" --version ${MOD_VERSION}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Build mod data -> ${FRAMEWORK_STAGE}, ${MOD_STAGE}"
        USES_TERMINAL VERBATIM)
# Both mods into the game (replacing what is there); shared by deploy and deploy_defiance.
set(DEPLOY_MODS
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAME_DIR}/mods/${FRAMEWORK_MOD}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FRAMEWORK_STAGE}" "${GAME_DIR}/mods/${FRAMEWORK_MOD}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAME_DIR}/mods/HEMTT_Spares"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${MOD_STAGE}" "${GAME_DIR}/mods/HEMTT_Spares")

# ---- deploy: поставить в игру DLL и мод (игра должна быть закрыта)
# deploy и deploy_defiance ставят одну из двух версий мода и убирают из игры другую (только её файлы: сам
# DefianceLoader, bin\winhttp.dll и чужие плагины не трогаются), так что между ними можно переключаться для
# проверки. Обе сразу не нужны: тогда плагин DefianceLoader отказывается работать.
add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E rm -f "${GAME_DIR}/DefianceLoader/plugins/spares_repair.dll"
                "${GAME_DIR}/DefianceLoader/plugins/spares_repair.plugin.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/bin/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:winhttp> "${GAME_DIR}/bin/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SparesSupply> "${GAME_DIR}/bin/plugins/"
        ${DEPLOY_MODS}
        DEPENDS winhttp SparesSupply mod
        COMMENT "Install DLLs and mods into ${GAME_DIR}"
        VERBATIM)

# ---- deploy_defiance: the same, but as a DefianceLoader plugin (DefianceLoader must be installed). Removes the
# standalone plugin bin\plugins\SparesSupply.dll; bin\winhttp.dll stays (without plugins it only forwards calls).
add_custom_target(deploy_defiance
        COMMAND ${CMAKE_COMMAND} -E rm -f "${GAME_DIR}/bin/plugins/SparesSupply.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/DefianceLoader/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:spares_repair>
                "${CMAKE_CURRENT_BINARY_DIR}/spares_repair.plugin.json" "${GAME_DIR}/DefianceLoader/plugins/"
        ${DEPLOY_MODS}
        DEPENDS spares_repair mod
        COMMENT "Install the DefianceLoader plugin and mods into ${GAME_DIR}"
        VERBATIM)

# ---- release: «собрать релиз» -> dist/: два комплекта отдельной версии (наш winhttp.dll + SparesSupply.dll):
#   SparesSupplyRepairFramework_v<версия>.zip - только фреймворк (DLL + мини-мод с кругами зоны),
#   HEMTT_Spares_v<версия>.zip - фреймворк + мод-пример HEMTT Spares (+ обложка и превью для страницы мода).
# DLL всегда собираются отдельной вложенной сборкой Release, независимо от профиля CLion.
# release_defiance - то же для плагина DefianceLoader: SparesSupplyRepairFramework_DefianceLoader_v<версия>.zip и
# HEMTT_Spares_DefianceLoader_v<версия>.zip (та же вложенная сборка, те же папки данных; обложка и превью общие).
if (NOT DFD_NESTED_RELEASE)
    set(REL_DIR "${CMAKE_BINARY_DIR}/release")
    # Shared steps of both releases: configure the nested Release build, build the mod data.
    set(REL_CONFIGURE
            COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${REL_DIR}" -G "${CMAKE_GENERATOR}"
                    -DCMAKE_BUILD_TYPE=Release -DDFD_NESTED_RELEASE=ON
                    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}"
                    "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}" "-DGAME_DIR=${GAME_DIR}")
    set(REL_MODS "${REL_DIR}/stage/mods")
    set(REL_MOD_DATA
            COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${REL_MODS}/HEMTT_Spares"
                    --framework-out "${REL_MODS}/${FRAMEWORK_MOD}" --cache "${REL_DIR}/stage/_game_src"
                    --version ${MOD_VERSION})
    # package.py arguments of each build; adding EXAMPLE_ARGS packs HEMTT Spares too.
    set(PACKAGE_COMMON --framework-mod "${REL_MODS}/${FRAMEWORK_MOD}" --version ${MOD_VERSION}
            --out "${CMAKE_SOURCE_DIR}/dist")
    set(EXAMPLE_ARGS --mod "${REL_MODS}/HEMTT_Spares" --game-src "${REL_DIR}/stage/_game_src/base")
    set(STANDALONE_ARGS --winhttp "${REL_DIR}/winhttp.dll" --plugin "${REL_DIR}/SparesSupply.dll")
    set(DEFIANCE_ARGS --defiance --plugin "${REL_DIR}/spares_repair.dll"
            --manifest "${REL_DIR}/spares_repair.plugin.json")
    add_custom_target(release
            ${REL_CONFIGURE}
            COMMAND ${CMAKE_COMMAND} --build "${REL_DIR}" --target winhttp SparesSupply
            ${REL_MOD_DATA}
            COMMAND ${Python3_EXECUTABLE} mod/package.py ${STANDALONE_ARGS} ${PACKAGE_COMMON}
            COMMAND ${Python3_EXECUTABLE} mod/package.py ${STANDALONE_ARGS} ${PACKAGE_COMMON} ${EXAMPLE_ARGS}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Release the framework and HEMTT Spares ${MOD_VERSION} -> dist/"
            USES_TERMINAL VERBATIM)
    add_custom_target(release_defiance
            ${REL_CONFIGURE}
            COMMAND ${CMAKE_COMMAND} --build "${REL_DIR}" --target spares_repair
            ${REL_MOD_DATA}
            COMMAND ${Python3_EXECUTABLE} mod/package.py ${DEFIANCE_ARGS} ${PACKAGE_COMMON}
            COMMAND ${Python3_EXECUTABLE} mod/package.py ${DEFIANCE_ARGS} ${PACKAGE_COMMON} ${EXAMPLE_ARGS}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Release the framework and HEMTT Spares ${MOD_VERSION} for DefianceLoader -> dist/"
            USES_TERMINAL VERBATIM)
endif ()

# ---- plugin tests (tests/): cmake --build <build> && ctest --test-dir <build>
option(DFD_TESTS "Build the SparesSupply plugin tests" ON)
if (DFD_TESTS AND NOT DFD_NESTED_RELEASE)
    enable_testing()
    add_subdirectory(tests)
endif ()
