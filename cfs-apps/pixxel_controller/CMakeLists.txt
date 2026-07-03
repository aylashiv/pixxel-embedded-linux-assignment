cmake_minimum_required(VERSION 2.6.4)
project(PIXXEL_CONTROLLER C)

include_directories(fsw/inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)                    # pixxel_msgids.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../mission/pixxel_defs) # cpu1_msgids.h

add_cfe_app(pixxel_ctrl fsw/src/pixxel_controller.c)
