// |jit-test| error:all-jobs-completed-successfully

load(libdir + "asserts.js");
load(libdir + 'match.js');
load(libdir + 'match-debugger.js');
const { Pattern } = Match;
const { OBJECT_WITH_EXACTLY: EXACT } = Pattern;

let g = newGlobal({newCompartment: true});
let dbg = Debugger(g);
const log = [];
g.capture = function () {
  dbg.getNewestFrame().onPop = completion => {
    log.push(completion);
  };
};

g.eval(`
    async function* asyncgen() {
        capture();
        await Promise.resolve(1);
        yield 2;
        await Promise.resolve(3);
        yield 4;
        return "ok";
    }
`);

async function collect() {
  let items = [];
  for await (let item of g.asyncgen()) {
    items.push(item);
  }
  return items;
}

collect().then(value => {
  assertDeepEq(value, [2, 4]);

  Pattern([
    EXACT({ return: new DebuggerObjectPattern("Promise"), await: true }),
    EXACT({ return: 2, yield: true }),
    EXACT({ return: new DebuggerObjectPattern("Promise"), await: true }),
    EXACT({ return: 4, yield: true }),
    EXACT({ return: "ok", await: true }),
    EXACT({ return: "ok" }),
  ]).assert(log);

  throw "all-jobs-completed-successfully";
});

