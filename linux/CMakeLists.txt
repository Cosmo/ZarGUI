# GTK 4 + libadwaita app. Enabled with -DZARGUI_LINUX_APP=ON (see the Flatpak manifest).
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZARGUI_GUI REQUIRED IMPORTED_TARGET gtk4>=4.12 libadwaita-1>=1.5)

set(ZARGUI_APP_ID io.github.Cosmo.ZarGUI)
set(ZARGUI_VERSION 0.3.0)

add_executable(zargui
  src/main.c
  src/zar-archive.c
  src/zar-archive-item.c
  src/zar-archive-window.c
  src/zar-drag-content.c
  src/zar-job.c
  src/zar-preferences.c
  src/zar-util.c
  src/zar-window.c)
target_compile_features(zargui PRIVATE c_std_11)
target_compile_definitions(zargui PRIVATE ZAR_VERSION="${ZARGUI_VERSION}"
  GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76)
target_compile_options(zargui PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(zargui PRIVATE PkgConfig::ZARGUI_GUI zarpack libzstd_static)
# The core is C++; link with the C++ driver so its runtime comes along.
set_target_properties(zargui PROPERTIES LINKER_LANGUAGE CXX)

include(GNUInstallDirs)
install(TARGETS zargui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/${ZARGUI_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${ZARGUI_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/${ZARGUI_APP_ID}.gschema.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)
install(FILES data/${ZARGUI_APP_ID}.mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME ${ZARGUI_APP_ID}.xml)
install(FILES data/icons/${ZARGUI_APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(CODE "execute_process(COMMAND glib-compile-schemas \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/glib-2.0/schemas\")")
