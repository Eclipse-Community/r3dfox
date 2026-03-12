// Don't crash on {return:} from onStep in a generator at a Yield.

// This test force-returns from each bytecode instruction in a generator.

let g = newGlobal({ newCompartment: true });
g.eval(`
function* gen() {
  yield 1;
}
`)

let dbg = new Debugger(g);

let targetSteps = 0;
let found = true;
dbg.onEnterFrame = (frame) => {
  let steps = 0;
  frame.onStep = () => {
    if (steps++ == targetSteps) {
      found = true;
      return { return: 0xdead };
    }
  }
}
dbg.uncaughtExceptionHook = () => undefined

while (found) {
  found = false;
  targetSteps++;
  for (var y of g.gen()) {}
}
