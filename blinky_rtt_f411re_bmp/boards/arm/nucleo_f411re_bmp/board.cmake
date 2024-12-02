# SPDX-License-Identifier: Apache-2.0

# keep first
#board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
#board_runner_args(jlink "--device=STM32F411RE" "--speed=4000")
# board_runner_args(blackmagicprobe "--connect-rst")

# keep first
#include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
#include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
#include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/blackmagicprobe.board.cmake)