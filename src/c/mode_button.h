#pragma once
#include <pebble.h>

// One button that is either shuffle or repeat. Holding it flips it over to
// the other one, like a coin: it narrows to nothing, changes face, and
// widens again. Which face is up is shared by both Now Playing looks.

// Draws the sign that is face up, centered on `center`: `on` colored when
// that mode is switched on, white when not. A dot in the `on` color, at
// `dot` from the center, shows when the mode that is face down is on, so
// neither is ever on out of sight. `small` draws it three quarters the size.
void mode_button_draw(GContext *ctx, GPoint center, bool shuffle, int repeat, GColor on,
                      GPoint dot, bool small);

// True while the repeat face is up.
bool mode_button_is_repeat(void);

// Flips the button over. `redraw` is called for every step of the turn.
void mode_button_flip(void (*redraw)(void));

// Stops a flip part way, when the screen goes away.
void mode_button_stop(void);
