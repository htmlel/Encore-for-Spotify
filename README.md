# Encore for Spotify

A Spotify remote for the Pebble Time 2. See what's playing with album art,
skip, pause, browse your library, search by voice, like songs and pick which
speaker the music plays on.

Based on [Playback for Spotify](https://github.com/alex523ap/Playback-for-Spotify)
by alex_pavlov (MIT license). Encore adds a login that works in the new Pebble
app, voice search, a like button and a device picker.

Not affiliated with or endorsed by Spotify.

## What it does

- **Now Playing:** title, artist, album art and progress
- **Controls:** play/pause, next, previous, seek or volume on a long press
- **Library:** playlists, artists, albums, liked songs, podcasts
- **Search:** say a song, artist, album or playlist and play it
- **Like Song:** add or remove the current song from Liked Songs
- **Play On:** move the music to another speaker or device
- **Queue:** see what's next; long-press a song to queue it
- **Shuffle and repeat**

## Setup

Spotify makes everyone who uses a small app like this register it themselves.
It takes about five minutes, once, and needs Spotify Premium.

The step-by-step guide is at **https://htmlel.github.io/Encore-for-Spotify/**

In short:

1. Create an app at the [Spotify developer dashboard](https://developer.spotify.com/dashboard)
   with the Redirect URI `https://htmlel.github.io/Encore-for-Spotify/` and
   the Web API selected. Add yourself under User Management.
2. In the Pebble app, open Encore's Settings and paste your Client ID.
3. Tap **Copy login link**, open it in your normal browser, log in and agree.
4. Tap **Copy** on the page you land on, go back to Settings, paste it and tap
   **Finish**. The watch tells you when it's connected.

Why the copy and paste? Spotify's login form does not work inside the Pebble
app's built-in browser, so the login is done in your real browser.

## Controls

| Action | Button |
|--------|--------|
| Next track | UP |
| Previous track | DOWN |
| Play / Pause | SELECT |
| Seek or volume | Hold UP / DOWN |
| Like, devices, shuffle, repeat | Hold SELECT on "Now Playing" in the menu |

## Building

Needs the Pebble SDK and `npm install`.

```
pebble build
```

or, with the CMake SDK:

```
export PEBBLE_SDK=/path/to/sdk
cmake -B build -G Ninja && cmake --build build
```

A ready-made build is in `release/encore.pbw`.

## Layout

- `src/c/`: the watch app
- `src/pkjs/`: the phone side: Spotify calls, login, album art, settings page
- `docs/`: the setup guide and login landing page, served by GitHub Pages

## License

MIT. See [LICENSE](LICENSE).
