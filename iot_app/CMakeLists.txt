cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S iot_app -B build/iot_app")
endif()

project(iot_app VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(compiler_options)

include(micropython)

get_filename_component(IOT_DEFAULT_LVGL_DIR "${PROJECT_SOURCE_DIR}/../lvgl" ABSOLUTE)
set(LVGL_DIR "${IOT_DEFAULT_LVGL_DIR}" CACHE PATH "Path to the pinned LVGL source tree")
if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "LVGL was not found at ${LVGL_DIR}. Run: git submodule update --init --recursive"
  )
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(LIBMOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/config/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

option(IOT_BUILD_TESTS "Build the IoT App unit and MicroPython tests" OFF)
option(IOT_ENABLE_COVERAGE "Instrument IoT App tests for gcovr coverage reports" OFF)

# LVGL is used only inside IoT App. Build it as a static library even when a
# parent build, such as Buildroot, enables shared libraries globally. This
# keeps the executable from depending on a project-owned liblvgl.so file.
set(IOT_PARENT_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libraries as shared objects" FORCE)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS "${IOT_PARENT_BUILD_SHARED_LIBS}"
    CACHE BOOL "Build libraries as shared objects" FORCE)

add_library(iot_platform STATIC
  src/input/adafruit_mini_i2c_gamepad.cpp
  src/input/game_controller.cpp
  src/logging/logger.cpp
  src/platform/linux/display_manager.cpp
  src/platform/linux/edid_parser.cpp
  src/platform/linux/i2c_device.cpp
  src/platform/linux/system_information.cpp
  src/ui/lvgl_framebuffer_render_backend.cpp
  src/ui/screen_manager.cpp
)
target_include_directories(iot_platform PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(iot_platform
  PUBLIC Threads::Threads
  PRIVATE lvgl::lvgl PkgConfig::LIBDRM
)
iot_apply_compiler_warnings(iot_platform)

add_library(iot_runtime STATIC
  ${IOT_MICROPYTHON_GENERATED_SOURCE_FILES}
  src/messaging/application_deployment_controller.cpp
  src/messaging/application_deployment_message.cpp
  src/messaging/application_message_queue.cpp
  src/messaging/mqtt_client_api.cpp
  src/messaging/mqtt_application_receiver.cpp
  micropython_iot_modules/device/mod_iot.c
  micropython_iot_modules/common/micropython_traceback.c
  micropython_iot_modules/display/display_cpp_bridge.cpp
  micropython_iot_modules/display/mod_iot_display.c
  micropython_iot_modules/input/input_cpp_bridge.cpp
  micropython_iot_modules/input/mod_iot_input.c
  micropython_iot_modules/scheduler/mod_iot_scheduler.c
  micropython_iot_modules/system/system_cpp_bridge.cpp
  micropython_iot_modules/system/mod_iot_system.c
  src/python/micropython_execution.c
  src/python/application_metadata.cpp
  src/python/path_validation.cpp
  src/python/python_application_loader.cpp
  src/python/micropython_runtime.cpp
  src/python/micropython_application_context.cpp
  src/python/python_application_manager.cpp
  src/python/temporary_python_application_installer.cpp
)
add_dependencies(iot_runtime generate_micropython_embed)
target_include_directories(iot_runtime
  PUBLIC "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/micropython_config"
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/common"
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/display"
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/input"
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/scheduler"
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/system"
    "${PROJECT_SOURCE_DIR}/src/python"
    "${IOT_MICROPYTHON_GENERATED_DIR}"
    "${IOT_MICROPYTHON_GENERATED_DIR}/port"
)
target_compile_options(iot_runtime PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-fno-common>
)
target_compile_definitions(iot_runtime PRIVATE IOT_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(iot_runtime
  PUBLIC iot_platform
  PRIVATE
    lvgl::lvgl
    PkgConfig::LIBMOSQUITTO
    PkgConfig::CJSON
    OpenSSL::Crypto
)

add_executable(iot_app
  src/runtime/main.cpp
  src/runtime/runtime_config.cpp
)
# main.cpp creates the internal libmosquitto adapter, but that header is not
# part of the public include directory.
target_include_directories(iot_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(iot_app PRIVATE iot_platform iot_runtime)
target_compile_definitions(iot_app PRIVATE
  IOT_INSTALL_DATA_DIRECTORY="${CMAKE_INSTALL_DATADIR}"
  IOT_INSTALLED_DEFAULT_APPLICATION_DIRECTORY="${CMAKE_INSTALL_FULL_DATADIR}/iot-app/default_python_application"
)
iot_apply_compiler_warnings(iot_app)

set(IOT_DEFAULT_APPLICATION_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}/default_python_application")
set(IOT_DEFAULT_APPLICATION_BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/default_python_application")
file(MAKE_DIRECTORY "${IOT_DEFAULT_APPLICATION_BUILD_DIRECTORY}")
configure_file(
  "${IOT_DEFAULT_APPLICATION_SOURCE_DIRECTORY}/app.json"
  "${IOT_DEFAULT_APPLICATION_BUILD_DIRECTORY}/app.json"
  COPYONLY
)
configure_file(
  "${IOT_DEFAULT_APPLICATION_SOURCE_DIRECTORY}/main.py"
  "${IOT_DEFAULT_APPLICATION_BUILD_DIRECTORY}/main.py"
  COPYONLY
)

install(TARGETS iot_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  DIRECTORY "${IOT_DEFAULT_APPLICATION_SOURCE_DIRECTORY}/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/iot-app/default_python_application"
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)

if(IOT_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

include(clang_format)

message(STATUS "IoT App configured with LVGL framebuffer output")
