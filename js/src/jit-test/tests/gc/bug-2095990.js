function f(x, y) {
  new Proxy(Object.getPrototypeOf(y), {
    get() {},
    has() {
      x;
    },
  });
}
for (var i = 3; i > 0; i--) {
  var z = new Int8Array();
  z[Symbol.toPrimitive] = function () {
    f("", 1);
    (function () {
      f("", 2);
    })();
    (() => {
      f("", 3);
      return [, 4];
    })();
    for (
      let [m, n] = (() => {
        f("", 5);
        return [5, this];
      })();
      (() => {
        f("", n);
        return n--;
      })();
    );
  };
  try {
    z.set({}, z);
  } catch {}
}
for (let a, b = (function () {})(); a--; );
gc();
