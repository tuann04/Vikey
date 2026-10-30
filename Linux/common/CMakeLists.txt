# What the IBus engine and the fcitx5 addon share: the core (Core.cpp, a port of the Mac/Windows state machine),
# the settings file (Prefs.cpp), the host helpers (Host.cpp), the data files and the settings app.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HOSTLIBS REQUIRED IMPORTED_TARGET GLOBAL glib-2.0 gio-2.0 gstreamer-1.0)
pkg_check_modules(X11 IMPORTED_TARGET GLOBAL x11)
include(GNUInstallDirs)

add_library(vikey_linux_core STATIC Core.cpp Prefs.cpp Host.cpp)
set_target_properties(vikey_linux_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vikey_linux_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey_linux_core PUBLIC VIKEY_VERSION="${VIKEY_VERSION}" VIKEY_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/vikey")
target_link_libraries(vikey_linux_core PUBLIC vikey_engine PkgConfig::HOSTLIBS)
if(X11_FOUND)
  target_compile_definitions(vikey_linux_core PUBLIC VIKEY_HAVE_X11)
  target_link_libraries(vikey_linux_core PUBLIC PkgConfig::X11)
endif()

# shared data: the switch sounds (the Mac's .caf files as .wav) and the emoji list of the picker
install(FILES ../sounds/SwitchToVietnamese.wav ../sounds/SwitchToEnglish.wav DESTINATION ${CMAKE_INSTALL_DATADIR}/vikey/sounds)
install(FILES ${ROOT}/App/Resources/Emoji.json DESTINATION ${CMAKE_INSTALL_DATADIR}/vikey)

add_subdirectory(../settings settings)
