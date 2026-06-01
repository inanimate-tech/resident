export function viewerHtml(deviceId: string): string {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>m5stick-voice — ${deviceId}</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; background:#0b0b10; color:#e6e6f0;
         font:16px/1.5 system-ui, sans-serif; display:flex;
         flex-direction:column; height:100vh; }
  header { padding:10px 16px; border-bottom:1px solid #23232c; }
  h1 { font-size:14px; margin:0; opacity:.7; font-weight:600; }
  #status { font-size:12px; opacity:.5; margin-top:2px; }
  #transcript { flex:1; overflow-y:auto; padding:16px; }
  .line { margin:0 0 10px; white-space:pre-wrap; }
  .interim { opacity:.5; }
  #fft-wrap { height:120px; border-top:1px solid #23232c; }
  canvas { width:100%; height:100%; display:block; }
</style>
</head>
<body>
<header>
  <h1>m5stick-voice · ${deviceId}</h1>
  <div id="status">connecting…</div>
</header>
<div id="transcript"></div>
<div id="fft-wrap"><canvas id="c"></canvas></div>
<script>
(function () {
  var N = 512, NUM_BARS = 48;
  var statusEl = document.getElementById("status");
  var transcriptEl = document.getElementById("transcript");
  var canvas = document.getElementById("c");
  var ctx = canvas.getContext("2d");

  // ---- transcript ----
  var items = {};
  function ensureItem(id) {
    if (!id) id = "_cur";
    if (!items[id]) {
      var el = document.createElement("div");
      el.className = "line interim";
      transcriptEl.appendChild(el);
      items[id] = { el: el, text: "" };
    }
    return items[id];
  }
  function atBottom() {
    return transcriptEl.scrollHeight - transcriptEl.scrollTop - transcriptEl.clientHeight < 40;
  }
  function scroll(was) { if (was) transcriptEl.scrollTop = transcriptEl.scrollHeight; }
  function onDelta(m) {
    var was = atBottom(), it = ensureItem(m.itemId);
    it.text += (m.text || "");
    it.el.textContent = it.text;
    scroll(was);
  }
  function onCompleted(m) {
    var was = atBottom(), it = ensureItem(m.itemId);
    if (m.text) it.text = m.text;
    it.el.textContent = it.text;
    it.el.className = "line";
    if (m.itemId) { /* keep keyed */ } else { delete items["_cur"]; }
    scroll(was);
  }
  function onJson(s) {
    var m; try { m = JSON.parse(s); } catch (e) { return; }
    if (m.type === "status") {
      statusEl.textContent = m.deviceConnected ? "device connected" : "waiting for device…";
    } else if (m.type === "transcript.delta") {
      onDelta(m);
    } else if (m.type === "transcript.completed") {
      onCompleted(m);
    }
  }

  // ---- FFT (from the courier binary-websocket example) ----
  var win = new Float32Array(N);
  for (var i = 0; i < N; i++) win[i] = 0.5 * (1 - Math.cos(2 * Math.PI * i / (N - 1)));
  var re = new Float32Array(N), im = new Float32Array(N);
  var mags = new Float32Array(NUM_BARS), bars = new Float32Array(NUM_BARS);
  var ranges = [], half = N / 2, minBin = 1, maxBin = half, prev = minBin;
  for (var b = 0; b < NUM_BARS; b++) {
    var hi = Math.round(minBin * Math.pow(maxBin / minBin, (b + 1) / NUM_BARS));
    if (hi <= prev) hi = prev + 1;
    if (hi > half) hi = half;
    ranges.push([prev, hi]); prev = hi;
  }
  function fft(re, im) {
    var n = re.length, j = 0, i, bit, len, k, ang, wr, wi, cr, ci;
    var ur, ui, vr, vi, ncr, tr, ti, hlen;
    for (i = 1; i < n; i++) {
      bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { tr = re[i]; re[i] = re[j]; re[j] = tr;
                   ti = im[i]; im[i] = im[j]; im[j] = ti; }
    }
    for (len = 2; len <= n; len <<= 1) {
      hlen = len >> 1; ang = -2 * Math.PI / len;
      wr = Math.cos(ang); wi = Math.sin(ang);
      for (i = 0; i < n; i += len) {
        cr = 1; ci = 0;
        for (k = 0; k < hlen; k++) {
          ur = re[i + k]; ui = im[i + k];
          vr = re[i + k + hlen] * cr - im[i + k + hlen] * ci;
          vi = re[i + k + hlen] * ci + im[i + k + hlen] * cr;
          re[i + k] = ur + vr; im[i + k] = ui + vi;
          re[i + k + hlen] = ur - vr; im[i + k + hlen] = ui - vi;
          ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
        }
      }
    }
  }
  function onFrame(buf) {
    var pcm = new Int16Array(buf);
    if (pcm.length < N) return;
    for (var i = 0; i < N; i++) { re[i] = (pcm[i] / 32768) * win[i]; im[i] = 0; }
    fft(re, im);
    var scale = N / 4;
    for (var b = 0; b < NUM_BARS; b++) {
      var lo = ranges[b][0], hi = ranges[b][1], peak = 0;
      for (var k = lo; k < hi; k++) {
        var mg = Math.sqrt(re[k] * re[k] + im[k] * im[k]);
        if (mg > peak) peak = mg;
      }
      var v = peak / scale, db = 20 * Math.log10(v + 1e-9), norm = (db + 60) / 60;
      mags[b] = norm < 0 ? 0 : (norm > 1 ? 1 : norm);
    }
  }
  function resize() {
    var dpr = window.devicePixelRatio || 1;
    canvas.width = canvas.clientWidth * dpr;
    canvas.height = canvas.clientHeight * dpr;
  }
  window.addEventListener("resize", resize); resize();
  function draw() {
    var w = canvas.width, h = canvas.height, dpr = window.devicePixelRatio || 1;
    ctx.clearRect(0, 0, w, h);
    var gap = 2 * dpr, bw = (w - gap * (NUM_BARS + 1)) / NUM_BARS;
    for (var b = 0; b < NUM_BARS; b++) {
      var target = mags[b];
      bars[b] += (target - bars[b]) * (target > bars[b] ? 0.6 : 0.12);
      var bh = bars[b] * h, x = gap + b * (bw + gap), hue = 200 - 160 * (b / NUM_BARS);
      ctx.fillStyle = "hsl(" + hue + ",80%," + (40 + 30 * bars[b]) + "%)";
      ctx.fillRect(x, h - bh, bw, bh);
    }
    requestAnimationFrame(draw);
  }
  requestAnimationFrame(draw);

  // ---- monitor WebSocket ----
  function connect() {
    var proto = location.protocol === "https:" ? "wss:" : "ws:";
    var ws = new WebSocket(proto + "//" + location.host + "/devices/${deviceId}?monitor=1");
    ws.binaryType = "arraybuffer";
    ws.onopen = function () { statusEl.textContent = "connected"; };
    ws.onmessage = function (ev) {
      if (typeof ev.data === "string") onJson(ev.data);
      else onFrame(ev.data);
    };
    ws.onclose = function () {
      statusEl.textContent = "disconnected — reconnecting…";
      setTimeout(connect, 1000);
    };
    ws.onerror = function () { try { ws.close(); } catch (e) {} };
  }
  connect();
})();
</script>
</body>
</html>`;
}
