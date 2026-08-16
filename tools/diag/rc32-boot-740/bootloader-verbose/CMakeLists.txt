# #740 -- verbose bootloader build. The app target is a stub; only the
# bootloader.bin this project emits is used.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rc32_bootloader_verbose)
