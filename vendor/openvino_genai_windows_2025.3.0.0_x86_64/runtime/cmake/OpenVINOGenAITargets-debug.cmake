#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "openvino::genai" for configuration "Debug"
set_property(TARGET openvino::genai APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(openvino::genai PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/runtime/lib/intel64/Debug/openvino_genaid.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/runtime/bin/intel64/Debug/openvino_genaid.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS openvino::genai )
list(APPEND _IMPORT_CHECK_FILES_FOR_openvino::genai "${_IMPORT_PREFIX}/runtime/lib/intel64/Debug/openvino_genaid.lib" "${_IMPORT_PREFIX}/runtime/bin/intel64/Debug/openvino_genaid.dll" )

# Import target "openvino::genai::c" for configuration "Debug"
set_property(TARGET openvino::genai::c APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(openvino::genai::c PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/runtime/lib/intel64/Debug/openvino_genai_cd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "openvino::genai"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/runtime/bin/intel64/Debug/openvino_genai_cd.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS openvino::genai::c )
list(APPEND _IMPORT_CHECK_FILES_FOR_openvino::genai::c "${_IMPORT_PREFIX}/runtime/lib/intel64/Debug/openvino_genai_cd.lib" "${_IMPORT_PREFIX}/runtime/bin/intel64/Debug/openvino_genai_cd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
