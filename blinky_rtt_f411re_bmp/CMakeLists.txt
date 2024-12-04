# SPDX-License-Identifier: Apache-2.0

# variable_watch(BOARD_ROOT) # pour debug 

# ajoute le répertoire projet à la liste BOARD_ROOT
# pour permettre de détecter la carte dans le dossier /boards local
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_sources(app PRIVATE src/main.c)
