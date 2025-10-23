cmake_minimum_required(VERSION 3.13)
project(UEFIRomExtract C)

set(CMAKE_C_STANDARD 17)

include_directories(.)

add_executable(UEFIRomExtract
        main.c
        main.h)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -s")