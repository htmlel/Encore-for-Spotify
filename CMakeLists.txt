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
  # With -DDEMO_SONG_NAME=..., the Now Playing row names that song.
  if(DEFINED DEMO_SONG_NAME)
    target_compile_definitions(app INTERFACE DEMO_MENU_SONG="${DEMO_SONG_NAME}")
  endif()
endif()

# -DDEMO_NP=1 opens Now Playing with a made-up song (2 = paused, 3 = shuffle on,
# 4 = shuffle and repeat on with the button turned over to repeat,
# 5 = repeat one song, 6 = a pale cover,
# 7 = a liked song).
if(DEFINED DEMO_NP)
  target_compile_definitions(app INTERFACE DEMO_NP=${DEMO_NP})
endif()

# -DDEMO_LOOK=1 shows the full-cover Now Playing look (0 = card), with DEMO_NP.
if(DEFINED DEMO_LOOK)
  target_compile_definitions(app INTERFACE DEMO_LOOK=${DEMO_LOOK})
endif()

# -DDEMO_ART_DIR=<folder> uses a real cover and song in the DEMO_NP pictures:
# the folder holds a demo_art.h (see main.c). Name the song with
# -DDEMO_SONG_NAME=..., -DDEMO_ARTIST_NAME=... and -DDEMO_SONG_LENGTH=<seconds>.
if(DEFINED DEMO_ART_DIR)
  target_include_directories(app INTERFACE ${DEMO_ART_DIR})
  target_compile_definitions(app INTERFACE DEMO_ART=1
    DEMO_SONG="${DEMO_SONG_NAME}" DEMO_ARTIST="${DEMO_ARTIST_NAME}"
    DEMO_SONG_SECONDS=${DEMO_SONG_LENGTH})
endif()
