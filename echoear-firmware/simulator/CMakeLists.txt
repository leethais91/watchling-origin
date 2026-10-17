# Host (macOS/Linux) simulator for the dashboard UI. Builds the firmware's own
# agent_dashboard.c and assets against the exact LVGL the firmware resolved.
cmake_minimum_required(VERSION 3.16)
project(dashboard_sim C)

set(CMAKE_C_STANDARD 11)
set(FW_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/../main)
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl)

if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL sources missing: run `idf.py reconfigure` in echoear-firmware first")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2 m)

file(GLOB DASHBOARD_ASSETS CONFIGURE_DEPENDS ${FW_MAIN}/assets/*.c)
add_executable(dashboard_sim sim_main.c ${FW_MAIN}/agent_dashboard.c ${DASHBOARD_ASSETS})
target_include_directories(dashboard_sim PRIVATE ${FW_MAIN})
target_compile_options(dashboard_sim PRIVATE -Wall -Wextra)
target_link_libraries(dashboard_sim PRIVATE lvgl)
