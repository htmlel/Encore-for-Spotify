// Spotify login (authorization code with PKCE) and token refresh.

var pkce = require('./pkce');
var configPage = require('./config_page');

// No built-in Client ID: each person uses their own, entered in Settings.
var SPOTIFY_CLIENT_ID = '';
// Where Spotify sends the browser after login: the page in docs/, served by
// GitHub Pages. It has to be listed under Redirect URIs in the Spotify app's
// dashboard. The page only shows a Copy button for its own address, which
// carries the one-time code.
var SPOTIFY_REDIRECT_URI = 'https://htmlel.github.io/Encore-for-Spotify/';
var SPOTIFY_SCOPES = 'user-read-playback-state user-modify-playback-state user-read-currently-playing playlist-read-private user-library-read user-library-modify user-follow-read';

var TOKEN_KEY = 'spotify_token';
var EXPIRY_KEY = 'spotify_token_expiry';
var REFRESH_KEY = 'spotify_refresh_token';
var CLIENT_ID_KEY = 'spotify_client_id';

// Set to the full "pebblejs://close#..." link for emulator testing, or null for store builds.
// Populated automatically from .env at build time via debug_env.js (gitignored).
var DEBUG_RESPONSE = null;
try {
  DEBUG_RESPONSE = require('./debug_env');
} catch (e) {}

var refreshInProgress = false;
var refreshQueue = [];

function saveToken(token, expiresIn, clientId, refreshToken) {
  var expiryTime = Date.now() + (expiresIn * 1000);
  localStorage.setItem(TOKEN_KEY, token);
  localStorage.setItem(EXPIRY_KEY, String(expiryTime));
  if (clientId) {
    localStorage.setItem(CLIENT_ID_KEY, clientId);
  }
  if (refreshToken) {
    localStorage.setItem(REFRESH_KEY, refreshToken);
  }
}

// Handle debug response override
if (DEBUG_RESPONSE) {
  try {
    var decoded = decodeURIComponent(DEBUG_RESPONSE.split('#')[1]);
    var data = JSON.parse(decoded);
    if (data.token) {
      saveToken(data.token, data.expiresIn || 3600, data.clientId, data.refreshToken);
      console.log('[playback] Debug token loaded');
    }
  } catch (e) {
    console.log('[playback] Debug parse error: ' + e.message);
  }
}

function getToken() {
  return localStorage.getItem(TOKEN_KEY);
}

function isTokenExpired() {
  if (DEBUG_RESPONSE) return false; // Prevent refresh loops in debug mode
  var expiryTime = localStorage.getItem(EXPIRY_KEY);
  if (!expiryTime) return true;
  return Date.now() > parseInt(expiryTime, 10);
}

function refreshAccessToken(callback) {
  // If a refresh is already in progress, queue this callback
  if (refreshInProgress) {
    refreshQueue.push(callback);
    return;
  }

  var refreshToken = localStorage.getItem(REFRESH_KEY);
  var clientId = localStorage.getItem(CLIENT_ID_KEY) || SPOTIFY_CLIENT_ID;

  if (!refreshToken) {
    console.log('[playback] No refresh token available');
    if (callback) callback(new Error('No refresh token'), null);
    return;
  }

  if (!clientId) {
    console.log('[playback] No client ID available');
    if (callback) callback(new Error('No client ID'), null);
    return;
  }

  refreshInProgress = true;
  console.log('[playback] Refreshing access token...');
  var xhr = new XMLHttpRequest();
  xhr.open('POST', 'https://accounts.spotify.com/api/token', true);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');

  var body = 'grant_type=refresh_token' +
             '&refresh_token=' + encodeURIComponent(refreshToken) +
             '&client_id=' + encodeURIComponent(clientId);

  xhr.onload = function() {
    refreshInProgress = false;
    var queued = refreshQueue.slice();
    refreshQueue = [];

    if (xhr.status === 200) {
      var data = JSON.parse(xhr.responseText);
      saveToken(data.access_token, data.expires_in, clientId, data.refresh_token);
      console.log('[playback] Token refreshed successfully');
      if (callback) callback(null, data.access_token);
      for (var i = 0; i < queued.length; i++) {
        if (queued[i]) queued[i](null, data.access_token);
      }
    } else {
      console.log('[playback] Refresh failed: ' + xhr.status + ' ' + xhr.responseText);
      var err = new Error('Refresh failed');
      if (callback) callback(err, null);
      for (var i = 0; i < queued.length; i++) {
        if (queued[i]) queued[i](err, null);
      }
    }
  };
  xhr.onerror = function() {
    refreshInProgress = false;
    var queued = refreshQueue.slice();
    refreshQueue = [];
    var err = new Error('Network error');
    if (callback) callback(err, null);
    for (var i = 0; i < queued.length; i++) {
      if (queued[i]) queued[i](err, null);
    }
  };
  xhr.send(body);
}

function clearToken() {
  localStorage.removeItem(TOKEN_KEY);
  localStorage.removeItem(EXPIRY_KEY);
  localStorage.removeItem(REFRESH_KEY);
}

function isAuthenticated() {
  return !!getToken();
}

// --- Login ----------------------------------------------------------------
//
// Spotify's login form resets itself inside the Pebble app's built-in
// browser, so the login is done in the phone's real browser: the settings
// page gives a link to open there, and the wearer pastes back the address
// Spotify redirects to. The one-time code in it is exchanged here.

var VERIFIER_KEY = 'pkce_verifier';
var VERIFIER_AT_KEY = 'pkce_verifier_at';
var VERIFIER_MAX_AGE_MS = 30 * 60 * 1000;

// The verifier outlives the settings page, which may be closed and reopened
// between copying the link and pasting the address.
function pendingVerifier() {
  var verifier = localStorage.getItem(VERIFIER_KEY);
  var at = parseInt(localStorage.getItem(VERIFIER_AT_KEY), 10) || 0;
  if (!verifier || Date.now() - at > VERIFIER_MAX_AGE_MS) {
    verifier = pkce.randomVerifier(64);
    localStorage.setItem(VERIFIER_KEY, verifier);
    localStorage.setItem(VERIFIER_AT_KEY, String(Date.now()));
  }
  return verifier;
}

function notify(text) {
  console.log('[playback] ' + text);
  try {
    Pebble.showSimpleNotificationOnPebble('Encore', text);
  } catch (e) {
    // The log line above is all there is.
  }
}

// The code from whatever was pasted: the whole address, or just the code.
function codeFrom(pasted) {
  var m = /[?&#]code=([^&#\s]+)/.exec(pasted);
  if (m) return decodeURIComponent(m[1]);
  return /^[A-Za-z0-9_\-]{40,}$/.test(pasted) ? pasted : null;
}

function exchangeCode(clientId, code, done, attempt) {
  attempt = attempt || 1;
  var verifier = localStorage.getItem(VERIFIER_KEY);
  if (!verifier) {
    done('copy a new login link and start again');
    return;
  }
  var again = function (why) {
    if (attempt < 3) {
      setTimeout(function () { exchangeCode(clientId, code, done, attempt + 1); }, 2000);
    } else {
      done(why);
    }
  };
  var xhr = new XMLHttpRequest();
  xhr.open('POST', 'https://accounts.spotify.com/api/token', true);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  xhr.timeout = 20000;
  xhr.onload = function () {
    var data = null;
    try {
      data = JSON.parse(xhr.responseText);
    } catch (e) {
      data = null;
    }
    if (xhr.status === 200 && data && data.access_token) {
      saveToken(data.access_token, data.expires_in || 3600, clientId, data.refresh_token);
      localStorage.removeItem(VERIFIER_KEY);
      done(null);
    } else if (data && data.error === 'invalid_grant') {
      // The code was used already, is too old, or belongs to another link.
      done('that address was already used or is too old; copy a new login link and start again');
    } else if (data && data.error === 'invalid_client') {
      done('Spotify does not know that Client ID');
    } else {
      done('Spotify said ' + xhr.status + (data && data.error ? ' ' + data.error : ''));
    }
  };
  xhr.onerror = function () { again('network error'); };
  xhr.ontimeout = function () { again('timed out'); };
  xhr.send('grant_type=authorization_code' +
           '&code=' + encodeURIComponent(code) +
           '&redirect_uri=' + encodeURIComponent(SPOTIFY_REDIRECT_URI) +
           '&client_id=' + encodeURIComponent(clientId) +
           '&code_verifier=' + encodeURIComponent(verifier));
}

function init(onAuthChange) {
  Pebble.addEventListener('showConfiguration', function() {
    var authBase = 'https://accounts.spotify.com/authorize?response_type=code' +
      '&redirect_uri=' + encodeURIComponent(SPOTIFY_REDIRECT_URI) +
      '&scope=' + encodeURIComponent(SPOTIFY_SCOPES) +
      '&show_dialog=true&code_challenge_method=S256' +
      '&code_challenge=' + pkce.challengeFor(pendingVerifier());
    Pebble.openURL(configPage.build({
      clientId: localStorage.getItem(CLIENT_ID_KEY) || '',
      loggedIn: isAuthenticated(),
      authBase: authBase,
      guideUrl: SPOTIFY_REDIRECT_URI
    }));
  });

  Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response) return;
    var data;
    try {
      data = JSON.parse(decodeURIComponent(e.response));
    } catch (ex) {
      try {
        data = JSON.parse(e.response);
      } catch (ex2) {
        console.log('[playback] Config parse error: ' + ex.message);
        return;
      }
    }
    if (data.logout) {
      clearToken();
      notify('Logged out of Spotify.');
      if (onAuthChange) onAuthChange(false);
      return;
    }
    if (data.clientId) {
      localStorage.setItem(CLIENT_ID_KEY, data.clientId);
    }
    if (data.token) {
      // A ready-made token, as the original settings page returned.
      saveToken(data.token, data.expiresIn || 3600, data.clientId, data.refreshToken);
      if (onAuthChange) onAuthChange(true);
      return;
    }
    if (!data.pasted) return;
    var code = codeFrom(data.pasted);
    if (!code) {
      notify('Login failed: no code in that address. Copy the whole address after tapping Agree.');
      return;
    }
    exchangeCode(data.clientId, code, function (err) {
      if (err) {
        notify('Login failed: ' + err + '.');
      } else {
        notify('Logged in to Spotify.');
        if (onAuthChange) onAuthChange(true);
      }
    });
  });
}

module.exports = {
  init: init,
  getToken: getToken,
  isTokenExpired: isTokenExpired,
  refreshAccessToken: refreshAccessToken,
  clearToken: clearToken,
  isAuthenticated: isAuthenticated
};
