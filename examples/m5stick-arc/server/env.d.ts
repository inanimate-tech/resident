/* eslint-disable */
declare namespace Cloudflare {
  interface GlobalProps {
    mainModule: typeof import("./src/worker");
    durableNamespaces: "ArcAgent";
  }
  interface Env {
    ArcAgent: DurableObjectNamespace<import("./src/worker").ArcAgent>;
    AI: Ai;
    CLOUDFLARE_AIG_ID: string;
  }
}
interface Env extends Cloudflare.Env {}

declare module "*.lua" {
  const src: string;
  export default src;
}

declare module "luaparse" {
  const luaparse: { parse(code: string, opts?: { luaVersion?: string }): unknown };
  export default luaparse;
}
