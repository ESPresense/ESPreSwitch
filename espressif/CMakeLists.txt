# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.19)

# The stage binary erases and rewrites flash from 0x0 with the payload image, so its MD5 and
# size are baked in from ../payload.json (shared with the server) instead of edited by hand.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../payload.json" PAYLOAD_JSON)
string(JSON PAYLOAD_URL GET "${PAYLOAD_JSON}" url)
string(JSON PAYLOAD_MD5 GET "${PAYLOAD_JSON}" md5)
set(PAYLOAD_FILE "${CMAKE_BINARY_DIR}/payload.bin")
file(DOWNLOAD "${PAYLOAD_URL}" "${PAYLOAD_FILE}" EXPECTED_MD5 "${PAYLOAD_MD5}" STATUS PAYLOAD_STATUS)
list(GET PAYLOAD_STATUS 0 PAYLOAD_STATUS_CODE)
if(NOT PAYLOAD_STATUS_CODE EQUAL 0)
    message(FATAL_ERROR "Payload download failed: ${PAYLOAD_STATUS}")
endif()
file(SIZE "${PAYLOAD_FILE}" PAYLOAD_SIZE)

# Erase whole 4 KB sectors. The stage runs from the factory ota_1 at 0x220000 (ota_0 is at
# 0x20000), so the write must stay below it or it overwrites itself mid-flash and bricks the plug.
math(EXPR WRITE_SIZE_DEC "((${PAYLOAD_SIZE} + 4095) / 4096) * 4096")
math(EXPR WRITE_SIZE "${WRITE_SIZE_DEC}" OUTPUT_FORMAT HEXADECIMAL)
if(WRITE_SIZE_DEC LESS_EQUAL 131072 OR WRITE_SIZE_DEC GREATER 2228224)  # 0x20000, 0x220000
    message(FATAL_ERROR "Payload is ${PAYLOAD_SIZE} bytes; it must end between 0x20000 and 0x220000 (WRITE_SIZE=${WRITE_SIZE})")
endif()
string(REGEX REPLACE "(..)" "0x\\1, " PAYLOAD_MD5_BYTES "${PAYLOAD_MD5}")
string(REGEX REPLACE ", $" "" PAYLOAD_MD5_BYTES "${PAYLOAD_MD5_BYTES}")
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/main/include/payload_config.h"
    "// Generated from payload.json by CMakeLists.txt; do not edit.\n"
    "#pragma once\n"
    "#define BINARY_MD5 (unsigned char[]){ ${PAYLOAD_MD5_BYTES} }\n"
    "#define WRITE_SIZE ${WRITE_SIZE}\n")
message(STATUS "Payload ${PAYLOAD_URL}: ${PAYLOAD_SIZE} bytes, WRITE_SIZE=${WRITE_SIZE}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(switchpresense)
