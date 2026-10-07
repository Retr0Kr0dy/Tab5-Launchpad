cmake_minimum_required(VERSION 3.16)
project(konsole VERSION 0.1.0 LANGUAGES C)

option(KONSOLE_BUILD_EXAMPLES "Build examples" OFF)
option(KONSOLE_ENABLE_ANSI "Enable ANSI features by default" ON)

file(GLOB KONSOLE_SRC CONFIGURE_DEPENDS "src/*.c")

add_library(konsole ${KONSOLE_SRC})
add_library(konsole::konsole ALIAS konsole)

target_include_directories(konsole PUBLIC
  $<BUILD_INTERFACE:{CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_compile_features(konsole PUBLIC c_std_11)
target_compile_definitions(konsole PUBLIC
  $<$<BOOL:${KONSOLE_ENABLE_ANSI}>:KONSOLE_ENABLE_ANSI=1>)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS konsole EXPORT konsoleTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
  cmake/konsoleConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/konsoleConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/konsole)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/konsoleConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/konsoleConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/konsoleConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/konsole)

install(EXPORT konsoleTargets
  NAMESPACE konsole::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/konsole)
