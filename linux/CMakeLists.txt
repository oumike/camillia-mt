# Headless Camillia-MT for the M5Stack CardputerZero (Linux, aarch64).
# Reuses the firmware's radio and protocol sources from ../src; see
# docs/BUILD.md ("CardputerZero") and linux/build.sh.
cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)  # FetchContent_Populate: sources only, no add_subdirectory
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
project(camillia-czero CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
# Same versions platformio.ini pins for the ESP32 builds.
FetchContent_Declare(radiolib
  URL https://github.com/jgromes/RadioLib/archive/refs/tags/7.8.1.tar.gz)
FetchContent_Declare(mtcrypto
  URL https://github.com/meshtastic/Crypto/archive/1c817c2f27aa4593e07d0f7e13b3dbf8d980bcae.tar.gz)
FetchContent_GetProperties(radiolib)
if(NOT radiolib_POPULATED)
  FetchContent_Populate(radiolib)
endif()
FetchContent_GetProperties(mtcrypto)
if(NOT mtcrypto_POPULATED)
  FetchContent_Populate(mtcrypto)
endif()

file(GLOB RADIOLIB_SRC
  ${radiolib_SOURCE_DIR}/src/*.cpp
  ${radiolib_SOURCE_DIR}/src/utils/*.cpp
  ${radiolib_SOURCE_DIR}/src/protocols/PhysicalLayer/*.cpp
  ${radiolib_SOURCE_DIR}/src/modules/SX126x/*.cpp)
add_library(radiolib STATIC ${RADIOLIB_SRC})
target_include_directories(radiolib PUBLIC ${radiolib_SOURCE_DIR}/src)

set(CRYPTO_DIR ${mtcrypto_SOURCE_DIR})
add_library(mtcrypto STATIC
  ${CRYPTO_DIR}/Crypto.cpp ${CRYPTO_DIR}/BigNumberUtil.cpp ${CRYPTO_DIR}/Hash.cpp
  ${CRYPTO_DIR}/SHA512.cpp ${CRYPTO_DIR}/Curve25519.cpp ${CRYPTO_DIR}/Ed25519.cpp
  ${CRYPTO_DIR}/XEdDSA.cpp)
target_include_directories(mtcrypto PUBLIC ${CRYPTO_DIR})
target_compile_definitions(mtcrypto PUBLIC HOST_BUILD=1)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../src)
# Everything but main(), shared by the daemon and its tests.
add_library(czero_core STATIC
  daemon/settings.cpp
  daemon/gps.cpp
  daemon/store.cpp
  shim/arduino_linux.cpp
  hal/linux_hal.cpp
  hal/linux_board.cpp
  ${FW}/mesh_radio.cpp
  ${FW}/mesh_proto.cpp
  ${FW}/mesh_channel_plan.cpp
  ${FW}/base64_util.cpp
  ${FW}/xeddsa_sig.cpp
  ${FW}/ack_proof.cpp
  ${FW}/sha256_portable.cpp
  ${FW}/debug_flags.cpp
  ${FW}/licensed_mode.cpp)
add_executable(camillia-czero daemon/main.cpp)
add_executable(camillia-czero-tests tests/test_czero.cpp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION APP_VERSION LIMIT_COUNT 1)
target_compile_definitions(czero_core PUBLIC DEVICE_CARDPUTER_ZERO=1 APP_VERSION="${APP_VERSION}")
# shim/ first: its Arduino.h/SPI.h/Wire.h/esp_random.h stand in for the cores'.
target_include_directories(czero_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR} ${FW})
target_include_directories(mtcrypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)

find_library(MBEDCRYPTO_STATIC NAMES libmbedcrypto.a)
if(MBEDCRYPTO_STATIC)
  set(MBEDCRYPTO ${MBEDCRYPTO_STATIC})
else()
  find_library(MBEDCRYPTO NAMES mbedcrypto REQUIRED)
endif()
find_package(Threads REQUIRED)
target_link_libraries(czero_core PUBLIC radiolib mtcrypto ${MBEDCRYPTO} Threads::Threads)
target_link_libraries(camillia-czero PRIVATE czero_core)
target_link_libraries(camillia-czero-tests PRIVATE czero_core)

# ── camillia-czero-ui: the screen, an LVGL client of the daemon ─────────────
# LVGL at the firmware's version (platformio.ini lvgl/lvgl@9.5.0), compiled
# from its sources against ui/lv_conf.h rather than through its own CMake.
FetchContent_Declare(lvgl
  URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()
file(GLOB_RECURSE LVGL_SRC ${lvgl_SOURCE_DIR}/src/*.c)
file(GLOB FONT_SRC ${FW}/fonts/latin/lv_font_montserrat_1[024].c)
add_library(lvgl_czero STATIC ${LVGL_SRC} ${FONT_SRC})
target_include_directories(lvgl_czero PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui ${lvgl_SOURCE_DIR})
target_compile_definitions(lvgl_czero PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

add_executable(camillia-czero-ui ui/main.cpp ui/keyboard.cpp daemon/store.cpp)
target_include_directories(camillia-czero-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(camillia-czero-ui PRIVATE lvgl_czero m)
