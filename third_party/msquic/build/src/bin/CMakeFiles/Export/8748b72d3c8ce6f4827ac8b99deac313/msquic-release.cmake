#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "msquic::msquic" for configuration "Release"
set_property(TARGET msquic::msquic APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(msquic::msquic PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/msquic.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/msquic.dll"
  )

list(APPEND _cmake_import_check_targets msquic::msquic )
list(APPEND _cmake_import_check_files_for_msquic::msquic "${_IMPORT_PREFIX}/lib/msquic.lib" "${_IMPORT_PREFIX}/bin/msquic.dll" )

# Import target "msquic::platform" for configuration "Release"
set_property(TARGET msquic::platform APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(msquic::platform PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/msquic_platform.lib"
  )

list(APPEND _cmake_import_check_targets msquic::platform )
list(APPEND _cmake_import_check_files_for_msquic::platform "${_IMPORT_PREFIX}/lib/msquic_platform.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
