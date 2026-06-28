include(FetchContent)
include(GoogleTest)

# Use a system GoogleTest package when one is installed. Otherwise download a
# pinned copy only for the opt-in host test build. It is never used by the
# normal application or Buildroot package build.
find_package(GTest 1.14 QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(iot_unit_tests
  main.cpp
  test_application_deployment_message.cpp
  test_application_deployment_controller.cpp
  test_application_message_queue.cpp
  test_application_metadata.cpp
  test_edid_parser.cpp
  test_display_manager.cpp
  scoped_linux_i2c_system_calls.cpp
  fake_mosquitto_library.cpp
  test_game_controller.cpp
  test_i2c_device.cpp
  test_adafruit_mini_i2c_gamepad.cpp
  test_hardware_construction.cpp
  test_input_cpp_bridge.cpp
  test_lvgl_framebuffer_render_backend.cpp
  test_mqtt_application_receiver.cpp
  test_native_cpp_bridges.cpp
  test_micropython_runtime.cpp
  test_micropython_device_module.cpp
  test_micropython_input_module.cpp
  test_python_application_loader.cpp
  test_python_application_manager.cpp
  test_runtime_config.cpp
  test_screen_manager.cpp
  test_temporary_python_application_installer.cpp
  test_system_information.cpp
)

# Tests can replace the internal Linux, LVGL, and libmosquitto adapters without
# exposing those headers as application interfaces.
target_include_directories(iot_unit_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/platform/linux"
  "${PROJECT_SOURCE_DIR}/src/runtime"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/common"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/display"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/input"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/system"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(iot_unit_tests PRIVATE
  iot_platform
  iot_runtime
  PkgConfig::LIBDRM
  PkgConfig::LIBMOSQUITTO
  lvgl::lvgl
  GTest::gtest
  GTest::gmock
)
target_link_options(iot_unit_tests PRIVATE
  "-Wl,--wrap=open"
  "-Wl,--wrap=close"
  "-Wl,--wrap=ioctl"
  "-Wl,--wrap=read"
  "-Wl,--wrap=write"
)
target_sources(iot_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/runtime/runtime_config.cpp")
target_compile_definitions(iot_unit_tests PRIVATE
  IOT_INSTALL_DATA_DIRECTORY="share"
  IOT_INSTALLED_DEFAULT_APPLICATION_DIRECTORY="/usr/share/iot-app/default_python_application"
)
iot_apply_compiler_warnings(iot_unit_tests)

# Logger tests use their own process so they can enable every log level without
# turning expected error-path messages back on for the main unit-test suite.
add_executable(iot_logger_tests
  logger_test_main.cpp
  test_logger.cpp
)
target_link_libraries(iot_logger_tests PRIVATE
  iot_platform
  GTest::gtest
)
iot_apply_compiler_warnings(iot_logger_tests)

# Register every GoogleTest case as its own CTest test. `ctest` can then show
# the real behavior name instead of one broad `iot_unit_tests` entry.
gtest_discover_tests(iot_unit_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "unit"
)
gtest_discover_tests(iot_logger_tests
  TEST_PREFIX "LoggerOutput."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "unit"
)

# `--help` returns before display discovery, LVGL, I2C, or MQTT start. It is a
# safe end-to-end check of the executable entry point and command-line path.
add_test(NAME iot_app_command_line_help COMMAND $<TARGET_FILE:iot_app> --help)
set_tests_properties(iot_app_command_line_help PROPERTIES
  LABELS "unit"
  PASS_REGULAR_EXPRESSION "Usage: iot_app"
)

include(coverage)
