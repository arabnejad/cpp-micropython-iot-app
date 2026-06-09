find_program(CLANG_FORMAT_EXE NAMES clang-format)

file(GLOB_RECURSE IOT_FORMAT_SOURCES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/include/*.[ch]"
  "${PROJECT_SOURCE_DIR}/include/*.[ch]pp"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.[ch]"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.[ch]pp"
  "${PROJECT_SOURCE_DIR}/src/*.[ch]"
  "${PROJECT_SOURCE_DIR}/src/*.[ch]pp"
  "${PROJECT_SOURCE_DIR}/tests/*.[ch]"
  "${PROJECT_SOURCE_DIR}/tests/*.[ch]pp"
)

if(CLANG_FORMAT_EXE AND IOT_FORMAT_SOURCES)
  add_custom_target(clang_format
    COMMAND ${CLANG_FORMAT_EXE} -i ${IOT_FORMAT_SOURCES}
    COMMENT "Formatting IoT application sources"
    VERBATIM
  )
  add_custom_target(clang_format_check
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${IOT_FORMAT_SOURCES}
    COMMENT "Checking IoT application source formatting"
    VERBATIM
  )
else()
  add_custom_target(clang_format
    COMMAND ${CMAKE_COMMAND} -E echo "clang-format unavailable or no source files exist yet"
    COMMAND ${CMAKE_COMMAND} -E false
  )
  add_custom_target(clang_format_check
    COMMAND ${CMAKE_COMMAND} -E echo "clang-format unavailable or no source files exist yet"
    COMMAND ${CMAKE_COMMAND} -E false
  )
endif()
