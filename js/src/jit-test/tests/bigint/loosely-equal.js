function check() {
  for (let i = 1; i < 2000; i++) {
    assertEq(Object(true) == 1n, true);
    assertEq(1n == Object(true), true);
    assertEq(Object(false) == 0n, true);
    assertEq(0n == Object(false), true);

    let called = false;
    assertEq({ valueOf() { called = true; return 0; }} == 0n, true);
    assertEq(called, true);

    called = false;
    assertEq(1n == { valueOf() { called = true; return 1; }}, true);
    assertEq(called, true);
  }
}

check();
