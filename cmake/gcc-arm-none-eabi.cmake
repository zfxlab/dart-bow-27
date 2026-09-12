# Thin board selector. Compiler/CPU/linker flags belong to the board toolchain.
set(PNX_BOARD "h723_mc02" CACHE STRING "Selected PNX board profile")
set(PNX_BOARD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../boards/${PNX_BOARD}/toolchain.cmake")
if(NOT EXISTS "${PNX_BOARD_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Unknown PNX_BOARD='${PNX_BOARD}': ${PNX_BOARD_TOOLCHAIN_FILE} not found")
endif()
include("${PNX_BOARD_TOOLCHAIN_FILE}")
