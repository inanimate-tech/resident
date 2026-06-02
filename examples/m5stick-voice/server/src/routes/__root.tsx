import { Outlet, createRootRoute, HeadContent, Scripts } from "@tanstack/react-router"

export const Route = createRootRoute({
  head: () => ({
    meta: [
      { charSet: "utf-8" },
      { name: "viewport", content: "width=device-width, initial-scale=1" },
      { title: "m5stick-voice" },
    ],
  }),
  component: RootComponent,
  notFoundComponent: () => (
    <div style={{ padding: 24, color: "#e6e6f0" }}>
      <h1 style={{ fontSize: 16, opacity: 0.7 }}>m5stick-voice</h1>
      <p style={{ opacity: 0.55 }}>Open <code>/devices/&lt;deviceId&gt;</code>.</p>
    </div>
  ),
})

function RootComponent() {
  return (
    <html lang="en">
      <head>
        <HeadContent />
        <style>{`
          :root { color-scheme: dark; }
          html, body { margin: 0; height: 100%; }
          body { color: #e6e6f0; font: 16px/1.5 system-ui, sans-serif;
                 display: flex; flex-direction: column; height: 100vh; background: transparent; }
          /* Default background for the #bg layer. Lives in the static style
             block so the Background component's runtime <style> can override
             it via specificity-equal, later-in-document rules. */
          #bg { background: #0b0b10; }
        `}</style>
      </head>
      <body>
        <Outlet />
        <Scripts />
      </body>
    </html>
  )
}
