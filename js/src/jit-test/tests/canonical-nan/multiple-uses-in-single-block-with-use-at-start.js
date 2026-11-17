function f(f64, c, fn) {
  // Load maybe non-canonical NaN.
  var x = f64[0];

  // Call function to emit guards.
  fn(0);

  // New block to move uses of |x| into a separate basic block.
  if (c) {
    // Call |fn(x)| twice to have multiple uses for |x|. Function guards from
    // |fn(0)| are reused, which requires to add |MCanonicalizeNaN| as the
    // first instruction in this basic block.
    fn(x);
    fn(x);
  }
}

// Don't inline |f| into outer script.
with ({});

function g() {
  // Don't inline into |f|.
  with ({});
}

let f64 = new Float64Array(2);
for (let i = 0; i < 100; ++i) {
  f(f64, i & 1, g);
}
