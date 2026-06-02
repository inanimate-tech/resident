// Minimal os shim — fengari only uses os.platform() to detect Windows.
export function platform() { return "linux" }
export default { platform }
