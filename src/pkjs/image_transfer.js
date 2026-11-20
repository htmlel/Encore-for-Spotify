var jpeg = require('jpeg-js');

function getChunkSize() {
  try {
    var platform = Pebble.getActiveWatchInfo().platform || '';
    // The Time 2 takes messages of up to about 8 KB; fewer, bigger pieces
    // spend less time waiting on each one to be acknowledged.
    if (platform === 'emery') return 4000;
    if (platform === 'basalt' || platform === 'gabbro') return 2000;
  } catch (e) { }
  return 1000;
}

// Slot 0 — currently displayed art
var transferInProgress = false;
var lastUrl = null;

// A large cover goes over twice: a small preview that shows within a second
// or two, then the full cover to sharpen it. While the full cover is still
// on its way, a newer song may cut in. (The Time 2's cover is small enough
// now to go in one pass; this stays for any screen that shows a big one.)
var PREVIEW_SIZE = 100;
var sharpening = false;     // the full cover is being sent after its preview
var slot0Abort = false;     // stop sending it; `nextUrl` is waiting
var nextUrl = null;
var afterSlot0 = null;      // what to do when the running slot-0 send is done

// The cover size the Time 2 wants, a side in pixels: small for the card look,
// screen-wide for the full-cover look. The watch sends it with each command.
var emerySide = 96;

// True if the size changed, in which case covers already sent are forgotten.
function setArtSize(side) {
  side = Number(side);
  if (!(side >= 32 && side <= 200) || side === emerySide) return false;
  emerySide = side;
  lastUrl = null;
  dropPrefetch();
  return true;
}

// Slot 1 — prefetched next-track art (emery/gabbro only)
var prefetchInProgress = false;
var prefetchAbort = false;
var prefetchUrl = null;
var prefetchTrackUri = null;
var prefetchReady = false;

function isColorPlatform() {
  try {
    var platform = Pebble.getActiveWatchInfo().platform || '';
    return ['basalt', 'chalk', 'emery', 'gabbro'].indexOf(platform) !== -1;
  } catch (e) {
    return true;
  }
}

function prefetchSupported() {
  try {
    var p = Pebble.getActiveWatchInfo().platform || '';
    return p === 'emery' || p === 'gabbro';
  } catch (e) { return false; }
}

function sendError(msg) {
  console.log('[playback] ERROR: ' + msg);
  try {
    Pebble.sendAppMessage({ 'ErrorMsg': msg });
  } catch (e) { }
}

function arrayBufferToBytes(ab) {
  return new Uint8Array(ab);
}

function resizeAndQuantize(srcPixels, srcW, srcH, dstW, dstH) {
  var dst = new Uint8Array(dstW * dstH);
  var scale = Math.max(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale, scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2, offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);
      var r = 0, g = 0, b = 0;
      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        r = srcPixels[srcIdx]; g = srcPixels[srcIdx + 1]; b = srcPixels[srcIdx + 2];
      }
      dst[y * dstW + x] = 0xC0 | ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
    }
  }
  return dst;
}

// --- The Time 2's cover, at the best quality its 64 colors allow -------------

// Shrinks the picture to dstW x dstH (filling it, cropping the overhang) by
// averaging every source pixel that falls inside each new pixel, so no
// detail is skipped over. Returns red, green, blue for each pixel, 0..255.
function resizeSmooth(srcPixels, srcW, srcH, dstW, dstH) {
  var out = new Float32Array(dstW * dstH * 3);
  var scale = Math.max(dstW / srcW, dstH / srcH);
  var offsetX = (srcW - dstW / scale) / 2, offsetY = (srcH - dstH / scale) / 2;
  var step = 1 / scale;  // source pixels per new pixel
  for (var y = 0; y < dstH; y++) {
    var y0 = offsetY + y * step, y1 = y0 + step;
    var sy0 = Math.max(0, Math.floor(y0)), sy1 = Math.min(srcH, Math.ceil(y1));
    for (var x = 0; x < dstW; x++) {
      var x0 = offsetX + x * step, x1 = x0 + step;
      var sx0 = Math.max(0, Math.floor(x0)), sx1 = Math.min(srcW, Math.ceil(x1));
      var r = 0, g = 0, b = 0, weight = 0;
      for (var sy = sy0; sy < sy1; sy++) {
        // How much of this source row and column lies inside the new pixel.
        var wy = Math.min(sy + 1, y1) - Math.max(sy, y0);
        for (var sx = sx0; sx < sx1; sx++) {
          var w = wy * (Math.min(sx + 1, x1) - Math.max(sx, x0));
          var i = (sy * srcW + sx) * 4;
          r += srcPixels[i] * w; g += srcPixels[i + 1] * w; b += srcPixels[i + 2] * w;
          weight += w;
        }
      }
      var o = (y * dstW + x) * 3;
      if (weight > 0) { out[o] = r / weight; out[o + 1] = g / weight; out[o + 2] = b / weight; }
    }
  }
  return out;
}

// Each channel has four levels on the watch: 0, 85, 170, 255.
function nearestLevel(v) {
  var level = Math.round(v / 85);
  return level < 0 ? 0 : level > 3 ? 3 : level;
}

// To the watch's colors, each pixel to its nearest. Plain areas stay plain,
// which packs small: used for the quick preview.
function quantizeNearest(rgb, w, h) {
  var dst = new Uint8Array(w * h);
  for (var i = 0; i < w * h; i++) {
    dst[i] = 0xC0 | (nearestLevel(rgb[i * 3]) << 4) | (nearestLevel(rgb[i * 3 + 1]) << 2) |
      nearestLevel(rgb[i * 3 + 2]);
  }
  return dst;
}

// To the watch's colors with the rounding error of each pixel handed on to
// its neighbors (Floyd-Steinberg, back and forth on alternate rows), so
// shades between the watch's 64 colors come out as a fine mix of the two
// nearest: skin, skies and soft gradients keep their shape and do not break
// into bands. Used for the full cover.
function quantizeDithered(rgb, w, h) {
  var dst = new Uint8Array(w * h);
  var buf = new Float32Array(rgb);  // a copy; errors are added into it
  for (var y = 0; y < h; y++) {
    var leftToRight = (y % 2) === 0;
    var dir = leftToRight ? 1 : -1;
    for (var n = 0; n < w; n++) {
      var x = leftToRight ? n : w - 1 - n;
      var o = (y * w + x) * 3;
      var px = 0xC0;
      for (var c = 0; c < 3; c++) {
        var want = buf[o + c];
        var level = nearestLevel(want);
        var err = want - level * 85;
        px |= level << (4 - 2 * c);
        var ahead = x + dir, behind = x - dir;
        if (ahead >= 0 && ahead < w) buf[o + dir * 3 + c] += err * 7 / 16;
        if (y + 1 < h) {
          var below = o + w * 3;
          buf[below + c] += err * 5 / 16;
          if (behind >= 0 && behind < w) buf[below - dir * 3 + c] += err * 3 / 16;
          if (ahead >= 0 && ahead < w) buf[below + dir * 3 + c] += err * 1 / 16;
        }
      }
      dst[y * w + x] = px;
    }
  }
  return dst;
}

function resizeAndDither(srcPixels, srcW, srcH, dstW, dstH) {
  var gray = new Float32Array(dstW * dstH);
  var scale = Math.max(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale, scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2, offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);
      var lum = 0;
      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        lum = 0.299 * srcPixels[srcIdx] + 0.587 * srcPixels[srcIdx + 1] + 0.114 * srcPixels[srcIdx + 2];
      }
      gray[y * dstW + x] = lum;
    }
  }

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var idx = y * dstW + x;
      var old = gray[idx];
      var nw = old < 128 ? 0 : 255;
      gray[idx] = nw;
      var err = old - nw;
      if (x + 1 < dstW) gray[idx + 1] += err * 7 / 16;
      if (y + 1 < dstH) {
        if (x > 0) gray[(y + 1) * dstW + (x - 1)] += err * 3 / 16;
        gray[(y + 1) * dstW + x] += err * 5 / 16;
        if (x + 1 < dstW) gray[(y + 1) * dstW + (x + 1)] += err * 1 / 16;
      }
    }
  }

  var rowBytes = Math.ceil(dstW / 32) * 4;
  var dst = new Uint8Array(rowBytes * dstH);
  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      if (gray[y * dstW + x] <= 128) {
        dst[y * rowBytes + Math.floor(x / 8)] |= (1 << (x % 8));
      }
    }
  }
  return dst;
}

function resizeAndQuantizeFit(srcPixels, srcW, srcH, dstW, dstH) {
  var dst = new Uint8Array(dstW * dstH);
  for (var i = 0; i < dst.length; i++) dst[i] = 0xC0;
  var scale = Math.min(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale, scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2, offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);
      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        var r = srcPixels[srcIdx], g = srcPixels[srcIdx + 1], b = srcPixels[srcIdx + 2];
        dst[y * dstW + x] = 0xC0 | ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
      }
    }
  }
  return dst;
}

function resetPrefetch() {
  prefetchInProgress = false;
  prefetchAbort = false;
  prefetchUrl = null;
  prefetchTrackUri = null;
  prefetchReady = false;
}

function processJpeg(jpegData, slot) {
  try {
    var raw = jpeg.decode(jpegData, { useTArray: true });
    var W = 144, H = 106, isRound = false;
    var platform = '';
    try {
      platform = Pebble.getActiveWatchInfo().platform || '';
      // The Time 2 asks for the size its chosen Now Playing look uses.
      if (platform === 'emery') { W = emerySide; H = emerySide; }
      else if (platform === 'gabbro') { W = 260; H = 260; isRound = true; }
      else if (platform === 'chalk') { W = 180; H = 180; isRound = true; }
      else if (platform === 'aplite') { W = 96; H = 96; }
    } catch (e) { }

    var imageData = isRound ? resizeAndQuantizeFit(raw.data, raw.width, raw.height, W, H)
      : platform === 'emery' ? quantizeDithered(resizeSmooth(raw.data, raw.width, raw.height, W, H), W, H)
        : isColorPlatform() ? resizeAndQuantize(raw.data, raw.width, raw.height, W, H)
          : resizeAndDither(raw.data, raw.width, raw.height, W, H);

    // Only worth it for a cover bigger than the preview itself.
    if (slot === 0 && platform === 'emery' && W > PREVIEW_SIZE) {
      // The preview first; the full cover follows unless a newer song cut in.
      var preview = quantizeNearest(
        resizeSmooth(raw.data, raw.width, raw.height, PREVIEW_SIZE, PREVIEW_SIZE),
        PREVIEW_SIZE, PREVIEW_SIZE);
      var url = lastUrl;
      afterSlot0 = function () {
        if (lastUrl !== url) return;
        sharpening = true;
        afterSlot0 = function () { sharpening = false; };
        sendImageToWatch(imageData, W, H, 0);
      };
      sendImageToWatch(preview, PREVIEW_SIZE, PREVIEW_SIZE, 0);
      return;
    }
    sendImageToWatch(imageData, W, H, slot);
  } catch (ex) {
    if (slot === 1) {
      resetPrefetch();
    } else {
      lastUrl = null;
      sendError('Decode: ' + ex.message);
    }
  }
}

function downloadAndSend(url) {
  if (url === lastUrl) return;
  if (transferInProgress) {
    // A sharpening pass gives way to a newer cover; a preview is short enough
    // to let finish.
    if (sharpening) {
      slot0Abort = true;
      nextUrl = url;
    }
    return;
  }
  // Current art always wins — abort any in-flight prefetch.
  if (prefetchInProgress) prefetchAbort = true;
  lastUrl = url;
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.responseType = 'arraybuffer';
  xhr.onload = function () {
    if (xhr.status === 200) {
      processJpeg(arrayBufferToBytes(xhr.response), 0);
    } else {
      lastUrl = null;
      sendError('Download ' + xhr.status);
    }
  };
  xhr.onerror = function () {
    lastUrl = null;
    sendError('Download failed');
  };
  xhr.send();
}

function downloadPrefetch(url, trackUri) {
  if (!prefetchSupported()) return;
  if (transferInProgress) return;
  if (prefetchInProgress || prefetchReady) {
    if (url === prefetchUrl) return;
    // A stale prefetch is in flight or buffered; clear it before queuing the new one.
    if (prefetchInProgress) { prefetchAbort = true; return; }
    resetPrefetch();
  }
  if (url === lastUrl) return;

  prefetchInProgress = true;
  prefetchAbort = false;
  prefetchReady = false;
  prefetchUrl = url;
  prefetchTrackUri = trackUri;

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.responseType = 'arraybuffer';
  xhr.onload = function () {
    if (prefetchAbort || xhr.status !== 200) {
      resetPrefetch();
      return;
    }
    processJpeg(arrayBufferToBytes(xhr.response), 1);
  };
  xhr.onerror = function () { resetPrefetch(); };
  xhr.send();
}

// Packs a color image for the trip to the watch. Covers have long runs of
// one color, so a run of 3 or more identical pixels is sent as two bytes: a
// count byte (the run's length minus 3, at most 191) and the pixel. Every
// pixel byte has its top two bits set and no count byte does, which is how
// the watch tells them apart. Never comes out longer than it went in.
function packRuns(data) {
  var out = [];
  var i = 0;
  var n = data.length;
  while (i < n) {
    var px = data[i];
    var run = 1;
    while (i + run < n && data[i + run] === px && run < 194) run++;
    if (run >= 3) {
      out.push(run - 3, px);
    } else {
      for (var k = 0; k < run; k++) out.push(px);
    }
    i += run;
  }
  return out;
}

function canPack() {
  try {
    return (Pebble.getActiveWatchInfo().platform || '') === 'emery';
  } catch (e) { }
  return false;
}

function sendImageToWatch(data, width, height, slot) {
  slot = slot || 0;
  var packed = canPack();
  if (packed) data = packRuns(data);
  if (slot === 1) prefetchInProgress = true;
  else transferInProgress = true;

  var chunkSize = getChunkSize();
  var totalChunks = Math.ceil(data.length / chunkSize);

  var headerMsg = {
    'ImageWidth': width,
    'ImageHeight': height,
    'ImageDataSize': data.length,
    'ImageChunksTotal': totalChunks
  };
  if (slot === 1) headerMsg['ImageSlot'] = 1;
  if (packed) headerMsg['ImagePacked'] = 1;

  Pebble.sendAppMessage(headerMsg, function () {
    sendChunk(data, 0, totalChunks, chunkSize, slot);
  }, function () {
    if (slot === 1) {
      resetPrefetch();
    } else {
      transferInProgress = false;
      lastUrl = null;
      sendError('Header fail');
    }
  });
}

function finishChunkRun(slot) {
  if (slot === 1) {
    prefetchInProgress = false;
    prefetchAbort = false;
    prefetchReady = true;
  } else {
    transferInProgress = false;
    var then = afterSlot0;
    afterSlot0 = null;
    if (then) then();
  }
}

function failChunkRun(slot) {
  if (slot === 1) {
    resetPrefetch();
  } else {
    transferInProgress = false;
    sharpening = false;
    afterSlot0 = null;
    lastUrl = null;
    sendError('Chunk fail');
  }
}

// Stops the sharpening pass for a newer cover. True if it did.
function giveWayToNext() {
  if (!slot0Abort) return false;
  slot0Abort = false;
  sharpening = false;
  afterSlot0 = null;
  transferInProgress = false;
  var url = nextUrl;
  nextUrl = null;
  lastUrl = null;
  if (url) downloadAndSend(url);
  return true;
}

function sendChunk(data, index, totalChunks, chunkSize, slot) {
  if (slot === 1 && prefetchAbort) { resetPrefetch(); return; }
  if (slot !== 1 && giveWayToNext()) return;

  var start = index * chunkSize;
  var end = Math.min(start + chunkSize, data.length);
  var chunk = [];
  for (var i = start; i < end; i++) chunk.push(data[i]);

  var msg = { 'ImageChunkIndex': index, 'ImageChunkData': chunk };
  if (slot === 1) msg['ImageSlot'] = 1;

  Pebble.sendAppMessage(msg, function () {
    if (slot === 1 && prefetchAbort) { resetPrefetch(); return; }
    if (index + 1 < totalChunks) sendChunk(data, index + 1, totalChunks, chunkSize, slot);
    else finishChunkRun(slot);
  }, function () {
    setTimeout(function () {
      if (slot === 1 && prefetchAbort) { resetPrefetch(); return; }
      Pebble.sendAppMessage(msg, function () {
        if (slot === 1 && prefetchAbort) { resetPrefetch(); return; }
        if (index + 1 < totalChunks) sendChunk(data, index + 1, totalChunks, chunkSize, slot);
        else finishChunkRun(slot);
      }, function () {
        failChunkRun(slot);
      });
    }, 500);
  });
}

function dropPrefetch() {
  if (prefetchInProgress) {
    prefetchAbort = true;
  } else {
    resetPrefetch();
  }
}

function promotePrefetch() {
  if (!prefetchReady) return false;
  var promotedUrl = prefetchUrl;
  Pebble.sendAppMessage({ 'ImagePromote': 1 }, function () {
    lastUrl = promotedUrl;
    resetPrefetch();
  }, function () {
    // Promote failed in transit — fall back to a fresh transfer.
    var url = prefetchUrl;
    resetPrefetch();
    lastUrl = null;
    if (url) downloadAndSend(url);
  });
  return true;
}

function getPrefetchedUri() {
  return prefetchReady ? prefetchTrackUri : null;
}

module.exports = {
  packRuns: packRuns,
  sendImageFromUrl: downloadAndSend,
  sendPrefetchFromUrl: downloadPrefetch,
  dropPrefetch: dropPrefetch,
  promotePrefetch: promotePrefetch,
  getPrefetchedUri: getPrefetchedUri,
  prefetchSupported: prefetchSupported,
  // Busy with a cover that should not be interrupted. The sharpening pass
  // can be, so it does not count.
  isTransferring: function () { return transferInProgress && !sharpening; },
  setArtSize: setArtSize
};
