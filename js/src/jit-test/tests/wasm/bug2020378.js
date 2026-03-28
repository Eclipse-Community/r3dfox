// |jit-test| skip-if: !wasmThreadsEnabled()

const m = new WebAssembly.Module(wasmTextToBinary(`
(module
  (memory $m32 1 1)
  (memory $m64 i64 1 1)
  (memory $m32s 1 1 shared)
  (memory $m64s i64 1 1 shared)

  ${["", "s"].map(s => `
    (func (export "wait3232${s}") (param i32) (result i32)
      (memory.atomic.wait32 $m32${s}
        (local.get 0)
        (i32.const 0)
        (i64.const 0)
      )
    )
    (func (export "wait3264${s}") (param i32) (result i32)
      (memory.atomic.wait64 $m32${s}
        (local.get 0)
        (i64.const 0)
        (i64.const 0)
      )
    )
    (func (export "wait6432${s}") (param i64) (result i32)
      (memory.atomic.wait32 $m64${s}
        (local.get 0)
        (i32.const 0)
        (i64.const 0)
      )
    )
    (func (export "wait6464${s}") (param i64) (result i32)
      (memory.atomic.wait64 $m64${s}
        (local.get 0)
        (i64.const 0)
        (i64.const 0)
      )
    )
    (func (export "notify32${s}") (param i32) (result i32)
      (memory.atomic.notify $m32${s}
        (local.get 0)
        (i32.const 0)
      )
    )
    (func (export "notify64${s}") (param i64) (result i32)
      (memory.atomic.notify $m64${s}
        (local.get 0)
        (i32.const 0)
      )
    )
  `).join("\n")}
)`));
const {
  wait3232, wait3264,
  wait6432, wait6464,
  notify32, notify64,

  wait3232s, wait3264s,
  wait6432s, wait6464s,
  notify32s, notify64s,
} = new WebAssembly.Instance(m).exports;

//
// Shared memories
//

assertEq(wait3232s(0), 2);
assertEq(wait3232s(65532), 2);
assertErrorMessage(() => wait3232s(65533), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3232s(65536), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3232s(-8), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3232s(-4), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3232s(-3), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3232s(-2), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3232s(-1), WebAssembly.RuntimeError, /unaligned/);

assertEq(wait3264s(0), 2);
assertEq(wait3264s(65528), 2);
assertErrorMessage(() => wait3264s(65529), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(65536), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3264s(-16), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3264s(-8), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait3264s(-7), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-6), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-5), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-4), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-3), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-2), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264s(-1), WebAssembly.RuntimeError, /unaligned/);

assertEq(wait6432s(0n), 2);
assertEq(wait6432s(65532n), 2);
assertErrorMessage(() => wait6432s(65533n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6432s(65536n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n-4n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n-4n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(-4n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(-3n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6432s(-2n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6432s(-1n), WebAssembly.RuntimeError, /unaligned/);

assertEq(wait6464s(0n), 2);
assertEq(wait6464s(65528n), 2);
assertErrorMessage(() => wait6464s(65529n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(65536n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n-16n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**32n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n-16n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6432s(2n**33n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6464s(-16n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6464s(-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => wait6464s(-7n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-6n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-5n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-4n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-3n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-2n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464s(-1n), WebAssembly.RuntimeError, /unaligned/);

assertEq(notify32s(65532), 0);
assertErrorMessage(() => notify32s(65533), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify32s(65536), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify32s(-8), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify32s(-4), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify32s(-3), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify32s(-2), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify32s(-1), WebAssembly.RuntimeError, /unaligned/);

assertEq(notify64s(65532n), 0);
assertErrorMessage(() => notify64s(65533n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify64s(65536n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify64s(-8n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify64s(-4n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify64s(-3n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify64s(-2n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify64s(-1n), WebAssembly.RuntimeError, /unaligned/);

//
// Non-shared memories
//

assertErrorMessage(() => wait3232(65532), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait3232(65533), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3232(65536), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait3232(-4), WebAssembly.RuntimeError, /non-shared/);

assertErrorMessage(() => wait3264(65528), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait3264(65529), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait3264(65536), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait3264(-8), WebAssembly.RuntimeError, /non-shared/);

assertErrorMessage(() => wait6432(65532n), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait6432(65533n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6432(65536n), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait6432(-4n), WebAssembly.RuntimeError, /non-shared/);

assertErrorMessage(() => wait6464(65528n), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait6464(65529n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => wait6464(65536n), WebAssembly.RuntimeError, /non-shared/);
assertErrorMessage(() => wait6464(-8n), WebAssembly.RuntimeError, /non-shared/);

assertEq(notify32(65532), 0);
assertErrorMessage(() => notify32(65533), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify32(65536), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify32(-4), WebAssembly.RuntimeError, /index out of bounds/);

assertEq(notify64(65532n), 0);
assertErrorMessage(() => notify64(65533n), WebAssembly.RuntimeError, /unaligned/);
assertErrorMessage(() => notify64(65536n), WebAssembly.RuntimeError, /index out of bounds/);
assertErrorMessage(() => notify64(-4n), WebAssembly.RuntimeError, /index out of bounds/);
