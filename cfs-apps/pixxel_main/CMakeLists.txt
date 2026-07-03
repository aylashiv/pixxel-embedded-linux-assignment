cmake_minimum_required(VERSION 2.6.4)
project(PIXXEL_MAIN C)

include_directories(fsw/inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)                          # pixxel_msgids.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../pixxel_controller/fsw/inc) # pixxel_controller_msg.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../mission/pixxel_defs)       # cpu1_msgids.h

add_cfe_app(pixxel_main fsw/src/pixxel_main.c)
