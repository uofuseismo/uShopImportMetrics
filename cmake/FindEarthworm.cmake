#.rst:
#
# FindEarthworm
# -------------
# Finds the Earthworm libraries.
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# ``Earthworm_FOUND``
#   True indicates Earthworm was found.
# ``Earthworm::util``
#   The Earthworm utility library, if found.
# ``Earthworm::mt``
#   The Earthworm multithreaded library, if found.
#
# Already in cache, be silent
if (EARTHWORM_INCLUDE_DIR AND EARTHWORM_LIBRARY)
    set(EARTHWORM_FIND_QUIETLY TRUE)
endif()
find_path(EARTHWORM_INCLUDE_DIR
          NAMES earthworm.h
          PATHS $ENV{EW_HOME}/$ENV{EW_VERSION}/include
                $ENV{EW_INCLUDE_DIR}
                /opt/earthworm/include
                /usr/include
                /usr/local/include)
find_file(EARTHWORM_MT_LIBRARY
          NAMES libew_mt.a
          PATHS $ENV{EW_HOME}/$ENV{EW_VERSION}/lib
                $ENV{EW_LIBRARY_DIR}
                /usr/local/lib
                /usr/local/lib64
                /usr/lib/
                /usr/lib64)
find_file(EARTHWORM_UTIL_LIBRARY
          NAMES libew_util.a
          PATHS $ENV{EW_HOME}/$ENV{EW_VERSION}/lib
          $ENV{EW_LIBRARY_DIR}
          /usr/local/lib
          /usr/local/lib64
          /usr/lib/
          /usr/lib64)
set(EARTHWORM_LIBRARY ${EARTHWORM_MT_LIBRARY} ${EARTHWORM_UTIL_LIBRARY})
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Earthworm
                                  FOUND_VAR Earthworm_FOUND
                                  REQUIRED_VARS EARTHWORM_LIBRARY EARTHWORM_INCLUDE_DIR)
if (Earthworm_FOUND AND NOT TARGET Earthworm::mt)
   add_library(Earthworm::mt UNKNOWN IMPORTED)
   set_target_properties(Earthworm::mt PROPERTIES
                         IMPORTED_LOCATION "${EARTHWORM_MT_LIBRARY}"
                         INTERFACE_INCLUDE_DIRECTORIES "${EARTHWORM_INCLUDE_DIR}")
endif()
if (Earthworm_FOUND AND NOT TARGET Earthworm::util)
   add_library(Earthworm::util UNKNOWN IMPORTED)
   set_target_properties(Earthworm::util PROPERTIES
                         IMPORTED_LOCATION "${EARTHWORM_UTIL_LIBRARY}"
                         INTERFACE_INCLUDE_DIRECTORIES "${EARTHWORM_INCLUDE_DIR}")
endif()
mark_as_advanced(EARTHWORM_INCLUDE_DIR EARTHWORM_LIBRARY)
