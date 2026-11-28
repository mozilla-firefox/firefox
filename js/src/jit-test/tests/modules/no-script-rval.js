// Module scripts are compiled with noScriptRval. Test top-level code that saves
// the return value across an implicit await.

function run(source, expected = "fulfilled") {
  globalThis.log = [];
  let m = parseModule(source);
  moduleLoadAndLink(m);
  let status = "pending";
  moduleEvaluate(m).then(() => { status = "fulfilled"; },
                         () => { status = "rejected"; });
  drainJobQueue();
  assertEq(status, expected);
  return globalThis.log.join(",");
}

assertEq(run(`
  for await (let x of [1, 2]) {
    log.push(x);
    break;
  }
  log.push("done");
`), "1,done");

const resource = `
  let res = id => ({[Symbol.asyncDispose]() { log.push("dispose" + id); }});
`;

assertEq(run(resource + `
  for (await using r of [res(1), res(2)]) {
    log.push("body");
    break;
  }
  log.push("done");
`), "body,dispose1,done");

assertEq(run(resource + `
  try {
    for (await using r of [res(1)]) {
      throw "boom";
    }
  } catch (e) {
    log.push(e);
  }
`), "dispose1,boom");

assertEq(run(resource + `
  for (await using r of [res(1), res(2)]) {
    log.push("body");
  }
`), "body,dispose1,body,dispose2");

assertEq(run(`
  await 1;
  log.push("done");
`), "done");

assertEq(run(`
  await 1;
  log.push("before");
  throw "boom";
`, "rejected"), "before");
