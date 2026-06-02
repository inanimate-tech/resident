// Fengari has no public type declarations. We treat the dynamically-imported
// namespace as opaque (`any`) inside lua-vm.ts; this stub stops tsc from
// emitting TS7016 on the dynamic import.
declare module "fengari"
