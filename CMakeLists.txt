cmake_minimum_required(VERSION 3.20)

# Before project(): the SDK selects the toolchain.
find_package(PebbleSDK REQUIRED HINTS $ENV{PEBBLE_SDK})

project(encore C)

file(GLOB PLAYBACK_SOURCES CONFIGURE_DEPENDS src/c/*.c)
pebble_app(SOURCES ${PLAYBACK_SOURCES})
# Every JS file is listed so a change to any of them rebuilds the bundle.
file(GLOB ENCORE_JS CONFIGURE_DEPENDS src/pkjs/*.js)
pebble_js(SOURCES ${ENCORE_JS} ENTRY src/pkjs/index.js)
