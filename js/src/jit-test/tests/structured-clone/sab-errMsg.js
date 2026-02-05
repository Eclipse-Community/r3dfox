// |jit-test| skip-if: !sharedMemoryEnabled()

// Check the error mssage when the prefs for COOP/COEP are both enable or not.
var g = newGlobal();
var ex;
const sab = new SharedArrayBuffer();
try {
  g.serialize(sab);
} catch (e) {
  ex = e;
}
assertEq(ex.toString(),
         `TypeError: The SharedArrayBuffer object cannot be serialized. The ` +
         `Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy HTTP ` +
         `headers will enable this in the future.`);

var h = newGlobal({enableCoopAndCoep: true});
try {
  h.serialize(sab);
} catch (e) {
  ex = e;
}
assertEq(ex.toString(),
         `TypeError: The SharedArrayBuffer object cannot be serialized. The ` +
         `Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy HTTP ` +
         `headers can be used to enable this.`);

try {
  const s = serialize([sab], undefined, { scope: "DifferentProcess", SharedArrayBuffer: "allow" });
  deserialize(s, { scope: "DifferentProcess" });
  assertEq("threw exception?", true);
} catch (e) {
  ex = e;
}
assertEq(ex.toString().includes("Policy object must forbid cloning shared memory objects cross-process"), true);

// Can't deserialize a SameProcess buffer when only allowing DifferentProcess scope.
try {
  const s = serialize([sab], undefined, { SharedArrayBuffer: "allow" });
  deserialize(s, { scope: "DifferentProcess" });
  assertEq("threw exception?", true);
} catch (e) {
  ex = e;
}
assertEq(ex.toString().includes("incompatible structured clone scope"), true);

// If a buffer is tampered with, it can only be deserialized as DifferentProcess.
try {
  const s = serialize([sab], undefined, { SharedArrayBuffer: "allow" });
  const s2 = serialize([sab], undefined, { SharedArrayBuffer: "allow" });
  const ta = new Uint32Array(s.arraybuffer);
  ta[2] = 2; // DifferentProcess
  // synthetic buffer! Forces scope to DifferentProcess despite what we say below.
  s2.clonebuffer = ta.buffer;
  const result = deserialize(s2, { SharedArrayBuffer: "allow", scope: "SameProcess" });
  assertEq("threw exception?", true);
} catch (e) {
  ex = e;
}
assertEq(ex.toString().includes("Cannot use less restrictive scope"), true);

// You can't deserialize with both scope=DifferentProcess and allowing shared memory.
try {
  const s = serialize([sab], undefined, { SharedArrayBuffer: "allow" });
  const result = deserialize(s, { SharedArrayBuffer: "allow", scope: "DifferentProcess" });
  assertEq("threw exception?", true);
} catch (e) {
   ex = e;
 }
assertEq(ex.toString().includes("deserialize in DifferentProcess scope cannot allow shared memory"), true);
