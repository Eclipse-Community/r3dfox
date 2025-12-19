var x = newGlobal({ newCompartment: true });
var y = Debugger(x);
y.x = y;
y.onDebuggerStatement = function(w) {
  nukeAllCCWs();
  w.environment.getVariable("x");
}
x.eval('function f(z) { with(z) { debugger } }');
x.f(y);
