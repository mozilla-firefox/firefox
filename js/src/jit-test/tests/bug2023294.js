var gSame = newGlobal({sameCompartmentAs: globalThis});
var g2 = newGlobal({newCompartment: true});

var {object: proxy, transplant} = transplantableObject();

transplant(g2);

gSame.eval("nukeAllCCWs()");

try {
  transplant(gSame);
} catch (e) {}

uneval(proxy);
