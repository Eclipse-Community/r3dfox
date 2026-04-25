// |jit-test| skip-if: !getBuildConfiguration("explicit-resource-management"); --enable-explicit-resource-management

const g = newGlobal({ newCompartment: true });
const dbg = new Debugger(g);

g.leak = function(promise) {
  const pDO = dbg.makeGlobalObjectReference(g).makeDebuggeeValue(promise);
  const reactions = pDO.getPromiseReactions();
  for (const r of reactions) {
    if (r && r.environment) {
      const v = r.environment.getVariable("disposeCapability");
      if (v && v.class === "Array") {
        return v.unsafeDereference();
      }
    }
  }
  return null;
};

g.eval(`
  var as = new AsyncDisposableStack();
  var p;
  as.defer(() => (p = new Promise(() => {})));
  as.use({
    get [Symbol.asyncDispose]() {
      as.disposeAsync();
      var arr = leak(p);
      assertEq(arr, null);
      return function() {};
    }
  });
`);
