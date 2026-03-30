cmake_minimum_required(VERSION 3.28)
project(uShopImportMetrics VERSION 0.0.1 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(BUILD_EW "Build Earthworm" OFF)
option(BUILD_TESTS "Compile unit tests" ON) 
include(GenerateExportHeader)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if (NOT ${BUILD_SHARED_LIBS})
   set(Boost_USE_STATIC_LIBS ON) 
endif()

# Versioning information
configure_file(${CMAKE_SOURCE_DIR}/src/version.hpp.in
               ${CMAKE_SOURCE_DIR}/include/uShopImportMetrics/version.hpp)
find_package(spdlog REQUIRED)
find_package(libmseed REQUIRED)
find_package(Boost COMPONENTS program_options REQUIRED)
find_package(Threads REQUIRED)
find_package(SEEDLink REQUIRED)
find_package(opentelemetry-cpp CONFIG REQUIRED COMPONENTS exporters_otlp_http)
if (${BUILD_EW})
   find_package(Earthworm REQUIRED)
else()
   find_package(Earthworm)
endif()
if (${BUILD_TESTS})
   find_package(Catch2 REQUIRED)
else()
   find_package(Catch2)
endif()

set(LIBRARY_SRC
    src/seedLinkClient.cpp
    src/seedLinkClientOptions.cpp
    src/streamIdentifier.cpp
    src/streamSelector.cpp
    src/packet.cpp
    src/version.cpp)
set(LIBRARY_MODULE_FILES
    src/modules/metrics.cppm)
set(HEADER_FILES
    include/uShopImportMetrics/packet.hpp
    include/uShopImportMetrics/streamIdentifier.hpp
    include/uShopImportMetrics/streamSelector.hpp
    include/uShopImportMetrics/seedLinkClientOptions.hpp
    include/uShopImportMetrics/version.hpp)
if (${Earthworm_FOUND})
   list(APPEND LIBRARY_SRC src/traceBuf2.cpp src/waveRing.cpp)
   list(APPEND HEADER_FILES include/uShopImportMetrics/traceBuf2.hpp
                            include/uShopImportMetrics/waveRing.hpp)
endif()

add_library(libuShopImportMetrics STATIC ${LIBRARY_SRC})
add_library(uShopImportMetrics::libuShopImportMetrics ALIAS libuShopImportMetrics)
set_target_properties(libuShopImportMetrics PROPERTIES
                      OUTPUT_NAME uShopImportMetrics
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_sources(libuShopImportMetrics
               PUBLIC FILE_SET HEADERS
                      BASE_DIRS
                          ${CMAKE_CURRENT_SOURCE_DIR}
                      FILES
                          ${HEADER_FILES}
               PUBLIC FILE_SET
                      CXX_MODULES
                      BASE_DIRS
                         src/modules
                      FILES
                         ${LIBRARY_MODULE_FILES}
               )
target_include_directories(libuShopImportMetrics
                           PUBLIC $<INSTALL_INTERFACE:include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/modules>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                          )
target_link_libraries(libuShopImportMetrics
                      PRIVATE opentelemetry-cpp::otlp_http_metric_exporter
                              spdlog::spdlog_header_only
                              Boost::headers
                              SEEDLink::SEEDLink
                              mseed::mseed
                              Threads::Threads)
set(BINARIES "")
if (${Earthworm_FOUND})
   message("Building uEarthwormPacketMetrics")
   target_link_libraries(libuShopImportMetrics PRIVATE Earthworm::util Earthworm::mt)
   target_compile_definitions(libuShopImportMetrics PRIVATE WITH_EARTHWORM)

   add_executable(uEarthwormPacketMetrics src/earthwormPacketMetrics.cpp)
   set_target_properties(uEarthwormPacketMetrics PROPERTIES
                         CXX_STANDARD 20
                         CXX_STANDARD_REQUIRED YES 
                         CXX_EXTENSIONS NO) 
   target_link_libraries(uEarthwormPacketMetrics
                         PRIVATE uShopImportMetrics::libuShopImportMetrics
                                 opentelemetry-cpp::otlp_http_metric_exporter
                                 Boost::headers Boost::program_options)
   target_include_directories(uEarthwormPacketMetrics
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
   set(BINARIES ${BINARIES} uEarthwormPacketMetrics)
endif()

##########################################################################################
#                                         Tests                                          #
##########################################################################################

if (PROJECT_IS_TOP_LEVEL AND ${BUILD_TESTS})
   add_executable(unitTests
                  testing/packet.cpp
                  testing/seedLink.cpp
                  )
   set_target_properties(unitTests PROPERTIES
                         CXX_STANDARD 20
                         CXX_STANDARD_REQUIRED YES
                         CXX_EXTENSIONS NO)
   target_link_libraries(unitTests 
                         PRIVATE
                            uShopImportMetrics::libuShopImportMetrics
                            Threads::Threads
                            Catch2::Catch2 Catch2::Catch2WithMain)
   target_include_directories(unitTests
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
   add_test(NAME unitTests
            COMMAND unitTests)
endif()

##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(CMakePackageConfigHelpers)
configure_file(${PROJECT_SOURCE_DIR}/cmake/uShopImportMetrics.cmake.in
               uShopImportMetricsConfig.cmake @ONLY)
write_basic_package_version_file(
   uShopImportMetricsConfigVersion.cmake
   VERSION ${PACKAGE_VERSION}
   COMPATIBILITY AnyNewerVersion)

include(GNUInstallDirs)
install(TARGETS libuShopImportMetrics
        EXPORT ${PROJECT_NAME}Targets
        FILE_SET HEADERS
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILE_SET CXX_MODULES
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries)
install(TARGETS ${BINARIES}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)
export(EXPORT ${PROJECT_NAME}Targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/uShopImportMetricsConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/uShopImportMetricsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uShopImportMetrics)

##########################################################################################
#                                       Packaging                                        #
##########################################################################################

include(CPackComponent)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "UUSS")
set(CPACK_PACKAGE_CONTACT "ben.baker@utah.edu")
set(CPACK_PACKAGE_LICENSE "MIT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tabulates metrics on data packets imported into Earthworm or via SEEDLink.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
cpack_add_component(libraries
                    DISPLAY_NAME "uShopImportMetrics common libraries")
cpack_add_component(headers
                    DISPLAY_NAME "uShopImportMetrics library header files"
                    DEPENDS libraries)
cpack_add_component(applications
                    DISPLAY_NAME "uShopImportMetrics applications"
                    DEPENDS libraries)
set(CPACK_GENERATOR TGZ)
if (DPKG_PROGRAM)
   list(APPEND CPACK_GENERATOR DEB)
   set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
   set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) 
endif()
set(CPACK_SOURCE_IGNORE_FILES
  /\\.git/
  \\.swp
  \\.orig
  /CMakeLists\\.txt\\.user
  /private/
)
include(CPack) # Put this last!

