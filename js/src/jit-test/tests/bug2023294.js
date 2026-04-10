var gSame = newGlobal({sameCompartmentAs: globalThis});
var g2 = newGlobal({newCompartment: true});

var {object: proxy, transplant} = transplantableObject({proxy: true});

transplant(g2);

gSame.eval("nukeAllCCWs()");

try {
  transplant(gSame);
} catch (e) {}

uneval(proxy);
