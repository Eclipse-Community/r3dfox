function makeArgs() {
  "use strict";
  return arguments;
}
function test() {
  for (var alloc = 1; alloc < 50; alloc++) {
    var args = makeArgs(1, 2, 3);
    oomAtAllocation(alloc);
    try {
      delete args[0];
    } catch (e) {}
    resetOOMFailure();
    args[0] = "x";
    assertEq(args[0], "x");
  }
}
test();
