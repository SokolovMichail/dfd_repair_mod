cmake_minimum_required(VERSION 3.20)
project(DFDRepairMod C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The game is 64-bit: an x86_64 toolchain is required")
endif ()

set(MOD_VERSION "1.0")
set(GAME_DIR "C:/Games/Terminator Dark Fate - Defiance" CACHE PATH "Game folder (files for the mod build, deploy)")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Common build settings for the mod DLLs: no dependency on the MinGW runtime (libstdc++/libgcc/winpthread).
function(dfd_flags target)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-cast-function-type -mcrtdll=ucrt)
    # Everything static; the CRT is the system UCRT (this MinGW's static libwinpthread is built for UCRT).
    # --no-insert-timestamp: no link time in the PE header and the export table, so the same sources and
    # toolchain give a byte-identical DLL (a reproducible release).
    target_link_options(${target} PRIVATE -static -mcrtdll=ucrt -Wl,--no-insert-timestamp $<$<CONFIG:Release>:-s>)
endfunction()

# A mod DLL: common settings + a name without the lib prefix.
function(dfd_dll target)
    set_target_properties(${target} PROPERTIES PREFIX "")
    dfd_flags(${target})
endfunction()

# ---- winhttp.dll: proxy + plugin loader (bin\winhttp.dll)
add_library(winhttp SHARED
        loader/dllmain.cpp
        loader/winhttp_exports.S
        loader/winhttp.def)
dfd_dll(winhttp)

# ---- plugins (bin\plugins\*.dll)
# The crash guard (common/guard.h): the plugins run their own code inside the game under it.
set(DFD_GUARD_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/common/guard.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/guard_x64.S")
# Plugin sources - shared by the DLL and the tests (tests/). core/ - the game-agnostic rules (no Windows, no
# logic.dll), logic_dll/ - the binding to the current game build.
set(SPARES_SUPPLY_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/repair.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/spares.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/spares_supply/core/supply.cpp")
set(SPARES_SUPPLY_SOURCES ${SPARES_SUPPLY_CORE_SOURCES} ${DFD_GUARD_SOURCES}
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

# ---- mod: build the data from the current game files -> <build>/stage/mods/: two mods - the framework mini-mod
# SparesSupplyRepairFramework (zone circles, any framework vehicle needs it) and example mod HEMTT_Spares (vehicles).
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
# mod_tablemerge: HEMTT Spares once more as its table-merge build (build_mod.py --tables patch: table patches for
# DefianceLoader's defiance.table-merge, the vehicles only) -> <build>/stage/mods_tablemerge/; the framework mini-mod
# built beside it is the same as above.
set(TM_STAGE "${CMAKE_BINARY_DIR}/stage/mods_tablemerge")
add_custom_target(mod_tablemerge
        COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${TM_STAGE}/HEMTT_Spares"
                --framework-out "${TM_STAGE}/${FRAMEWORK_MOD}" --cache "${GAME_SRC}" --version ${MOD_VERSION}
                --tables patch
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Build the table-merge mod data -> ${TM_STAGE}"
        USES_TERMINAL VERBATIM)
# Both mods into the game (replacing what is there); shared by deploy and deploy_defiance (stage - where they are).
function(dfd_deploy_mods var stage)
    set(${var}
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAME_DIR}/mods/${FRAMEWORK_MOD}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage}/${FRAMEWORK_MOD}" "${GAME_DIR}/mods/${FRAMEWORK_MOD}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAME_DIR}/mods/HEMTT_Spares"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage}/HEMTT_Spares" "${GAME_DIR}/mods/HEMTT_Spares"
            PARENT_SCOPE)
endfunction()
dfd_deploy_mods(DEPLOY_MODS "${CMAKE_BINARY_DIR}/stage/mods")
dfd_deploy_mods(DEPLOY_MODS_TABLEMERGE "${TM_STAGE}")

# ---- deploy: install the DLLs and the mod into the game (the game must be closed)
# deploy and deploy_defiance install one of the two mod builds and remove the other one from the game (only its
# files: DefianceLoader itself, bin\winhttp.dll and other plugins are not touched), so you can switch between them
# for testing. Both at once are not wanted: then the DefianceLoader plugin refuses to work.
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
# ---- deploy_defiance_tablemerge: the same with the table-merge build of HEMTT Spares (for testing it; the game
# needs [defiance.table-merge] enabled = true in DefianceLoader\config\settings.ini). deploy_defiance goes back.
add_custom_target(deploy_defiance_tablemerge
        COMMAND ${CMAKE_COMMAND} -E rm -f "${GAME_DIR}/bin/plugins/SparesSupply.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/DefianceLoader/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:spares_repair>
                "${CMAKE_CURRENT_BINARY_DIR}/spares_repair.plugin.json" "${GAME_DIR}/DefianceLoader/plugins/"
        ${DEPLOY_MODS_TABLEMERGE}
        DEPENDS spares_repair mod_tablemerge
        COMMENT "Install the DefianceLoader plugin and the table-merge mods into ${GAME_DIR}"
        VERBATIM)

# ---- release: "build a release" -> dist/: two packages of the standalone build (our winhttp.dll + SparesSupply.dll):
#   SparesSupplyRepairFramework_v<version>.zip - the framework only (DLLs + the mini-mod with the zone circles),
#   HEMTT_Spares_v<version>.zip - the framework + the HEMTT Spares example mod (+ cover and preview for the mod page).
# The DLLs are always built by a separate nested Release build, regardless of the CLion profile.
# release_defiance - the same for the DefianceLoader plugin: SparesSupplyRepairFramework_DefianceLoader_v<version>.zip
# and HEMTT_Spares_DefianceLoader_v<version>.zip (same nested build and data folders; shared cover and preview), plus
# HEMTT_Spares_DefianceLoader_TableMerge_v<version>.zip - HEMTT Spares as table-merge patches (vehicles only).
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
    # the table-merge build of HEMTT Spares, for the third DefianceLoader package
    set(REL_TM "${REL_DIR}/stage/mods_tablemerge")
    set(REL_MOD_DATA_TABLEMERGE
            COMMAND ${Python3_EXECUTABLE} mod/build_mod.py --game "${GAME_DIR}" --out "${REL_TM}/HEMTT_Spares"
                    --framework-out "${REL_TM}/${FRAMEWORK_MOD}" --cache "${REL_DIR}/stage/_game_src"
                    --version ${MOD_VERSION} --tables patch)
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
            ${REL_MOD_DATA_TABLEMERGE}
            COMMAND ${Python3_EXECUTABLE} mod/package.py ${DEFIANCE_ARGS} ${PACKAGE_COMMON} --tablemerge
                    --mod "${REL_TM}/HEMTT_Spares" --game-src "${REL_DIR}/stage/_game_src/base"
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
