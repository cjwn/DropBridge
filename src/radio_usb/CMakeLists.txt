cmake_minimum_required(VERSION 3.15)
project(airbridge_radio CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg toolchain must be set before project(); this file is also used from the
# command line with -DCMAKE_TOOLCHAIN_FILE, so the env fallback is just a
# convenience like devourer's own CMakeLists.
if(WIN32 AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET libusb-1.0)

# One chip only: RTL8811CU / RTL8821CU (Jaguar2 C8821C).
foreach(chip JAGUAR1 8814 JAGUAR2_8822B JAGUAR3_8822C JAGUAR3_8822E 8733B
             KESTREL_8852B KESTREL_8852C)
    set(DEVOURER_${chip} OFF CACHE BOOL "" FORCE)
endforeach()
set(DEVOURER_JAGUAR2_8821C ON CACHE BOOL "" FORCE)
set(DEVOURER_PCIE OFF CACHE BOOL "" FORCE)
set(DEVOURER_MT7612U OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../upstream/devourer"
                 devourer EXCLUDE_FROM_ALL)

add_executable(airbridge_radio main.cpp radio_link.h)
target_link_libraries(airbridge_radio PRIVATE devourer PkgConfig::libusb)
target_compile_features(airbridge_radio PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(airbridge_radio PRIVATE /W4)
    target_compile_definitions(airbridge_radio PRIVATE _CRT_SECURE_NO_WARNINGS)
    # devourer's headers contain non-ASCII comments; this is not a code issue.
    target_compile_options(airbridge_radio PRIVATE /wd4819)
endif()
