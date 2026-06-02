// SSR stub for fengari-web. The Lua simulator only runs in the browser.
// fengari-web uses `new Function` at module load, which trips workerd's
// strings-to-code guard during dev SSR. Stubbing it here keeps the SSR
// module graph eval-free; on the client the real fengari-web is used.

const noop = () => {}
const handler = {
  get(_target, prop) {
    if (prop === Symbol.toPrimitive || prop === "toString") {
      return () => "[ssr-fengari-stub]"
    }
    return noop
  },
  apply: () => noop,
}
const lua = new Proxy(noop, handler)
const lauxlib = new Proxy(noop, handler)
const lualib = new Proxy(noop, handler)
const to_luastring = () => null

export { lua, lauxlib, lualib, to_luastring }
export default { lua, lauxlib, lualib, to_luastring }
