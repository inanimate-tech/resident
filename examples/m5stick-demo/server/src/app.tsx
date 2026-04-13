import { useState, useCallback } from "react";
import { useAgent } from "agents/react";

function DeviceMonitor({
  deviceId,
  onStatus,
  onApp
}: {
  deviceId: string;
  onStatus: (connected: boolean) => void;
  onApp: (code: string) => void;
}) {
  useAgent({
    agent: "DeviceAgent",
    name: deviceId,
    query: { monitor: "1" },
    onOpen: useCallback(() => onStatus(false), [onStatus]),
    onClose: useCallback(() => onStatus(false), [onStatus]),
    onMessage: useCallback(
      (event: MessageEvent) => {
        try {
          const data = JSON.parse(String(event.data));
          if (data.type === "status") {
            onStatus(data.deviceConnected);
          } else if (data.type === "app") {
            onApp(data.code);
          }
        } catch {
          // Not JSON or protocol message
        }
      },
      [onStatus, onApp]
    )
  });

  return null;
}

export default function App() {
  const [deviceId, setDeviceId] = useState("");
  const [deviceConnected, setDeviceConnected] = useState(false);
  const [code, setCode] = useState("");
  const [lastSent, setLastSent] = useState("");
  const [sending, setSending] = useState(false);

  const handleStatus = useCallback((connected: boolean) => {
    setDeviceConnected(connected);
  }, []);

  const handleApp = useCallback((appCode: string) => {
    setLastSent(appCode);
  }, []);

  const sendApp = useCallback(async () => {
    if (!code.trim() || !deviceId || sending) return;
    setSending(true);
    try {
      const url = `/agents/device-agent/${deviceId}`;
      const res = await fetch(url, {
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
  }, [code, deviceId, sending]);

  return (
    <div className="min-h-screen bg-gray-50 p-8">
      <div className="max-w-2xl mx-auto space-y-6">
        <h1 className="text-2xl font-bold text-gray-900">
          M5Stick App Broadcaster
        </h1>

        {/* Monitor connection — only mounts when deviceId is set */}
        {deviceId && (
          <DeviceMonitor
            deviceId={deviceId}
            onStatus={handleStatus}
            onApp={handleApp}
          />
        )}

        {/* Device ID input */}
        <div className="flex items-center gap-3">
          <label htmlFor="device-id" className="text-sm font-medium text-gray-700">
            Device ID
          </label>
          <input
            id="device-id"
            type="text"
            value={deviceId}
            onChange={(e) => setDeviceId(e.target.value.trim())}
            placeholder="e.g. a1b2c3d4"
            className="flex-1 px-3 py-2 border border-gray-300 rounded-lg text-sm font-mono focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
          />
          {deviceId && (
            <span className="text-xl" title={deviceConnected ? "Device connected" : "Device not connected"}>
              {deviceConnected ? "\u2705" : "\u274C"}
            </span>
          )}
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
