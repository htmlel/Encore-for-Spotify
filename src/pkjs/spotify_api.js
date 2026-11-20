var auth = require('./spotify_auth');

var BASE_URL = 'https://api.spotify.com/v1';

function apiRequest(method, path, body, callback) {
  if (typeof body === 'function') {
    callback = body;
    body = null;
  }

  var token = auth.getToken();
  if (!token) {
    if (callback) callback(new Error('Not authenticated'), null);
    return;
  }

  // Auto-refresh if token is likely expired
  if (auth.isTokenExpired()) {
    auth.refreshAccessToken(function(err, newToken) {
      if (err) {
        if (callback) callback(err, null);
        return;
      }
      apiRequest(method, path, body, callback);
    });
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.open(method, BASE_URL + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + token);
  if (body) {
    xhr.setRequestHeader('Content-Type', 'application/json');
  }

  xhr.onload = function() {
    if (xhr.status === 401) {
      // Token rejected, attempt one refresh and retry
      auth.refreshAccessToken(function(err, newToken) {
        if (err) {
          // Don't clear tokens here — let the user re-login manually
          if (callback) callback(new Error('Token expired'), null);
          return;
        }
        apiRequest(method, path, body, callback);
      });
      return;
    }
    if (xhr.status === 204) {
      // No content (e.g., no active device)
      if (callback) callback(null, null);
      return;
    }
    if (xhr.status < 200 || xhr.status >= 300) {
      if (callback) callback(new Error('API error ' + xhr.status), null);
      return;
    }
    try {
      var data = JSON.parse(xhr.responseText);
      if (callback) callback(null, data);
    } catch (e) {
      // If parsing fails but the status was 200/202 (success),
      // it means the endpoint returned an empty or plain text response
      // (like a snapshot ID) which is normal for Shuffle, Repeat, and Queue.
      if (callback) callback(null, null);
    }
  };

  xhr.onerror = function() {
    if (callback) callback(new Error('Network error'), null);
  };

  xhr.send(body ? JSON.stringify(body) : null);
}

module.exports = {
  getCurrentPlayback: function(cb) {
    // additional_types=episode so /me/player returns podcast episodes in
    // `item` instead of nulling it out. Without this, the watch has no
    // way to show podcast titles.
    apiRequest('GET', '/me/player?additional_types=episode', cb);
  },

  getPlaylists: function(cb) {
    apiRequest('GET', '/me/playlists?limit=20', cb);
  },

  getFollowedArtists: function(cb) {
    apiRequest('GET', '/me/following?type=artist&limit=20', cb);
  },

  getSavedAlbums: function(cb) {
    apiRequest('GET', '/me/albums?limit=20', cb);
  },

  getLikedTracks: function(cb) {
    apiRequest('GET', '/me/tracks?limit=20', cb);
  },

  play: function(cb) {
    apiRequest('PUT', '/me/player/play', cb);
  },

  pause: function(cb) {
    apiRequest('PUT', '/me/player/pause', cb);
  },

  nextTrack: function(cb) {
    apiRequest('POST', '/me/player/next', cb);
  },

  previousTrack: function(cb) {
    apiRequest('POST', '/me/player/previous', cb);
  },

  setVolume: function(percent, cb) {
    apiRequest('PUT', '/me/player/volume?volume_percent=' + percent, cb);
  },

  seek: function(positionMs, cb) {
    apiRequest('PUT', '/me/player/seek?position_ms=' + positionMs, cb);
  },

  getSavedShows: function(cb) {
    apiRequest('GET', '/me/shows?limit=20', cb);
  },

  getShowEpisodes: function(showId, cb) {
    apiRequest('GET', '/shows/' + showId + '/episodes?limit=1', cb);
  },

  playContext: function(contextUri, cb, customBody) {
    var body = customBody || { context_uri: contextUri };
    apiRequest('PUT', '/me/player/play', body, cb);
  },

  getQueue: function(cb) {
    apiRequest('GET', '/me/player/queue', cb);
  },

  addToQueue: function(uri, cb) {
    apiRequest('POST', '/me/player/queue?uri=' + encodeURIComponent(uri), cb);
  },

  setShuffle: function(on, cb) {
    apiRequest('PUT', '/me/player/shuffle?state=' + (on ? 'true' : 'false'), cb);
  },

  setRepeat: function(state, cb) {
    // state: "off" | "context" | "track"
    apiRequest('PUT', '/me/player/repeat?state=' + state, cb);
  },

  getPlaylistFirstTrack: function(id, cb) {
    apiRequest('GET', '/playlists/' + id + '/tracks?limit=1&fields=items(track(uri))', cb);
  },

  getAlbumFirstTrack: function(id, cb) {
    apiRequest('GET', '/albums/' + id + '/tracks?limit=1', cb);
  },

  // The library calls take Spotify URIs (spotify:track:...). The older
  // per-type calls (/me/tracks) are tried when an app still has those.
  isSaved: function(uri, cb) {
    apiRequest('GET', '/me/library/contains?uris=' + encodeURIComponent(uri), function(err, data) {
      if (!err) return cb(null, data);
      apiRequest('GET', '/me/tracks/contains?ids=' + encodeURIComponent(uri.split(':')[2]), cb);
    });
  },

  setSaved: function(uri, saved, cb) {
    var method = saved ? 'PUT' : 'DELETE';
    apiRequest(method, '/me/library?uris=' + encodeURIComponent(uri), function(err, data) {
      if (!err) return cb(null, data);
      apiRequest(method, '/me/tracks?ids=' + encodeURIComponent(uri.split(':')[2]), cb);
    });
  },

  getDevices: function(cb) {
    apiRequest('GET', '/me/player/devices', cb);
  },

  transferPlayback: function(deviceId, cb) {
    apiRequest('PUT', '/me/player', { device_ids: [deviceId], play: true }, cb);
  },

  // Spotify caps a search at 10 results per type.
  search: function(query, cb) {
    apiRequest('GET', '/search?type=track,artist,album,playlist&limit=5&q=' +
               encodeURIComponent(query), cb);
  },

  getArtistTopTrack: function(id, cb) {
    apiRequest('GET', '/artists/' + id + '/top-tracks?market=from_token', cb);
  }
};
