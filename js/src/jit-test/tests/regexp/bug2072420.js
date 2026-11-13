// |jit-test| skip-if: typeof Intl === 'undefined'

const r = /(.)(?i:\1)/u;
const s = "\u{10400}\u{10428}";
for (let i = 0; i < 10; i++) {
  if (!r.test(s)) throw new Error("Unicode backreference mismatch");
}
