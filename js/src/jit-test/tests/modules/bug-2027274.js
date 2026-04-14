// |jit-test| slow; error: InternalError: too much recursion
var leaf = parseModule("await 0; throw 1;", "l.js");
registerModule("l", leaf);
moduleLink(leaf);
moduleEvaluate(leaf);
var p = "l";
for (var i = 0; i < 50000; i++) {
  var m = parseModule("import '" + p + "'; if(0) await 0;", "m" + i + ".js");
  registerModule("m" + i, m);
  moduleLink(m);
  moduleEvaluate(m);
  p = "m" + i;
}
drainJobQueue();
