get_filename_component(
  IOT_DEFAULT_MICROPYTHON_DIR
  "${PROJECT_SOURCE_DIR}/../micropython"
  ABSOLUTE
)

set(
  MICROPYTHON_DIR
  "${IOT_DEFAULT_MICROPYTHON_DIR}"
  CACHE PATH
  "Path to the pinned MicroPython source tree"
)

if(NOT EXISTS "${MICROPYTHON_DIR}/ports/embed/embed.mk")
  message(FATAL_ERROR
    "MicroPython embed port not found at ${MICROPYTHON_DIR}. "
    "Initialize the root submodule before configuring iot_app."
  )
endif()

set(IOT_MICROPYTHON_CONFIG_DIR "${PROJECT_SOURCE_DIR}/micropython_config")
message(STATUS "MicroPython source: ${MICROPYTHON_DIR}")

find_program(IOT_MAKE_EXECUTABLE NAMES gmake make)
if(NOT IOT_MAKE_EXECUTABLE)
  message(FATAL_ERROR "A Make executable is required to generate the MicroPython embed sources")
endif()

set(IOT_MICROPYTHON_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/micropython_embed")
set(IOT_MICROPYTHON_GENERATOR_BUILD_DIR "${CMAKE_BINARY_DIR}/generated/micropython_generator")
set(IOT_MICROPYTHON_GENERATION_STAMP "${IOT_MICROPYTHON_GENERATED_DIR}/generation-complete.stamp")

file(GLOB IOT_MICROPYTHON_CORE_SOURCE_FILES CONFIGURE_DEPENDS "${MICROPYTHON_DIR}/py/*.c")
set(IOT_MICROPYTHON_GENERATED_SOURCE_FILES)
foreach(source_file IN LISTS IOT_MICROPYTHON_CORE_SOURCE_FILES)
  get_filename_component(source_name "${source_file}" NAME)
  list(APPEND IOT_MICROPYTHON_GENERATED_SOURCE_FILES "${IOT_MICROPYTHON_GENERATED_DIR}/py/${source_name}")
endforeach()
list(APPEND IOT_MICROPYTHON_GENERATED_SOURCE_FILES
  "${IOT_MICROPYTHON_GENERATED_DIR}/port/embed_util.c"
  "${IOT_MICROPYTHON_GENERATED_DIR}/port/mphalport.c"
  "${IOT_MICROPYTHON_GENERATED_DIR}/shared/runtime/gchelper_generic.c"
)

file(GLOB_RECURSE IOT_MICROPYTHON_MODULE_DEFINITION_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.c"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*.h"
  "${PROJECT_SOURCE_DIR}/micropython_iot_modules/*/micropython.mk"
)

add_custom_command(
  OUTPUT "${IOT_MICROPYTHON_GENERATION_STAMP}"
  BYPRODUCTS ${IOT_MICROPYTHON_GENERATED_SOURCE_FILES}
  COMMAND "${CMAKE_COMMAND}" -E remove_directory "${IOT_MICROPYTHON_GENERATOR_BUILD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${IOT_MICROPYTHON_GENERATED_DIR}"
  COMMAND "${IOT_MAKE_EXECUTABLE}"
          -f "${IOT_MICROPYTHON_CONFIG_DIR}/micropython_embed.mk"
          "MICROPYTHON_TOP=${MICROPYTHON_DIR}"
          "BUILD=${IOT_MICROPYTHON_GENERATOR_BUILD_DIR}"
          "PACKAGE_DIR=${IOT_MICROPYTHON_GENERATED_DIR}"
          "USER_C_MODULES=${PROJECT_SOURCE_DIR}/micropython_iot_modules"
  COMMAND "${CMAKE_COMMAND}" -E touch "${IOT_MICROPYTHON_GENERATION_STAMP}"
  DEPENDS
    "${IOT_MICROPYTHON_CONFIG_DIR}/micropython_embed.mk"
    "${IOT_MICROPYTHON_CONFIG_DIR}/mpconfigport.h"
    ${IOT_MICROPYTHON_MODULE_DEFINITION_FILES}
  WORKING_DIRECTORY "${IOT_MICROPYTHON_CONFIG_DIR}"
  COMMENT "Generating self-contained MicroPython embed sources"
  VERBATIM
)

add_custom_target(generate_micropython_embed DEPENDS "${IOT_MICROPYTHON_GENERATION_STAMP}")
set_source_files_properties(${IOT_MICROPYTHON_GENERATED_SOURCE_FILES} PROPERTIES GENERATED TRUE)
