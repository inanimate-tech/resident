// Minimal readline-sync shim for fengari's debug library.
// Fengari requires readline-sync which calls process.binding — not available
// in Workers or browser contexts.
export const setDefaultOptions = () => {}
export const question = () => ""
export default { setDefaultOptions, question }
