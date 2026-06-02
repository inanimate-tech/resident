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
        `}</style>
      </head>
      <body>
        <Outlet />
        <Scripts />
      </body>
    </html>
  )
}
