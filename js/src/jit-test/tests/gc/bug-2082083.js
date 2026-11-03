// |jit-test| --no-ion

// The test skipped in debug configurations because it's very slow.
if (getBuildConfiguration("debug")){
  quit();
}

// The test requires baseline (but not Ion) and GC zeal.
if (!getBuildConfiguration("has-gczeal") ||
    !getJitCompilerOptions()['baseline.enable']) {
  quit();
}

gczeal(0);
setJitCompilerOption("baseline.warmup.trigger", 10);
gcPreserveCode();
const A = newGlobal({newCompartment: true});
const B = newGlobal({newCompartment: true});
A.B = B; B.A = A;
A.eval(`
  var N = 1200, keys = Object.create(null), atoms, indices;
  var bases = [], deps = [];
  function makeString(i) {
    const chars = new Array(80).fill(71);
    chars[0]=82;chars[1]=65;chars[2]=67;chars[3]=69;
    for(let k=0;k<8;k++){chars[4+k]=65+(i&15);i>>>=4;}
    return String.fromCharCode(...chars);
  }
  for (let i=0; i<2*N; i++) keys[makeString(i)]=i;
  atoms = Object.keys(keys);
  indices=atoms.map(s=>getAtomRefIndex(s));
  function prime() {for(let i=0;i<2*N;i+=2)Reflect.defineProperty(keys,makeString(i),{value:0});}
  function wrappers() {
    bases=[];deps=[];B.copies.length=0;
    for(let i=0;i<2*N;i+=2){
      let s=makeString(i);B.receive(s);let dep=s.slice(1);
      Reflect.defineProperty(keys,s,{value:0});bases.push(s);deps.push(dep);
    }
  }
  function drop() {keys=null;atoms=[];bases=[];deps=[];}
`);
B.eval(`
  var copies=[], readers=[];
  function receive(s){copies.push(s);}
  for(let i=0;i<A.N;i++){
    Object.defineProperty(globalThis,'__weakTmp',{value:A.atoms[2*i+1],writable:true,configurable:true});
    let f=new Function('return __weakTmp;');
    for(let j=0;j<130;j++)f();
    readers.push(f);globalThis.__weakTmp=null;delete globalThis.__weakTmp;
  }
`);

gc();
let lost=-1, round=-1;
for(round=0; round<1000; round++) {
  A.prime();
  gczeal(25,1000000000);
  schedulezone(B);schedulezone('atoms');startgc(1000000);
  assertEq(gcstate(A),'NoGC');
  assertEq(gcstate(B),'MarkBlackAndGray');
  A.wrappers();minorgc();
  unsetgczeal(25);finishgc();
  for(let i=0;i<A.N;i++) {
    if(getAtomRefColor(B,A.indices[2*i+1])==='white') {lost=i;break;}
  }
  if(lost!==-1) {
    break;
  }
}

assertEq(lost, -1);
