function test(f) {
  var frames = [];
  for (var code of ["saveStack()", "saveStack() // different source"]) {
    frames.push(f(code));
  }
  assertEq(frames[0] !== frames[1], true);
  assertEq(frames[0].sourceId !== frames[1].sourceId, true);
  assertEq(frames[0].source, frames[1].source);
  assertEq(frames[0].line, frames[1].line);
  assertEq(frames[0].column, frames[1].column);
  assertEq(frames[0].parent, frames[1].parent);
}
test(code => eval(code));
test(code => evaluate(code, {fileName: "same.js"}));
