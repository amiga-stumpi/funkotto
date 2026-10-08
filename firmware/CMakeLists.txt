cmake_minimum_required(VERSION 3.31)

# M1 is intentionally a Pico 2 W / Arm diagnostic image only.
set(PICO_BOARD pico2_w CACHE STRING "Board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
if(NOT PICO_BOARD STREQUAL "pico2_w" OR NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
    message(FATAL_ERROR "FunkOtto M1 requires pico2_w / rp2350-arm-s")
endif()
if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Run tools/bootstrap_sdk.py and set PICO_SDK_PATH")
endif()
find_package(Git REQUIRED)
file(READ "${CMAKE_CURRENT_LIST_DIR}/dependencies.json" FO_DEPS)
string(JSON FO_SDK_SHA GET "${FO_DEPS}" pico_sdk commit)
string(JSON FO_USB_SHA GET "${FO_DEPS}" tinyusb commit)
string(JSON FO_PICOTOOL_SHA GET "${FO_DEPS}" picotool commit)
foreach(ITEM IN ITEMS SDK USB)
    if(ITEM STREQUAL "SDK")
        set(FO_CHECK_PATH "${PICO_SDK_PATH}")
    else()
        set(FO_CHECK_PATH "${PICO_SDK_PATH}/lib/tinyusb")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${FO_CHECK_PATH}" rev-parse HEAD
        OUTPUT_VARIABLE FO_ACTUAL OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE FO_RESULT)
    if(NOT FO_RESULT EQUAL 0 OR NOT FO_ACTUAL STREQUAL FO_${ITEM}_SHA)
        message(FATAL_ERROR "Wrong ${ITEM} commit; use firmware/dependencies.json")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${FO_CHECK_PATH}" diff --quiet HEAD --
        RESULT_VARIABLE FO_DIRTY)
    if(NOT FO_DIRTY EQUAL 0)
        message(FATAL_ERROR "Tracked ${ITEM} sources have local changes")
    endif()
endforeach()
set(PICOTOOL_GIT_BRANCH "${FO_PICOTOOL_SHA}" CACHE STRING "Pinned picotool" FORCE)
set(PICOTOOL_FORCE_FETCH_FROM_GIT ON CACHE BOOL "Use pinned picotool source" FORCE)
include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
project(funkotto VERSION 0.1.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Keep the physical flash size truthful; restrict only the linker's image region.
math(EXPR FO_FLASH_TOTAL "${PICO_FLASH_SIZE_BYTES}")
if(NOT FO_FLASH_TOTAL EQUAL 4194304)
    message(FATAL_ERROR "Expected the Pico 2 W 4 MiB flash layout")
endif()
math(EXPR FO_CONFIG_OFFSET "${FO_FLASH_TOTAL} - 8192")
math(EXPR FO_CONFIG_START "0x10000000 + ${FO_CONFIG_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR FO_IMAGE_BYTES "${FO_CONFIG_OFFSET} - 4096")
file(READ "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld" FO_LD)
string(REPLACE "INCLUDE \"pico_flash_region.ld\""
    "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${FO_IMAGE_BYTES}" FO_LD "${FO_LD}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/funkotto.ld" "${FO_LD}\n__funkotto_config_start = ${FO_CONFIG_START};\n__funkotto_image_limit = 0x103fd000;\nASSERT(__flash_binary_end <= __funkotto_image_limit, \"Firmware overlaps reserved sectors\")\n")

set(FUNKOTTO_VERSION "0.1.0-m1")
set(FUNKOTTO_SOURCE_ID "development" CACHE STRING "Source commit or candidate ID")
if(NOT FUNKOTTO_SOURCE_ID MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "Invalid source ID")
endif()
configure_file(include/funkotto/build_info.h.in generated/funkotto/build_info.h @ONLY)
add_executable(funkotto_m1 src/main.c src/board.c src/console.c)
target_include_directories(funkotto_m1 PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(funkotto_m1 PRIVATE pico_stdlib hardware_watchdog)
target_compile_options(funkotto_m1 PRIVATE
    "-ffile-prefix-map=${PICO_SDK_PATH}=pico-sdk"
    "-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=firmware"
    "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=build")
set_source_files_properties(src/main.c src/board.c src/console.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wconversion;-Wshadow")
set_target_properties(funkotto_m1 PROPERTIES LINKER_LANGUAGE C)
target_compile_definitions(funkotto_m1 PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
    PICO_STDIO_USB_POST_CONNECT_WAIT_DELAY_MS=0
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0)
pico_set_linker_script(funkotto_m1 "${CMAKE_CURRENT_BINARY_DIR}/funkotto.ld")
pico_enable_stdio_uart(funkotto_m1 0) # GP0/GP1 belong to the parallel data bus!
pico_enable_stdio_usb(funkotto_m1 1)
pico_set_program_name(funkotto_m1 "FunkOtto M1")
pico_set_program_version(funkotto_m1 "${FUNKOTTO_VERSION}")
# SDK's default RP2350-E10 absolute block targets 0x10ffff00, which can
# alias the last sector on a 4 MiB flash. Give it its own reserved sector.
set_target_properties(funkotto_m1 PROPERTIES PICOTOOL_EXTRA_UF2_ARGS "--abs-block;0x103fdf00")
pico_add_extra_outputs(funkotto_m1)
