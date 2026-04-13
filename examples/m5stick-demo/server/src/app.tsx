import { useState, useCallback } from "react";
import { useAgent } from "agents/react";

const DEVICE_ID = "m5stick-demo";
const AGENT_URL = `/agents/device-agent/${DEVICE_ID}`;

export default function App() {
  const [deviceConnected, setDeviceConnected] = useState(false);
  const [code, setCode] = useState("");
  const [lastSent, setLastSent] = useState("");
  const [sending, setSending] = useState(false);

  useAgent({
    agent: "DeviceAgent",
    name: DEVICE_ID,
    query: { monitor: "1" },
    onMessage: useCallback((event: MessageEvent) => {
      try {
        const data = JSON.parse(String(event.data));
        if (data.type === "status") {
          setDeviceConnected(data.deviceConnected);
        } else if (data.type === "app") {
          setLastSent(data.code);
        }
      } catch {
        // Not JSON or protocol message
      }
    }, [])
  });

  const sendApp = useCallback(async () => {
    if (!code.trim() || sending) return;
    setSending(true);
    try {
      const res = await fetch(AGENT_URL, {
        method: "POST",
        headers: { "Content-Type": "text/plain" },
        body: code
      });
      if (!res.ok) {
        console.error("Failed to send app:", res.status);
      }
    } catch (err) {
      console.error("Failed to send app:", err);
    } finally {
      setSending(false);
    }
  }, [code, sending]);

  return (
    <div className="min-h-screen bg-gray-50 p-8">
      <div className="max-w-2xl mx-auto space-y-6">
        <h1 className="text-2xl font-bold text-gray-900">
          Outrun App Relay
        </h1>

        {/* Device status */}
        <div className="flex items-center gap-2 text-sm text-gray-600">
          <span className="text-xl">{deviceConnected ? "\u2705" : "\u274C"}</span>
          <span className="font-mono">{DEVICE_ID}</span>
          <span>{deviceConnected ? "connected" : "waiting for device"}</span>
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
            disabled={!code.trim() || sending}
            className="px-4 py-2 bg-blue-600 text-white text-sm font-medium rounded-lg hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {sending ? "Sending..." : "Send to Device"}
          </button>
        </div>

        {/* Last sent */}
        {lastSent && (
          <div className="space-y-2">
            <h2 className="text-sm font-medium text-gray-700">Last Sent</h2>
            <pre className="p-4 bg-gray-900 text-green-400 text-sm font-mono rounded-lg overflow-auto max-h-64">{lastSent}</pre>
          </div>
        )}
      </div>
    </div>
  );
}
