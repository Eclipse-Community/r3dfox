function test() {
  var f = function() { return arguments; };
  var template = new Array(9000).fill(0);

  var a = f.apply(null, template);
  Object.defineProperty(a, 0, {value: "v1", writable: false, configurable: true});

  for (var alloc = 4; alloc < 15; alloc++) {
    var args = null;
    var ok = false;
    oomAtAllocation(alloc);
    try {
      args = f.apply(null, template);
      ok = true;
      Object.defineProperty(args, 0, {value: "v1", writable: false, configurable: true});
    } catch (e) {}
    resetOOMFailure();

    if (ok) {
      Object.defineProperty(args, 0, {value: "v2"});
      assertEq(args[0], "v2");
    }
  }
}
test();
