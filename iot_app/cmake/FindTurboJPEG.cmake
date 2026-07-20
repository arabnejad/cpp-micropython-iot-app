# libjpeg-turbo does not install a pkg-config file on every supported system.
# Find its public header and library directly so the same CMake code works on
# Ubuntu, Buildroot, and Yocto.
find_path(TurboJPEG_INCLUDE_DIR NAMES turbojpeg.h)
find_library(TurboJPEG_LIBRARY NAMES turbojpeg)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  TurboJPEG
  REQUIRED_VARS TurboJPEG_INCLUDE_DIR TurboJPEG_LIBRARY
)

if(TurboJPEG_FOUND AND NOT TARGET TurboJPEG::TurboJPEG)
  add_library(TurboJPEG::TurboJPEG UNKNOWN IMPORTED)
  set_target_properties(TurboJPEG::TurboJPEG PROPERTIES
    IMPORTED_LOCATION "${TurboJPEG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TurboJPEG_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(TurboJPEG_INCLUDE_DIR TurboJPEG_LIBRARY)
