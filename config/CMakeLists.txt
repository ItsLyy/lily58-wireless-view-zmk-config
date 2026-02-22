# CMakeLists.txt
# Add this to your Lily58 ZMK config repo's CMakeLists.txt
# so the custom OLED source gets compiled into the firmware.

cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(lily58)

# Include custom OLED display source
target_sources(app PRIVATE
    lily58_oled.c
)
