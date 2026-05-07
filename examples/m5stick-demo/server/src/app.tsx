import { useState, useCallback, useEffect, useRef } from "react";

const DEVICE_ID_KEY = "resident.deviceId";

export default function App() {
  const [deviceId, setDeviceId] = useState<string>(() =>
    typeof window !== "undefined" ? localStorage.getItem(DEVICE_ID_KEY) ?? "" : "",
  );
  const [deviceConnected, setDeviceConnected] = useState(false);
  const [code, setCode] = useState("");
  const [lastSent, setLastSent] = useState("");
  const [sending, setSending] = useState(false);
  const wsRef = useRef<WebSocket | null>(null);

  // Persist deviceId.
  useEffect(() => {
    if (deviceId) localStorage.setItem(DEVICE_ID_KEY, deviceId);
  }, [deviceId]);

  // Open monitor WS for the current deviceId; reopen on change.
  useEffect(() => {
    if (!deviceId) {
      setDeviceConnected(false);
      return;
    }
    const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(
      `${proto}//${window.location.host}/devices/${deviceId}?monitor=1`,
    );
    wsRef.current = ws;
    ws.onmessage = (evt) => {
      try {
        const data = JSON.parse(String(evt.data));
        if (data.type === "status") setDeviceConnected(Boolean(data.deviceConnected));
        else if (data.type === "app" && typeof data.code === "string") {
          setLastSent(data.code);
        }
      } catch {
        // Ignore non-JSON / agents-SDK protocol messages.
      }
    };
    ws.onclose = () => setDeviceConnected(false);
    return () => ws.close();
  }, [deviceId]);

  const sendApp = useCallback(async () => {
    if (!code.trim() || !deviceId || sending) return;
    setSending(true);
    try {
      const res = await fetch(`/devices/${deviceId}/send`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ type: "app", code }),
      });
      if (!res.ok) {
        console.error("Push failed:", res.status, await res.text());
      }
    } catch (err) {
      console.error("Push failed:", err);
    } finally {
      setSending(false);
    }
  }, [code, deviceId, sending]);

  return (
    <div className="min-h-screen bg-gray-50 p-8">
      <div className="max-w-2xl mx-auto space-y-6">
        <h1 className="text-2xl font-bold text-gray-900">
          Resident self-hosted relay
        </h1>

        {/* Device ID input */}
        <div className="space-y-2">
          <label htmlFor="device-id" className="block text-sm font-medium text-gray-700">
            Device ID
          </label>
          <input
            id="device-id"
            type="text"
            value={deviceId}
            onChange={(e) => setDeviceId(e.target.value.trim())}
            placeholder="e.g. abc12345 (read off your device's screen)"
            className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm font-mono focus:outline-none focus:ring-2 focus:ring-blue-500"
          />
        </div>

        {/* Connection status */}
        <div className="flex items-center gap-2 text-sm text-gray-600">
          <span className="text-xl">
            {!deviceId ? "❓" : deviceConnected ? "✅" : "❌"}
          </span>
          <span className="font-mono">{deviceId || "(no device)"}</span>
          <span>
            {!deviceId
              ? "enter a device ID"
              : deviceConnected
                ? "connected"
                : "waiting for device"}
          </span>
        </div>

        {/* Code textarea */}
        <div className="space-y-2">
          <label htmlFor="app-code" className="block text-sm font-medium text-gray-700">
            Lua App Code
          </label>
          <textarea
            id="app-code"
            value={code}
            onChange={(e) => setCode(e.target.value)}
            placeholder={'function on_tick(ctx, dt_ms)\n  screen.clear(0, 0, 0)\n  screen.text(10, 10, "Hello")\n  screen.flip()\nend'}
            rows={16}
            className="w-full px-4 py-3 border border-gray-300 rounded-lg text-sm font-mono focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent resize-y"
          />
          <button
            type="button"
            onClick={sendApp}
            disabled={!code.trim() || !deviceId || sending}
            className="px-4 py-2 bg-blue-600 text-white text-sm font-medium rounded-lg hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {sending ? "Sending..." : "Send to device"}
          </button>
        </div>

        {/* Last sent */}
        {lastSent && (
          <div className="space-y-2">
            <h2 className="text-sm font-medium text-gray-700">Last sent</h2>
            <pre className="p-4 bg-gray-900 text-green-400 text-sm font-mono rounded-lg overflow-auto max-h-64">{lastSent}</pre>
          </div>
        )}

        {/* Footer */}
        <div className="text-xs text-gray-500 pt-4 border-t border-gray-200 space-y-1">
          <p>
            This is a self-hosted Resident relay. The default firmware in
            <code className="mx-1 px-1 bg-gray-100 rounded">examples/m5stick-demo/device/</code>
            connects to <code className="px-1 bg-gray-100 rounded">resident.inanimate.tech</code>.
            Change <code className="px-1 bg-gray-100 rounded">RESIDENT_HOST</code> in <code className="px-1 bg-gray-100 rounded">main.cpp</code> to point at this server's URL instead.
          </p>
          <p>
            See <code className="px-1 bg-gray-100 rounded">../send-app.sh</code> for a CLI alternative.
          </p>
        </div>
      </div>
    </div>
  );
}
