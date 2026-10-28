cmake_minimum_required(VERSION 3.20)
project(awdl_netd C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/deps/lwip")
set(LWIP_CONTRIB "${CMAKE_CURRENT_SOURCE_DIR}/../../build/deps/lwip-contrib")
if(NOT EXISTS "${LWIP_DIR}/src/core/init.c")
    message(FATAL_ERROR "lwIP not found at ${LWIP_DIR}")
endif()
if(NOT EXISTS "${LWIP_CONTRIB}/ports/win32/sys_arch.c")
    message(FATAL_ERROR "lwip-contrib win32 port not found at ${LWIP_CONTRIB}")
endif()

file(GLOB LWIP_CORE
    "${LWIP_DIR}/src/core/*.c"
    "${LWIP_DIR}/src/core/ipv4/*.c"
    "${LWIP_DIR}/src/core/ipv6/*.c"
    "${LWIP_DIR}/src/api/*.c"
)

add_library(lwip STATIC
    ${LWIP_CORE}
    "${LWIP_CONTRIB}/ports/win32/sys_arch.c"
)
target_include_directories(lwip PUBLIC
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB}/ports/win32/include"
)
target_include_directories(lwip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(lwip PRIVATE /std:c11 /W0)
else()
    target_compile_options(lwip PRIVATE -std=c11 -w)
endif()

add_executable(awdl_netd main.cpp lwip_proxy.cpp local_io.cpp native_mdns.cpp)
target_include_directories(awdl_netd PRIVATE
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB}/ports/win32/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(awdl_netd PRIVATE lwip ws2_32)
if(MSVC)
    target_compile_options(awdl_netd PRIVATE /W4)
endif()

enable_testing()
add_executable(netd_ipc_test ipc_io_test.cpp)
target_include_directories(netd_ipc_test PRIVATE
    "${LWIP_DIR}/src/include" "${LWIP_CONTRIB}/ports/win32/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(netd_ipc_test PRIVATE lwip ws2_32)
add_test(NAME netd_ipc_io COMMAND netd_ipc_test)
set_tests_properties(netd_ipc_io PROPERTIES TIMEOUT 15)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME netd_process_faults COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/test_netd_process.py" "$<TARGET_FILE:awdl_netd>")
    set_tests_properties(netd_process_faults PROPERTIES TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
