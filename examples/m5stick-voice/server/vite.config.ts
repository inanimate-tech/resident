/// <reference types="node" />
import { defineConfig, type Plugin } from "vite"
import { tanstackStart } from "@tanstack/react-start/plugin/vite"
import { cloudflare } from "@cloudflare/vite-plugin"
import react from "@vitejs/plugin-react"
import path from "path"

/**
 * Shim Node modules that fengari requires at module init but doesn't actually
 * use in browser/Workers contexts. Without this, fengari fails to load (its
 * luaconf.js calls require('os').platform() unconditionally) and Vite logs
 * noisy externalisation warnings. Ported from hawthorn-worker's vite config.
 */
function fengariShims(): Plugin {
  const emptyShim = path.resolve(__dirname, "shims/empty.js")
  const osShim = path.resolve(__dirname, "shims/os.js")
  const readlineSyncShim = path.resolve(__dirname, "shims/readline-sync.js")
  const fengariSsrShim = path.resolve(__dirname, "shims/fengari-web-ssr.js")

  // Modules to shim only when imported by fengari or its deps.
  const fengariScoped: Record<string, string> = {
    fs: emptyShim,
    child_process: emptyShim,
    os: osShim,
    path: emptyShim,
    tmp: emptyShim,
  }

  return {
    name: "fengari-shims",
    enforce: "pre",
    resolveId(source, importer) {
      // fengari-web on SSR: replace with an inert stub. The validator + sim
      // both only run in environments where fengari-web evaluates fine (the
      // browser; node for vitest). On workerd-SSR fengari's `new Function`
      // use trips the strings-to-code guard, so we never want it loaded.
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const env = (this as any).environment?.name
      if (source === "fengari-web" && env === "ssr") {
        return fengariSsrShim
      }

      // readline-sync: always shim — only fengari uses it.
      if (source === "readline-sync") return readlineSyncShim

      if (importer && fengariScoped[source]) {
        if (importer.includes("node_modules/fengari/") || importer.includes("node_modules/fengari-web/") || importer.includes("node_modules/tmp/")) {
          return fengariScoped[source]
        }
      }
      return null
    },
  }
}

export default defineConfig({
  plugins: [
    fengariShims(),
    cloudflare({ viteEnvironment: { name: "ssr" } }),
    tanstackStart(),
    react(),
  ],
})
