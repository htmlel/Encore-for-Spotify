#pragma once
#include <pebble.h>
#include "ui.h"

// The two Now Playing looks on the Pebble Time 2. Each offers the same calls
// as ui.h under its own prefix; ui_time2.c passes ui.h calls on to them.

// ui_card.c
void card_init(Window *window);
void card_deinit(void);
void card_start_loading(void);
void card_set_album_art(GBitmap *bitmap);
void card_clear_album_art_instant(void);
void card_set_status(const char *text);
void card_set_track_info(const char *title, const char *artist);
void card_set_progress(int elapsed_sec, int total_sec);
void card_set_shuffle(bool on);
void card_set_repeat(int state);
void card_hint_animation_dir(bool reverse);
void card_set_playing(bool playing);
void card_refresh(void);
UiHit card_hit(GPoint tap);
void card_wake(void);
bool card_controls_shown(void);
bool card_hold(GPoint at);

// ui_cover.c
void cover_init(Window *window);
void cover_deinit(void);
void cover_start_loading(void);
void cover_set_album_art(GBitmap *bitmap);
void cover_clear_album_art_instant(void);
void cover_set_status(const char *text);
void cover_set_track_info(const char *title, const char *artist);
void cover_set_progress(int elapsed_sec, int total_sec);
void cover_set_shuffle(bool on);
void cover_set_repeat(int state);
void cover_hint_animation_dir(bool reverse);
void cover_set_playing(bool playing);
void cover_refresh(void);
UiHit cover_hit(GPoint tap);
void cover_wake(void);
bool cover_controls_shown(void);
