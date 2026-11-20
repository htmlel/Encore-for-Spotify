#include <pebble.h>
#include "search.h"
#include "comm.h"
#include "list.h"
#include "playback_data.h"

// Voice search: the watch's dictation turns speech into text, the phone asks
// Spotify, and the results come back as an ordinary list.

#if defined(PBL_MICROPHONE)

static DictationSession *s_session;
static char s_query[100];

static void dictation_done(DictationSession *session, DictationSessionStatus status,
                           char *transcription, void *context) {
  if (status != DictationSessionStatusSuccess || !transcription || !transcription[0]) {
    return;
  }
  strncpy(s_query, transcription, sizeof(s_query) - 1);
  s_query[sizeof(s_query) - 1] = '\0';
  list_window_push(LIST_TYPE_SEARCH);
  comm_send_command(CMD_SEARCH, s_query);
}

void search_start(void) {
  if (!s_session) {
    s_session = dictation_session_create(sizeof(s_query), dictation_done, NULL);
    if (s_session) {
      // The results list shows what was heard; no need to confirm it first.
      dictation_session_enable_confirmation(s_session, false);
    }
  }
  if (!s_session || dictation_session_start(s_session) != DictationSessionStatusSuccess) {
    // No dictation on this watch or phone: say so in an empty results list.
    list_window_push(LIST_TYPE_SEARCH);
    list_mark_done();
    list_set_status("Voice not available");
  }
}

#else

void search_start(void) {
  list_window_push(LIST_TYPE_SEARCH);
  list_mark_done();
  list_set_status("No microphone");
}

#endif
