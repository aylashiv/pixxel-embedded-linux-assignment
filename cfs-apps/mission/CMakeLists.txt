cmake_minimum_required(VERSION 3.13.1)
project(PIXXEL_MISSION C)

# ---------------------------------------------------------------------------
# Directory layout expected by this mission cmake:
#
#   <mission-root>/          (this file lives here)
#     targets.cmake          CPU / app list
#     defs/                  mission-wide headers (msgids, platform cfg)
#     startup/               cfe_es_startup.scr
#     ../pixxel_controller/  cFS app source
#     ../pixxel_main/        cFS app source
#     cfe/                   cFE core (git clone nasa/cFE)
#     osal/                  OSAL    (git clone nasa/osal)
#     psp/                   PSP     (git clone nasa/psp)
#
# ---------------------------------------------------------------------------

# cFS build system entry point — provided by the cFE source tree
set(CFS_MISSION_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Tell cFE cmake where to find app sources
list(APPEND MISSION_APPLIST_SEARCH_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Pull in the cFE mission cmake infrastructure
include(${CMAKE_CURRENT_SOURCE_DIR}/cfe/cmake/mission_build.cmake)
