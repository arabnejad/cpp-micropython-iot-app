if(NOT IOT_ENABLE_COVERAGE)
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "IOT_ENABLE_COVERAGE requires GCC or Clang")
endif()

foreach(coverageTarget IN ITEMS iot_platform iot_runtime iot_app iot_unit_tests iot_logger_tests)
  target_compile_options(${coverageTarget} PRIVATE -O0 -g --coverage)
  target_link_options(${coverageTarget} PRIVATE --coverage)
endforeach()

find_program(GCOVR_EXECUTABLE gcovr REQUIRED)
find_program(FIND_EXECUTABLE find REQUIRED)

add_custom_target(coverage
  # Start with empty profile data so repeated coverage runs do not merge
  # results from an older executable.
  COMMAND "${FIND_EXECUTABLE}" "${CMAKE_BINARY_DIR}" -name "*.gcda" -delete
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/coverage"
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
  # CTest runs each GoogleTest case separately so it can print every test
  # name. Run the test binary once as well to create one complete gcov profile.
  COMMAND $<TARGET_FILE:iot_unit_tests>
  COMMAND "${GCOVR_EXECUTABLE}"
    -j 1
    --root "${PROJECT_SOURCE_DIR}"
    --object-directory "${CMAKE_BINARY_DIR}"
    --filter "${PROJECT_SOURCE_DIR}/src"
    --filter "${PROJECT_SOURCE_DIR}/micropython_iot_modules"
    --exclude "${PROJECT_SOURCE_DIR}/src/runtime/main.cpp"
    --exclude "${PROJECT_SOURCE_DIR}/src/messaging/mqtt_client_api.cpp"
    --fail-under-line 90
    --print-summary
  COMMAND "${GCOVR_EXECUTABLE}"
    -j 1
    --root "${PROJECT_SOURCE_DIR}"
    --object-directory "${CMAKE_BINARY_DIR}"
    --filter "${PROJECT_SOURCE_DIR}/src"
    --filter "${PROJECT_SOURCE_DIR}/micropython_iot_modules"
    --exclude "${PROJECT_SOURCE_DIR}/src/runtime/main.cpp"
    --exclude "${PROJECT_SOURCE_DIR}/src/messaging/mqtt_client_api.cpp"
    --html-details "${CMAKE_BINARY_DIR}/coverage/index.html"
    --print-summary
  COMMAND "${GCOVR_EXECUTABLE}"
    -j 1
    --root "${PROJECT_SOURCE_DIR}"
    --object-directory "${CMAKE_BINARY_DIR}"
    --filter "${PROJECT_SOURCE_DIR}/src"
    --filter "${PROJECT_SOURCE_DIR}/micropython_iot_modules"
    --exclude "${PROJECT_SOURCE_DIR}/src/runtime/main.cpp"
    --exclude "${PROJECT_SOURCE_DIR}/src/messaging/mqtt_client_api.cpp"
    --xml-pretty --output "${CMAKE_BINARY_DIR}/coverage/coverage.xml"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  DEPENDS iot_unit_tests iot_logger_tests iot_app
  COMMENT "Running IoT App tests and creating coverage/index.html and coverage.xml"
)
