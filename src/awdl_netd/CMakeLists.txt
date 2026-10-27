cmake_minimum_required(VERSION 3.20)
project(awdl_netd C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/deps/lwip")
if(NOT EXISTS "${LWIP_DIR}/src/core/init.c")
    message(FATAL_ERROR "lwIP not found at ${LWIP_DIR}; clone lwip-tcpip/lwip there")
endif()

file(GLOB LWIP_CORE
    "${LWIP_DIR}/src/core/*.c"
    "${LWIP_DIR}/src/core/ipv4/*.c"
    "${LWIP_DIR}/src/core/ipv6/*.c"
    "${LWIP_DIR}/src/apps/mdns/*.c"
)

add_library(lwip STATIC ${LWIP_CORE})
target_include_directories(lwip PUBLIC
    "${LWIP_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(MSVC)
    target_compile_options(lwip PRIVATE /std:c11 /W0)
else()
    target_compile_options(lwip PRIVATE -std=c11 -w)
endif()

add_executable(awdl_netd main.cpp)
target_include_directories(awdl_netd PRIVATE
    "${LWIP_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(awdl_netd PRIVATE lwip ws2_32)
if(MSVC)
    target_compile_options(awdl_netd PRIVATE /W4)
endif()
