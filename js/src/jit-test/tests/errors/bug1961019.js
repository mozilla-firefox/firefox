let s = undefined;
var f = bindToAsyncStack(
    function () {
       // (a)
        function b () {
          Error.captureStackTrace( // (4)
            {},
            Math.sin
          );
        };
        s = saveStack(); // (3) 
        b();
    },
    { stack: saveStack() }, // (1)
  );

  function g() { 
    // (2)
    f();
  }
g();
