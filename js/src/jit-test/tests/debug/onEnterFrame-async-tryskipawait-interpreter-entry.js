// |jit-test| --emit-interpreter-entry; --blinterp-eager

// Awaits are still skipped when the resumed frame was entered through its
// interpreter entry trampoline.

load(scriptdir + "onEnterFrame-async-tryskipawait-01.js");
