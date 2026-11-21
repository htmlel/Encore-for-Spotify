cmake_minimum_required(VERSION 3.20)

# Before project(): the SDK selects the toolchain.
find_package(PebbleSDK REQUIRED HINTS $ENV{PEBBLE_SDK})

project(encore C)

file(GLOB PLAYBACK_SOURCES CONFIGURE_DEPENDS src/c/*.c)
pebble_app(SOURCES ${PLAYBACK_SOURCES})
# Every JS file is listed so a change to any of them rebuilds the bundle.
file(GLOB ENCORE_JS CONFIGURE_DEPENDS src/pkjs/*.js)
pebble_js(SOURCES ${ENCORE_JS} ENTRY src/pkjs/index.js)

# -DDEMO_NOTICE=ON shows a sample notice at launch, for a simulator picture.
option(DEMO_NOTICE "Show a sample notice at launch" OFF)
if(DEMO_NOTICE)
  target_compile_definitions(app INTERFACE DEMO_NOTICE=1)
endif()

# -DDEMO_MENU=ON skips the tutorial, for a simulator picture of the main menu.
option(DEMO_MENU "Skip the tutorial at launch" OFF)
if(DEMO_MENU)
  target_compile_definitions(app INTERFACE DEMO_MENU=1)
endif()

# -DDEMO_NP=1 opens Now Playing with a made-up song (2 = paused, 3 = shuffle on,
# 4 = shuffle and repeat on with the button turned over to repeat,
# 5 = repeat one song, 6 = a pale cover).
if(DEFINED DEMO_NP)
  target_compile_definitions(app INTERFACE DEMO_NP=${DEMO_NP})
endif()

# -DDEMO_LOOK=1 shows the full-cover Now Playing look (0 = card), with DEMO_NP.
if(DEFINED DEMO_LOOK)
  target_compile_definitions(app INTERFACE DEMO_LOOK=${DEMO_LOOK})
endif()
