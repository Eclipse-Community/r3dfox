// |jit-test| error: TypeError
gczeal(9,16);
function F1() {
    if (!new.target) { throw 'must be called with new'; }
    this.b = null;
}
new F1();
new F1();
function f5() {}
new BigUint64Array(3474);
function f14() {}
function f25(a26, a27) {
    for (let i30 = 0, i31 = true; i31; i31--) {
        function f37() {
            function F38() {}
            for (let i44 = 0, i45 = SharedArrayBuffer; i45;
                (() => {
                    i45--;
                    Int8Array.principal = BigUint64Array;
                    function F50() {}
                    Int8Array.sameZoneAs = /wp(?:a?)+/imu;
                    const v54 = this.newGlobal(Int8Array);
                    const t7 = ({ __proto__: v54 }).Debugger;
                    const v57 = t7(F50);
                    const v59 = v57.getNewestFrame(i30, i45, i45, f25, v57).older;
                    v59.script.setBreakpoint(16, v59);
                })()) {}
            for (let [i134, i135] = (() => {
                    for (let i84 = 0, i85 = 10; i85;
                        (() => {
                            i85--;
                            for (let [i102, i103] = (() => {
                                    for (let [i95, i96] = (() => {
                                            new Uint8Array();
                                            return [0, 10];
                                        })(); i96; i96--) {
                                    }
                                    return [0, SharedArrayBuffer];
                                })();
                                i103; i103--) {}
                            for (let i113 = -4, i114 = 10; i114; i114--) {}
                            for (let i122 = 4, i123 = 10; i123--, i123; i123--) {
                                i123++;
                            }
                        })()) {}
                    return [0, SharedArrayBuffer];
                })();
                i135; i135--) {            }
            for (let i143 = 0, i144 = 10; i144; i144--) {}
        }
        f37.apply();
    }
    for (let i153 = 0, i154 = 10; i154; i154--) {}
    function F160(a162, a163) {
        if (!new.target) { throw 'must be called with new'; }
        this.c = a27;
        this.h = a162;
    }
    new F160(234, a27);
    const v167 = this.nukeAllCCWs();
    for (let i170 = 0, i171 = 10; i171; i171--) {}
    try {
        f25();
    } catch(e178) {}
}
f25(f25, f25);
