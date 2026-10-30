# ibus-engine-vikey. Version = project.yml MARKETING_VERSION, like the other platforms.
find_package(PkgConfig REQUIRED)
pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0)
include(GNUInstallDirs)

add_executable(ibus-engine-vikey Engine.cpp)
target_link_libraries(ibus-engine-vikey PRIVATE vikey_linux_core PkgConfig::IBUS)
configure_file(vikey.xml.in ${CMAKE_CURRENT_BINARY_DIR}/vikey.xml @ONLY)
install(TARGETS ibus-engine-vikey DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vikey.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/ibus/component)
