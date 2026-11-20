// The settings page, built here and opened as a data: URL, so nothing has to
// be hosted.
//
// Spotify's login form does not work inside the Pebble app's built-in
// browser (it resets itself), so the login happens in the phone's real
// browser instead: the page hands out a login link to open there, and takes
// back the address Spotify sends the browser to afterwards. That address
// carries the one-time code this app trades for its tokens.

function escapeHtml(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// opts: clientId, loggedIn, authBase (the login link without client_id),
// guideUrl (the setup guide, which is also where Spotify redirects to).
function build(opts) {
  var html = '<!DOCTYPE html><html><head><title>Encore</title>' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<style>' +
    'body{background:#121212;color:#fff;font-family:sans-serif;padding:20px;line-height:1.4}' +
    'h2{font-size:18px;margin:24px 0 6px}' +
    'p{font-size:15px;color:#ccc;margin:6px 0}' +
    'input,textarea{width:100%;padding:12px;border-radius:6px;border:1px solid #444;' +
    'background:#222;color:#fff;box-sizing:border-box;margin-top:8px;font-size:14px}' +
    'textarea{height:90px;word-break:break-all}' +
    'button{display:block;width:100%;background:#1DB954;color:#fff;padding:14px;font-size:17px;' +
    'border-radius:24px;margin-top:12px;border:none}' +
    'button.plain{background:#333}' +
    '.box{border:1px solid #333;border-radius:8px;padding:14px;margin-top:14px}' +
    '.state{font-weight:bold;color:#1DB954}.warn{color:#ffb74d}' +
    'a{color:#9be7b4;font-weight:bold}' +
    '</style></head><body>' +
    '<p class="state">' + (opts.loggedIn ? 'Connected to Spotify.' : 'Not connected yet.') + '</p>' +
    '<div class="box"><h2 style="margin-top:0">Spotify Client ID</h2>' +
    '<p>First time? <a href="' + escapeHtml(opts.guideUrl) + '">Setup guide</a></p>' +
    '<input type="text" id="cid" autocapitalize="none" autocorrect="off" ' +
    'placeholder="Paste your Client ID here" value="' + escapeHtml(opts.clientId || '') + '"></div>' +

    '<div class="box"><h2 style="margin-top:0">1. Copy the login link</h2>' +
    '<button id="copy">Copy login link</button>' +
    '<p id="copied"></p>' +
    '<textarea id="link" readonly placeholder="The link appears here"></textarea></div>' +

    '<div class="box"><h2 style="margin-top:0">2. Log in with your browser</h2>' +
    '<p>Open Chrome (or your usual browser), paste the link into the address bar and go. ' +
    'Log in to Spotify if it asks, then tap Agree.</p>' +
    '<p>You will land on a page that says <b>"Almost done"</b>. Tap its <b>Copy</b> button, ' +
    'then come back here.</p></div>' +

    '<div class="box"><h2 style="margin-top:0">3. Paste the address here</h2>' +
    '<textarea id="back" autocapitalize="none" autocorrect="off" ' +
    'placeholder="https://...?code=..."></textarea>' +
    '<p id="err" class="warn"></p>' +
    '<button id="finish">Finish</button></div>' +

    '<button class="plain" id="logout">Log out of Spotify</button>' +

    '<script>' +
    'var AUTH_BASE=' + JSON.stringify(opts.authBase) + ';' +
    'function $(id){return document.getElementById(id);}' +
    'function close(data){location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(data));}' +
    '$("copy").onclick=function(){' +
    'var cid=$("cid").value.trim();' +
    'if(!cid){$("copied").textContent="Enter your Client ID first.";return;}' +
    'var link=AUTH_BASE+"&client_id="+encodeURIComponent(cid);' +
    '$("link").value=link;$("link").focus();$("link").select();' +
    'var ok=false;try{ok=document.execCommand("copy");}catch(e){ok=false;}' +
    '$("copied").textContent=ok?"Copied. Now switch to your browser.":' +
    '"Press and hold the link below to copy it.";};' +
    '$("finish").onclick=function(){' +
    'var cid=$("cid").value.trim();var back=$("back").value.trim();' +
    'if(!cid){$("err").textContent="Enter your Client ID first.";return;}' +
    'if(back.indexOf("error=")>=0){$("err").textContent="Spotify refused the login: "+back;return;}' +
    'if(!back){$("err").textContent="Paste the address from your browser first.";return;}' +
    'close({clientId:cid,pasted:back});};' +
    '$("logout").onclick=function(){close({logout:true});};' +
    '</script></body></html>';
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

module.exports = { build: build };
