// Now Playing on the Pebble Time 2 comes in two looks, chosen under About:
//
//   Card        ui_card.c   the cover beside the song's name, pill buttons
//                           and a slider, on black with green accents
//   Full cover  ui_cover.c  the cover across the whole screen, with controls
//                           that fade away while music plays
//
// This file is ui.h for the Time 2: it keeps both looks up to date and shows
// the chosen one. (ui.c is the original screen, for the other watches.)

#include <pebble.h>
#include "ui.h"
#include "ui_looks.h"
#include "playback_data.h"

#if defined(PBL_PLATFORM_EMERY)

// The look on screen; only read while the Now Playing window is up.
static bool s_showing_cover;

void ui_init(Window *window) {
  s_showing_cover = now_playing_look_get() == NP_LOOK_COVER;
  if (s_showing_cover) {
    cover_init(window);
  } else {
    card_init(window);
  }
}

void ui_deinit(void) {
  if (s_showing_cover) {
    cover_deinit();
  } else {
    card_deinit();
  }
}

// What is playing goes to both, so either is ready the moment it is chosen.
void ui_start_loading(void) { card_start_loading(); cover_start_loading(); }
void ui_set_album_art(GBitmap *bitmap) { card_set_album_art(bitmap); cover_set_album_art(bitmap); }
void ui_clear_album_art_instant(void) { card_clear_album_art_instant(); cover_clear_album_art_instant(); }
void ui_set_status(const char *text) { card_set_status(text); cover_set_status(text); }
void ui_set_track_info(const char *title, const char *artist) {
  card_set_track_info(title, artist);
  cover_set_track_info(title, artist);
}
void ui_set_progress(int elapsed_sec, int total_sec) {
  card_set_progress(elapsed_sec, total_sec);
  cover_set_progress(elapsed_sec, total_sec);
}
void ui_set_shuffle(bool on) { card_set_shuffle(on); cover_set_shuffle(on); }
void ui_set_repeat(int state) { card_set_repeat(state); cover_set_repeat(state); }
void ui_hint_animation_dir(bool reverse) { (void)reverse; }
void ui_set_playing(bool playing) { card_set_playing(playing); cover_set_playing(playing); }
void ui_refresh(void) { card_refresh(); cover_refresh(); }

UiHit ui_hit(GPoint tap) {
  return s_showing_cover ? cover_hit(tap) : card_hit(tap);
}

void ui_wake(void) {
  if (s_showing_cover) {
    cover_wake();
  }
}

bool ui_controls_shown(void) {
  return s_showing_cover ? cover_controls_shown() : true;
}

bool ui_hold(GPoint at) {
  return s_showing_cover ? false : card_hold(at);
}

#endif  // PBL_PLATFORM_EMERY
