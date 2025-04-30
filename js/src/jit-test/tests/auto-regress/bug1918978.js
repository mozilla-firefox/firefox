// |jit-test| --no-ggc

const x = new ArrayBuffer(1, {
  maxByteLength: 1,
});

oomTest(() => {
  for (let i = 0; i < 5; ++i) {
    new Int8Array(x);
  }
});

fullcompartmentchecks(true);
gc();
