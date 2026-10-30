# vikey.so: the fcitx5 addon (a module fcitx5 loads), plus its addon and input method descriptions.
include(GNUInstallDirs)

add_library(vikey-fcitx5 MODULE Engine.cpp)
# fcitx5 5.1 headers use C++20 (std::span, string_view::starts_with); the rest of Vikey stays C++17
set_target_properties(vikey-fcitx5 PROPERTIES OUTPUT_NAME vikey PREFIX "" CXX_VISIBILITY_PRESET hidden CXX_STANDARD 20)
target_link_libraries(vikey-fcitx5 PRIVATE vikey_linux_core Fcitx5::Core Fcitx5::Utils)
install(TARGETS vikey-fcitx5 DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES vikey-addon.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon RENAME vikey.conf)
install(FILES vikey-im.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod RENAME vikey.conf)
