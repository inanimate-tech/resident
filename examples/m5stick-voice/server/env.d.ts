/* eslint-disable */
// Based on `wrangler types` output, hand-edited to add OPENAI_API_KEY.
// Secrets aren't declared in wrangler.jsonc, so regenerating with
// `wrangler types` will DROP OPENAI_API_KEY — re-add it if you do.
interface __BaseEnv_Env {
	VoiceAgent: DurableObjectNamespace<import("./src/server").VoiceAgent>;
}
declare namespace Cloudflare {
	interface GlobalProps {
		mainModule: typeof import("./src/server");
		durableNamespaces: "VoiceAgent";
	}
	interface Env extends __BaseEnv_Env {}
}
interface Env extends __BaseEnv_Env {
	OPENAI_API_KEY: string;
}
