# Headless Camillia-MT for the M5Stack CardputerZero (Linux, aarch64).
# Reuses the firmware's radio and protocol sources from ../src; see
# docs/BUILD.md ("CardputerZero") and linux/build.sh.
cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)  # FetchContent_Populate: sources only, no add_subdirectory
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
add_executable(camillia-czero
  main_linux.cpp
  shim/arduino_linux.cpp
  hal/linux_hal.cpp
  hal/linux_board.cpp
  ${FW}/mesh_radio.cpp
  ${FW}/mesh_proto.cpp
  ${FW}/xeddsa_sig.cpp
  ${FW}/ack_proof.cpp
  ${FW}/sha256_portable.cpp
  ${FW}/debug_flags.cpp
  ${FW}/licensed_mode.cpp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION APP_VERSION LIMIT_COUNT 1)
target_compile_definitions(camillia-czero PRIVATE DEVICE_CARDPUTER_ZERO=1 APP_VERSION="${APP_VERSION}")
# shim/ first: its Arduino.h/SPI.h/Wire.h/esp_random.h stand in for the cores'.
target_include_directories(camillia-czero PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR} ${FW})
target_include_directories(mtcrypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)

find_library(MBEDCRYPTO_STATIC NAMES libmbedcrypto.a)
if(MBEDCRYPTO_STATIC)
  set(MBEDCRYPTO ${MBEDCRYPTO_STATIC})
else()
  find_library(MBEDCRYPTO NAMES mbedcrypto REQUIRED)
endif()
find_package(Threads REQUIRED)
target_link_libraries(camillia-czero PRIVATE radiolib mtcrypto ${MBEDCRYPTO} Threads::Threads)
