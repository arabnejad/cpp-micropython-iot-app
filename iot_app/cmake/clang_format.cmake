find_program(CLANG_FORMAT_EXE NAMES clang-format)

file(GLOB_RECURSE IOT_FORMAT_SOURCES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/include/*.[ch]"
  "${PROJECT_SOURCE_DIR}/include/*.[ch]pp"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.[ch]"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.[ch]pp"
  "${PROJECT_SOURCE_DIR}/src/*.[ch]"
  "${PROJECT_SOURCE_DIR}/src/*.[ch]pp"
)

if(CLANG_FORMAT_EXE AND IOT_FORMAT_SOURCES)
  add_custom_target(clang_format
    COMMAND ${CLANG_FORMAT_EXE} -i ${IOT_FORMAT_SOURCES}
    COMMENT "Formatting IoT application sources"
    VERBATIM
  )
else()
  add_custom_target(clang_format
    COMMAND ${CMAKE_COMMAND} -E echo "clang-format unavailable or no source files exist yet"
  )
endif()
