# Install script for directory: C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/msquic")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/obj/Release/msquic.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/bin/Release/msquic.dll")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/obj/Release/msquic_platform.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic.hpp"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic_fuzz.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic_posix.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic_winkernel.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquic_winuser.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquichelper.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/msquicp.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_cert.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_crypt.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_datapath.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_driver_helpers.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_hashtable.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_pcp.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_platform.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_platform_posix.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_platform_winkernel.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_platform_winuser.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_sal_stub.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_storage.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_tls.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_toeplitz.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_trace.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_trace_manifested_etw.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_var_int.h"
    "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/src/src/bin/../inc/quic_versions.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/msquic" TYPE FILE FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/msquic-config.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/msquic/msquic.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/msquic/msquic.cmake"
         "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/src/bin/CMakeFiles/Export/8748b72d3c8ce6f4827ac8b99deac313/msquic.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/msquic/msquic-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/msquic/msquic.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/msquic" TYPE FILE FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/src/bin/CMakeFiles/Export/8748b72d3c8ce6f4827ac8b99deac313/msquic.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/msquic" TYPE FILE FILES "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/src/bin/CMakeFiles/Export/8748b72d3c8ce6f4827ac8b99deac313/msquic-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Admin/Desktop/SecureTunnelCpp/third_party/msquic/build/src/bin/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
