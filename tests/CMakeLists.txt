# Тесты плагина SparesSupply. Игра для них не нужна: вместо logic.dll и объектов движка - подделки
# (tests/support), построенные по тем же предположениям, что зашиты в плагин. Поэтому обновление игры
# тесты не ломает: они проверяют, что плагин ведёт себя как прежде, а не что игра не изменилась.
#
#   patch_h_test         - общий код плагинов common/patch.h (поиск по шаблонам, RTTI, набор правок, лог)
#   plugin_test          - поведение перехватов: режим F4, круг зоны, ремонт и броня, оплата запчастями
#   plugin_install_test  - поиск адресов и установка правок в поддельной logic.dll («всё или ничего»)

# Тест - обычная программа на doctest; запускается CTest. Собирается не как DLL мода (-static
# -mcrtdll=ucrt): в этом MinGW libstdc++.a собрана под msvcrt, а libwinpthread.a - под UCRT, и потоки
# ввода-вывода, которыми пользуется doctest, так не линкуются. Поэтому libstdc++/libgcc - статически,
# а libwinpthread-1.dll копируется к тестам из папки компилятора.
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

# Маленькая DLL, которую тест DllWatcher загружает сам, чтобы увидеть уведомление о загрузке.
add_library(dfd_watcher_test SHARED watcher_dll/watcher_dll.cpp)
dfd_dll(dfd_watcher_test)

dfd_test(patch_h_test patch_h_test.cpp)
add_dependencies(patch_h_test dfd_watcher_test)
target_compile_definitions(patch_h_test PRIVATE "WATCHER_DLL_NAME=\"$<TARGET_FILE_NAME:dfd_watcher_test>\"")

dfd_test(plugin_test plugin_test.cpp)
dfd_test(plugin_install_test plugin_install_test.cpp)
