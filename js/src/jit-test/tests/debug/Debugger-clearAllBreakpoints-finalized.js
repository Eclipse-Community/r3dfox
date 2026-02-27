gczeal(23);

String + "";

var g = newGlobal({ newCompartment: true });
var dbg = Debugger(g);
dbg.onNewScript = function (script) {
  script.setBreakpoint(0, () => {});
};
g.eval("");

// Trigger GC, which will mark the eval script about to be finalized,
// and the DebugScriptMap entry will be removed.
Uint8Array;

// This shouldn't try to use the DebugScriptMap entry.
dbg.clearAllBreakpoints();
