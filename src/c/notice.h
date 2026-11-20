#pragma once
#include <stdbool.h>

// A short message over whatever screen is showing, gone again after a few
// seconds or on any button. For things the wearer has to know about, such as
// "Open Spotify on your phone first".
void notice_show(const char *text, bool is_error);
