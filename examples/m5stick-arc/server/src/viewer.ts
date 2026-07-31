/** Live viewer for the arc loop: mirror, message stream, remote intents. */
export function viewerHtml(deviceId: string): string {
  return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>arc · ${deviceId}</title>
<style>
  :root { color-scheme: dark; }
  body { font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, monospace;
         background: #101014; color: #e6e6ea; margin: 0; padding: 24px; }
  h1 { font-size: 16px; font-weight: 600; margin: 0 0 4px; }
  h1 small { color: #8a8a94; font-weight: 400; }
  #status { display: inline-block; width: 9px; height: 9px; border-radius: 50%;
            background: #555; margin-right: 8px; }
  #status.on { background: #4ade80; }
  .row { display: flex; gap: 20px; margin-top: 18px; align-items: flex-start; }
  .panel { background: #17171d; border: 1px solid #26262e; border-radius: 8px;
           padding: 14px 16px; }
  #controls button { font: inherit; background: #26262e; color: #e6e6ea;
    border: 1px solid #3a3a44; border-radius: 6px; padding: 6px 14px;
    margin: 0 8px 8px 0; cursor: pointer; }
  #controls button:hover { background: #32323c; }
  #controls button.primary { background: #2b4a8f; border-color: #3c5ca8; }
  #mirror { min-width: 260px; }
  #mirror table { border-collapse: collapse; }
  #mirror td { padding: 2px 10px 2px 0; vertical-align: top; }
  #mirror td:first-child { color: #8a8a94; }
  #pending { color: #fbbf24; display: none; }
  #log { flex: 1; max-height: 70vh; overflow-y: auto; }
  .m { padding: 3px 0; border-bottom: 1px solid #1e1e26; word-break: break-all; }
  .m .dir { display: inline-block; width: 3.5em; color: #8a8a94; }
  .m.up .dir { color: #4ade80; }
  .m.down .dir { color: #60a5fa; }
  .m .ty { color: #fbbf24; }
  .m .body { color: #b9b9c2; }
  .lat { color: #f472b6; }
</style>
</head>
<body>
<h1><span id="status"></span>arc <small>device ${deviceId}</small></h1>
<div id="controls" class="panel">
  <button class="primary" onclick="push()">Push adventure app</button>
  <button onclick="intent('choose_a')">Choose A</button>
  <button onclick="intent('choose_b')">Choose B</button>
  <button onclick="intent('remix')">Remix look (shake)</button>
  <button onclick="intent('arc:probe')">Probe</button>
  <span id="pending">thinking…</span>
</div>
<div class="row">
  <div id="mirror" class="panel">
    <div style="color:#8a8a94;margin-bottom:6px">device mirror (arc:state)</div>
    <table id="mirrorTable"><tr><td colspan="2">—</td></tr></table>
  </div>
  <div id="log" class="panel"></div>
</div>
<script>
  var deviceId = ${JSON.stringify(deviceId)};
  var base = "/devices/" + deviceId;
  var log = document.getElementById("log");
  var askStarted = {};

  function line(cls, dir, type, body, extra) {
    var div = document.createElement("div");
    div.className = "m " + cls;
    div.innerHTML = '<span class="dir">' + dir + '</span><span class="ty">' +
      type + '</span> <span class="body"></span>' + (extra || "");
    div.querySelector(".body").textContent = body;
    log.prepend(div);
    while (log.children.length > 200) log.removeChild(log.lastChild);
  }

  function showMirror(data) {
    var t = document.getElementById("mirrorTable");
    t.innerHTML = "";
    Object.keys(data).sort().forEach(function (k) {
      var tr = document.createElement("tr");
      var td1 = document.createElement("td"); td1.textContent = k;
      var td2 = document.createElement("td"); td2.textContent = data[k];
      tr.appendChild(td1); tr.appendChild(td2); t.appendChild(tr);
    });
  }

  function handle(m) {
    if (m.type === "status") {
      document.getElementById("status").className = m.deviceConnected ? "on" : "";
      return;
    }
    var dir = m.dir, msg = m.msg;
    if (!msg) { dir = "down"; msg = m; }   // raw /send echo from the relay
    var type = (msg.channel ? msg.channel + "/" : "") + (msg.type || "?");
    var body = JSON.stringify(msg.data !== undefined ? msg.data : msg);
    var extra = "";
    if (msg.type === "arc:ask" && msg.data) askStarted[msg.data.id] = Date.now();
    if (msg.type === "arc:reply" && msg.data && askStarted[msg.data.id]) {
      extra = ' <span class="lat">' + (Date.now() - askStarted[msg.data.id]) + 'ms</span>';
      delete askStarted[msg.data.id];
    }
    document.getElementById("pending").style.display =
      Object.keys(askStarted).length ? "inline" : "none";
    if (msg.channel === "app" && msg.type === "arc:state") showMirror(msg.data || {});
    line(dir === "up" ? "up" : "down", dir === "up" ? "dev →" : "→ dev", type, body, extra);
  }

  function connect() {
    var ws = new WebSocket(
      (location.protocol === "https:" ? "wss://" : "ws://") + location.host +
      base + "?monitor=1");
    ws.onmessage = function (e) {
      if (typeof e.data !== "string") return;
      try { handle(JSON.parse(e.data)); } catch (_) {}
    };
    ws.onclose = function () { setTimeout(connect, 2000); };
  }
  connect();

  function push() {
    fetch(base + "/arc/push", { method: "POST" })
      .then(function (r) { return r.text(); })
      .then(function (t) { line("down", "→ dev", "arc/push", t); });
  }
  function intent(name) {
    fetch(base + "/send", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ channel: "app", type: name, from: "viewer",
                             nonce: Math.random().toString(36).slice(2, 10) }),
    });
  }
</script>
</body>
</html>`
}
