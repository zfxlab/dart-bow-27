set(PNX_BOARD_IOC "${CMAKE_CURRENT_LIST_DIR}/f407_c_board.ioc")
set(PNX_BOARD_CUBEMX_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake/stm32cubemx")
set(PNX_BOARD_TOOLCHAIN "${CMAKE_CURRENT_LIST_DIR}/toolchain.cmake")
set(PNX_BOARD_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/STM32F407XX_FLASH.ld")
set(PNX_BOARD_PARAMS "${CMAKE_CURRENT_LIST_DIR}/../../configs/boards/f407_c_board/params.json")
set(PNX_BOARD_ROBOT_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../../configs/boards/f407_c_board/robot.json")
set(PNX_BOARD_FAMILY "stm32f4")

set(PNX_BOARD_OPENOCD_TARGET "target/stm32f4x.cfg")
