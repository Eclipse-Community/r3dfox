var x = newGlobal({ newCompartment: true });
Debugger(x).onNewScript = function f(z) { m = z };
x.eval("function g(){}");
m.setBreakpoint(0, {});
nukeAllCCWs();
recomputeWrappers();
gc();

