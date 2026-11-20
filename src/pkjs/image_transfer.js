var jpeg = require('jpeg-js');

function getChunkSize() {
  try {
    var platform = Pebble.getActiveWatchInfo().platform || '';
    if (platform === 'basalt' || platform === 'emery' || platform === 'gabbro') return 2000;
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
      // The Time 2 shows the cover as a small square beside the song's name.
      if (platform === 'emery') { W = 96; H = 96; }
      else if (platform === 'gabbro') { W = 260; H = 260; isRound = true; }
      else if (platform === 'chalk') { W = 180; H = 180; isRound = true; }
      else if (platform === 'aplite') { W = 96; H = 96; }
    } catch (e) { }

    var imageData = isRound ? resizeAndQuantizeFit(raw.data, raw.width, raw.height, W, H)
      : isColorPlatform() ? resizeAndQuantize(raw.data, raw.width, raw.height, W, H)
        : resizeAndDither(raw.data, raw.width, raw.height, W, H);

    // Only worth it for a cover bigger than the preview itself.
    if (slot === 0 && platform === 'emery' && W > PREVIEW_SIZE) {
      // The preview first; the full cover follows unless a newer song cut in.
      var preview = resizeAndQuantize(raw.data, raw.width, raw.height, PREVIEW_SIZE, PREVIEW_SIZE);
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

function sendImageToWatch(data, width, height, slot) {
  slot = slot || 0;
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
  sendImageFromUrl: downloadAndSend,
  sendPrefetchFromUrl: downloadPrefetch,
  dropPrefetch: dropPrefetch,
  promotePrefetch: promotePrefetch,
  getPrefetchedUri: getPrefetchedUri,
  prefetchSupported: prefetchSupported,
  // Busy with a cover that should not be interrupted. The sharpening pass
  // can be, so it does not count.
  isTransferring: function () { return transferInProgress && !sharpening; }
};
