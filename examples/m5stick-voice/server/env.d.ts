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
  // Optional: route OpenAI realtime through Cloudflare AI Gateway. If both are
  // set, the worker uses the gateway; otherwise it connects to OpenAI directly.
  CF_ACCOUNT_ID?: string;
  AI_GATEWAY_ID?: string;
  // Optional: only needed if the AI Gateway is set to "authenticated".
  CF_AIG_TOKEN?: string;
  // Optional: set to "1" to force a direct OpenAI connection (bypass gateway).
  OPENAI_DIRECT?: string;
}
