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

# ── camillia: the firmware itself (src/main_lvgl.cpp) as a launcher app ──────
# The same sources the T-Deck environment builds, compiled for Linux against
# app/shim (Arduino/ESP-IDF stand-ins) and drawn through LovyanGFX's Linux
# framebuffer panel. Board specifics stay behind DEVICE_CARDPUTER_ZERO.
FetchContent_Declare(lovyangfx
  URL https://github.com/lovyan03/LovyanGFX/archive/refs/tags/1.2.34.tar.gz)
# TinyGPSPlus 1.1.0 (library.json "1.1.0"; upstream has no tag for it).
FetchContent_Declare(tinygps
  URL https://github.com/mikalhart/TinyGPSPlus/archive/c093432911f5e1a2f54844976d3a14ec212f2403.tar.gz)
# PubSubClient 2.8 (platformio.ini knolleary/PubSubClient@2.8.0).
FetchContent_Declare(pubsubclient
  URL https://github.com/knolleary/pubsubclient/archive/refs/tags/v2.8.tar.gz)
foreach(dep lovyangfx tinygps pubsubclient)
  FetchContent_GetProperties(${dep})
  if(NOT ${dep}_POPULATED)
    FetchContent_Populate(${dep})
  endif()
endforeach()

# The ESP32 builds' PubSubClient topic-length fix, applied from the same
# OLD_*/NEW_* strings in tools/patch_pubsubclient_topiclen.py.
file(READ ${FW}/../tools/patch_pubsubclient_topiclen.py PUBSUB_PATCH)
file(READ ${pubsubclient_SOURCE_DIR}/src/PubSubClient.cpp PUBSUB_CPP)
function(pubsub_patch_string name out)
  string(FIND "${PUBSUB_PATCH}" "${name} = \"\"\"" at)
  if(at LESS 0)
    message(FATAL_ERROR "PubSubClient patch: ${name} missing")
  endif()
  string(LENGTH "${name} = \"\"\"" skip)
  math(EXPR at "${at} + ${skip}")
  string(SUBSTRING "${PUBSUB_PATCH}" ${at} -1 rest)
  string(FIND "${rest}" "\"\"\"" end)
  string(SUBSTRING "${rest}" 0 ${end} value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()
foreach(hunk READ LOOP)
  pubsub_patch_string(OLD_${hunk} old)
  pubsub_patch_string(NEW_${hunk} new)
  string(FIND "${PUBSUB_CPP}" "${old}" first)
  string(FIND "${PUBSUB_CPP}" "${old}" last REVERSE)
  if(first LESS 0 OR NOT first EQUAL last)
    message(FATAL_ERROR "PubSubClient patch: ${hunk} hunk does not match exactly once")
  endif()
  string(REPLACE "${old}" "${new}" PUBSUB_CPP "${PUBSUB_CPP}")
endforeach()
set(PUBSUB_SRC ${CMAKE_BINARY_DIR}/pubsubclient/PubSubClient.cpp)
file(CONFIGURE OUTPUT ${PUBSUB_SRC} CONTENT "${PUBSUB_CPP}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${FW}/../tools/patch_pubsubclient_topiclen.py)

# TLS for WiFiClientSecure, static like mbedcrypto so the device needs no
# extra packages.
foreach(lib mbedtls mbedx509)
  string(TOUPPER ${lib} var)
  find_library(${var}_STATIC NAMES lib${lib}.a)
  if(${var}_STATIC)
    set(${var} ${${var}_STATIC})
  else()
    find_library(${var} NAMES ${lib} REQUIRED)
  endif()
endforeach()

# String/Print/Stream from the arduino-esp32 core the ESP32 builds use
# (espressif32@7.0.1 = arduino-esp32 2.0.17), so they behave identically.
set(ACORE_DIR ${CMAKE_BINARY_DIR}/arduino-core)
set(ACORE_URL https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/cores/esp32)
foreach(f WString.h WString.cpp Print.h Print.cpp Printable.h Stream.h Stream.cpp
          StreamString.h StreamString.cpp stdlib_noniso.h stdlib_noniso.c WCharacter.h)
  if(NOT EXISTS ${ACORE_DIR}/${f})
    file(DOWNLOAD ${ACORE_URL}/${f} ${ACORE_DIR}/${f} STATUS st)
    list(GET st 0 rc)
    if(NOT rc EQUAL 0)
      file(REMOVE ${ACORE_DIR}/${f})
      message(FATAL_ERROR "download ${f}: ${st}")
    endif()
  endif()
endforeach()

set(APP_FW_SRC
  main_lvgl.cpp i18n.cpp spell.cpp spell_en.cpp mqtt_bridge.cpp keyboard.cpp
  battery_util.cpp power_mgr.cpp emoji_font.cpp env_sensor.cpp gps.cpp los.cpp
  weather.cpp config_io.cpp mesh_channel_plan.cpp admin_proto.cpp admin_client.cpp
  admin_peers.cpp storage.cpp base64_util.cpp web_config.cpp web_theme.cpp
  community_presets.cpp ota_update.cpp mesh_radio.cpp mesh_proto.cpp mesh_beacon.cpp
  licensed_mode.cpp cs_proto.cpp cs_client.cpp sha256_portable.cpp xeddsa_sig.cpp
  ack_proof.cpp channel_mgr.cpp dm_mgr.cpp node_db.cpp key_pin.cpp node_name_cache.cpp
  wardrive_log.cpp live_util.cpp live_feed.cpp debug_flags.cpp ignore_list.cpp
  fonts/lv_font_montserrat_bold_12.c)
list(TRANSFORM APP_FW_SRC PREPEND ${FW}/)
file(GLOB APP_FONT_SRC ${FW}/fonts/latin/*.c)
file(GLOB APP_SHIM_SRC app/*.cpp)
file(GLOB_RECURSE LGFX_SRC
  ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/*.cpp ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/*.c
  ${lovyangfx_SOURCE_DIR}/src/lgfx/utility/*.c ${lovyangfx_SOURCE_DIR}/src/lgfx/Fonts/efont/*.c
  ${lovyangfx_SOURCE_DIR}/src/lgfx/Fonts/IPA/*.c)
list(FILTER LGFX_SRC EXCLUDE REGEX "/platforms/(esp|rp2040|samd|stm32|spresense|arduino|sdl|opencv)")

add_library(lgfx_fb STATIC ${LGFX_SRC})
target_include_directories(lgfx_fb PUBLIC ${lovyangfx_SOURCE_DIR}/src)
target_compile_definitions(lgfx_fb PUBLIC LGFX_LINUX_FB=1)

# LVGL again, this time with the firmware's own lv_conf.h.
add_library(lvgl_fw STATIC ${LVGL_SRC} ${APP_FONT_SRC})
target_include_directories(lvgl_fw PUBLIC ${FW} ${lvgl_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/app/shim)
target_compile_definitions(lvgl_fw PUBLIC LV_CONF_INCLUDE_SIMPLE=1 DEVICE_CARDPUTER_ZERO=1
  UI_LVGL_POC=1 I18N_ENABLED=1)

add_executable(camillia ${APP_FW_SRC} ${APP_SHIM_SRC}
  ${ACORE_DIR}/WString.cpp ${ACORE_DIR}/Print.cpp ${ACORE_DIR}/Stream.cpp
  ${ACORE_DIR}/StreamString.cpp ${ACORE_DIR}/stdlib_noniso.c
  ${tinygps_SOURCE_DIR}/src/TinyGPS++.cpp
  ${PUBSUB_SRC}
  ui/keyboard.cpp hal/linux_hal.cpp hal/linux_board.cpp)
# TinyGPS++ brings its own millis() unless ARDUINO is set; the shim has one.
set_source_files_properties(${tinygps_SOURCE_DIR}/src/TinyGPS++.cpp
  TARGET_DIRECTORY camillia PROPERTIES COMPILE_DEFINITIONS ARDUINO=10819)
target_compile_definitions(camillia PRIVATE APP_VERSION="${APP_VERSION}"
  RADIOLIB_EXCLUDE_APRS=1)
# app/shim first so its Arduino.h and ESP-IDF stand-ins win; ACORE_DIR for the
# String/Print/Stream headers they include.
target_include_directories(camillia PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app/shim
  ${ACORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${FW} ${FW}/hal ${tinygps_SOURCE_DIR}/src
  ${pubsubclient_SOURCE_DIR}/src)
target_compile_options(camillia PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_link_libraries(camillia PRIVATE lvgl_fw lgfx_fb radiolib mtcrypto
  ${MBEDTLS} ${MBEDX509} ${MBEDCRYPTO} Threads::Threads m)
