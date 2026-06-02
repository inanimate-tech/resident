/* eslint-disable */
// Based on `wrangler types` output, hand-edited to add OPENAI_API_KEY.
// Secrets aren't declared in wrangler.jsonc, so regenerating with
// `wrangler types` will DROP OPENAI_API_KEY — re-add it if you do.
declare namespace Cloudflare {
  interface GlobalProps {
    mainModule: typeof import("./src/worker");
    durableNamespaces: "VoiceAgent";
  }
  interface Env {
    VoiceAgent: DurableObjectNamespace<import("./src/worker").VoiceAgent>;
  }
}
interface Env extends Cloudflare.Env {
  OPENAI_API_KEY: string;
}
