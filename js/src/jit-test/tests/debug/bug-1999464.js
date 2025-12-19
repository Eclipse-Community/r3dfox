fullcompartmentchecks(1);
var x = newGlobal({ newCompartment: true });
Debugger(x).onEnterFrame = function (y) {
  y.script.setBreakpoint(0, {});
};
x.eval("(function(){})()");
