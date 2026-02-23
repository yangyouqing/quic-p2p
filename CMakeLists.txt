cmake_minimum_required(VERSION 3.10)
project(quic-p2p C)

set(CMAKE_C_STANDARD 11)
find_package(Threads REQUIRED)

# xquic with MOQ and babassl (optional if SSL not found)
set(SSL_TYPE "babassl" CACHE STRING "SSL library: babassl only")
if(NOT SSL_PATH)
  set(SSL_PATH "/usr/local/babassl" CACHE PATH "babassl install path")
endif()
set(XQC_ENABLE_MOQ ON CACHE BOOL "Enable MOQ")
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/xquic/cmake;${CMAKE_MODULE_PATH}")
set(SSL_DIR "${SSL_PATH}")
find_package(SSL QUIET)
if(SSL_FOUND)
  add_subdirectory(xquic ${CMAKE_BINARY_DIR}/xquic)
  set(P2P_HAVE_XQUIC 1)
else()
  message(WARNING "SSL (babassl) not found at SSL_PATH=${SSL_PATH}. Build without xquic/MOQ. Install babassl and set -DSSL_PATH=... to enable.")
  set(P2P_HAVE_XQUIC 0)
endif()

# P2P signaling client/peer: depend on libjuice
add_subdirectory(libjuice ${CMAKE_BINARY_DIR}/libjuice)

set(P2P_COMMON_SOURCES
  ${CMAKE_SOURCE_DIR}/src/common/signal.c
  ${CMAKE_SOURCE_DIR}/src/common/run.c
)
if(P2P_HAVE_XQUIC)
  list(APPEND P2P_COMMON_SOURCES ${CMAKE_SOURCE_DIR}/src/common/xquic_ice.c)
  add_compile_definitions(P2P_HAVE_XQUIC=1)
endif()

add_executable(quic-p2p-client ${CMAKE_SOURCE_DIR}/src/client/main.c ${P2P_COMMON_SOURCES})
target_include_directories(quic-p2p-client PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/libjuice/include
)
if(P2P_HAVE_XQUIC)
  target_include_directories(quic-p2p-client PRIVATE ${CMAKE_SOURCE_DIR}/xquic/include)
  target_link_libraries(quic-p2p-client PRIVATE juice xquic-static Threads::Threads m)
else()
  target_link_libraries(quic-p2p-client PRIVATE juice Threads::Threads)
endif()
if(WIN32)
  target_link_libraries(quic-p2p-client PRIVATE ws2_32)
endif()
set_target_properties(quic-p2p-client PROPERTIES OUTPUT_NAME client RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(quic-p2p-peer ${CMAKE_SOURCE_DIR}/src/peer/main.c ${P2P_COMMON_SOURCES})
target_include_directories(quic-p2p-peer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/libjuice/include
)
if(P2P_HAVE_XQUIC)
  target_include_directories(quic-p2p-peer PRIVATE ${CMAKE_SOURCE_DIR}/xquic/include)
  target_link_libraries(quic-p2p-peer PRIVATE juice xquic-static Threads::Threads m)
else()
  target_link_libraries(quic-p2p-peer PRIVATE juice Threads::Threads)
endif()
if(WIN32)
  target_link_libraries(quic-p2p-peer PRIVATE ws2_32)
endif()
set_target_properties(quic-p2p-peer PROPERTIES OUTPUT_NAME peer RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
