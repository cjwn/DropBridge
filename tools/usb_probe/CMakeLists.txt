cmake_minimum_required(VERSION 3.15)
project(usb_probe C)

# Same libusb lookup as the devourer build: pkgconf with MSVC syntax.
find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(usb_probe usb_probe.c)
target_link_libraries(usb_probe PRIVATE PkgConfig::libusb)
