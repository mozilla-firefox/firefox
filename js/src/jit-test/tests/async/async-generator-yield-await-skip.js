// |jit-test| error:all-jobs-completed-successfully

// The implicit await in an async generator's yield can be skipped when the
// generator is resumed from the job queue and no other job is pending. Check
// this produces the same results, including for promises and rejections.

const log = [];
let resolveStart;
const start = new Promise(r => { resolveStart = r; });

// Each yield follows an await, so the generator is resumed from the job
// queue.
async function* g() {
  await start;
  log.push("g:resumed");
  const a = yield 1;
  log.push("g:a=" + a);
  await null;
  yield Promise.resolve(2);
  await null;
  try {
    yield Promise.reject(new Error("rej"));
  } catch (e) {
    log.push("g:caught " + e.message);
  }
  await null;
  yield { value: 4 };
  return 5;
}

async function consume() {
  const it = g();
  const results = [];
  let r = await it.next("ignored");
  while (!r.done) {
    results.push(typeof r.value === "object" ? r.value.value : r.value);
    r = await it.next("sent" + results.length);
  }
  results.push("ret:" + r.value);
  return results;
}

// Let the shell drain the job queue, so there's no JS code on the stack when
// the generator is resumed.
consume().then(results => {
  assertEq(JSON.stringify(results), JSON.stringify([1, 2, 4, "ret:5"]));
  assertEq(JSON.stringify(log), JSON.stringify([
    "g:resumed", "g:a=sent1", "g:caught rej"
  ]));
  throw "all-jobs-completed-successfully";
});
resolveStart();
