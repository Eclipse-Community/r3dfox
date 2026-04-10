var bytes = new BigUint64Array([
  0xfff1000000000002n, // SCTAG_HEADER (version=2)
  0xffff002200000000n, // SCTAG_ERROR_OBJECT
  0xffff000480000001n, // .message = SCTAG_STRING (length=1, Latin-1)
  0x0000000000000078n, //   string data: "x"
  0xffff000700000000n, // .hasCause = SCTAG_ARRAY_OBJECT (length=0) <-- INVALID!!!
  0xffff000480000002n, // .filename = SCTAG_STRING (length=2, Latin-1)
  0x000000000000652dn, //   string data: "e-"
  0xffff000300000002n, // .lineNumber = SCTAG_INT32 (value=2)
  0xffff000300000011n, // .column = SCTAG_INT32 (value=17)
  0xffff000000000000n, // .cause = SCTAG_NULL
  0xffff000000000000n, // .errors = SCTAG_NULL
  0xffff0016ffff0018n, // .stack = SCTAG_SAVED_FRAME_OBJECT | SCTAG_NULL_JSPRINCIPALS
  0xffff000200000000n, //   .mutedErrors = SCTAG_BOOLEAN (value=0)
  0xffff000480000002n, //   .source = SCTAG_STRING (length=2, Latin-1)
  0x000000000000652dn, //     string data: "e-"
  0xffff000300000002n, //   .lineNumber = SCTAG_INT32 (value=2)
  0xffff000300000011n, //   .columnNumber SCTAG_INT32 (value=17)
  0xffff000000000000n, //   .functionDisplayName = SCTAG_NULL
  0xffff000000000000n, //   .asyncCause = SCTAG_NULL
  0xffff000000000000n, // .parent = SCTAG_NULL
  0xffff001300000000n, // SCTAG_END_OF_KEYS
  0xffff001300000000n, // SCTAG_END_OF_KEYS
]);
var buf = serialize(null, undefined, {scope: 'DifferentProcess'});
buf.arraybuffer = bytes.buffer;

var e;
try {
    deserialize(buf);
} catch (err) {
    e = err;
}
assertEq(e.message.includes("hasCause must be a boolean"), true);
