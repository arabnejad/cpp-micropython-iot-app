# Compile project-owned C++ code as strict ISO C++17. Prefer C++14-compatible
# constructs unless a C++17 feature makes the implementation clearer or
# provides functionality unavailable in C++14.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

function(iot_apply_compiler_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /permissive- /W4 /w44265 /w44062)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  endif()
endfunction()
