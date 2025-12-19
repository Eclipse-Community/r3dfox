var dbgA = new Debugger;
var g1 = newGlobal({newCompartment: true});
g1.eval('function g1f() { print("Weltuntergang"); }');
var DOAg1 = dbgA.addDebuggee(g1);
var DOAg1f = DOAg1.getOwnPropertyDescriptor('g1f').value;
DOAg1f.script.setBreakpoint(0, { hit: () => { logA += '1'; } });
gczeal(2,1)
class Base { }
recomputeWrappers();
