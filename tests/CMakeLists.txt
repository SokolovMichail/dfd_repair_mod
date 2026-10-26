# Tests of the SparesSupply plugin. They do not need the game: logic.dll and engine objects are replaced by
# fakes (tests/support) built from the same assumptions the plugin is built on. So a game update does not
# break the tests: they check that the plugin behaves as before, not that the game has not changed.
#
#   patch_h_test         - shared plugin code common/patch.h (pattern search, RTTI, patch set, log)
#   plugin_test          - hook behaviour: F4 mode, zone circle, repair and armor, paying with spare parts
#   plugin_install_test  - finding addresses and applying patches in a fake logic.dll ("all or nothing")

# A test is an ordinary doctest program run by CTest. It is not built like the mod DLLs (-static
# -mcrtdll=ucrt): in this MinGW libstdc++.a is built for msvcrt and libwinpthread.a for UCRT, and the I/O
# streams doctest uses do not link that way. So libstdc++/libgcc are linked statically, and
# libwinpthread-1.dll is copied next to the tests from the compiler folder.
get_filename_component(DFD_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
function(dfd_test name)
    add_executable(${name} ${ARGN})
    target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-cast-function-type)
    target_link_options(${name} PRIVATE -static-libstdc++ -static-libgcc)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if (EXISTS "${DFD_MINGW_BIN}/libwinpthread-1.dll")
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DFD_MINGW_BIN}/libwinpthread-1.dll"
                        "$<TARGET_FILE_DIR:${name}>")
    endif ()
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "$<TARGET_FILE_DIR:${name}>")
endfunction()

# A tiny DLL the DllWatcher test loads itself to see the load notification.
add_library(dfd_watcher_test SHARED watcher_dll/watcher_dll.cpp)
dfd_dll(dfd_watcher_test)

dfd_test(patch_h_test patch_h_test.cpp)
add_dependencies(patch_h_test dfd_watcher_test)
target_compile_definitions(patch_h_test PRIVATE "WATCHER_DLL_NAME=\"$<TARGET_FILE_NAME:dfd_watcher_test>\"")

dfd_test(plugin_test plugin_test.cpp ${SPARES_SUPPLY_SOURCES})
dfd_test(plugin_install_test plugin_install_test.cpp ${SPARES_SUPPLY_SOURCES})
