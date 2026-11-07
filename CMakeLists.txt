cmake_minimum_required(VERSION 3.16)

# Version and widget id come from the widget metadata, so there is one place to bump.
file(READ plasmoid/metadata.json _meta)
string(JSON PROJECT_VERSION_FROM_META GET "${_meta}" KPlugin Version)
string(JSON PLASMOID_ID GET "${_meta}" KPlugin Id)

project(glass-color-picker VERSION ${PROJECT_VERSION_FROM_META} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(LayerShellQt REQUIRED)
find_package(KF6WindowSystem REQUIRED)

add_executable(glass-color-picker src/main.cpp src/PickerOverlay.cpp src/PickerOverlay.h)
target_link_libraries(glass-color-picker PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus LayerShellQt::Interface KF6::WindowSystem)

# ---- install: `cmake --install build` (or `--prefix ~/.local` for a user install) ----
install(TARGETS glass-color-picker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY plasmoid/ DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/${PLASMOID_ID})

# ---- release artifacts, all in build/ ----
#   cmake --build build --target plasmoid       -> glass-color-picker-<ver>.plasmoid (store.kde.org)
#   cmake --build build --target package_source -> glass-color-picker-<ver>-src.tar.gz (AUR)
#   cmake --build build --target release        -> both, plus the stripped binary and checksums
set(_name glass-color-picker-${PROJECT_VERSION})

file(GLOB_RECURSE _plasmoid_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/plasmoid/*)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/${_name}.plasmoid
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/${_name}.plasmoid
    # A .plasmoid is a zip with metadata.json at its root.
    COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/${_name}.plasmoid --format=zip -- metadata.json contents
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/plasmoid
    DEPENDS ${_plasmoid_files}
    COMMENT "Packaging ${_name}.plasmoid")
add_custom_target(plasmoid DEPENDS ${CMAKE_BINARY_DIR}/${_name}.plasmoid)

set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME ${_name}-src)
set(CPACK_SOURCE_IGNORE_FILES "/build/" "/dist/" "/\\\\.git/" "~$" "\\\\.swp$")
include(CPack)

add_custom_target(release
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package_source
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:glass-color-picker> ${CMAKE_BINARY_DIR}/glass-color-picker-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}
    COMMAND strip ${CMAKE_BINARY_DIR}/glass-color-picker-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}
    COMMAND sha256sum ${_name}.plasmoid ${_name}-src.tar.gz glass-color-picker-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR} > ${_name}.sha256
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS plasmoid glass-color-picker
    COMMENT "Release ${_name} in ${CMAKE_BINARY_DIR}")
