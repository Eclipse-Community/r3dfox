var dummy = {abc: 1};
// Non-Latin1 UTF-8 function name that hash-collides with the Latin1 "abc" atom.
// The unreachable trap exercises UTF8EqualsChars when building the error message.
var bytes = wasmTextToBinary('(module (func $"\\ee\\96\\95\\ea\\b5\\81\\04" (export "f") unreachable))');
var ex = null;
try {
  new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.f();
} catch (e) {
  ex = e;
}
assertEq(ex.stack.includes("\uE595\uAD41\x04@"), true);
