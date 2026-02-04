// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
"use strict";

// Attempts to verify a certificate for a time a few weeks into the future in
// the hopes of avoiding mass test failures when the certificates all expire.
// If this test fails, the certificates probably need to be regenerated.
// See bug 1525191.

// If this test and only this test fails, do the following:
// 1. Create a bug for the issue in "Core :: Security: PSM".
// 2. Write a patch to temporarily disable the test.
// 3. Land the patch.
// 4. Write a patch to reenable the test but don't land it.
// 5. Needinfo the triage owner of Bugzilla's "Core :: Security: PSM" component
//    in the bug.
// 6. Patches to update certificates get created.
// 6.1. Update certificates in security/manager/ssl/tests/unit with
//      ./mach generate-test-certs
// 6.2. Update more certificates with
//      ./mach python build/pgo/genpgocert.py
// 6.3. Temporarily uncomment the code in security/manager/ssl/tests/unit/test_signed_apps/moz.build,
//      build Firefox with |./mach build| and copy the relevant non-build files
//      from the related object directory folder into this folder.
// 6.4. Update the certificate fingerprints mentioned in
//      security/manager/ssl/tests/unit/test_cert_override_read.js with
//      openssl x509 -noout -fingerprint -sha256 -in security/manager/ssl/tests/unit/bad_certs/certName.pem
// 6.5. Update the base64 encoded serial numbers of test-int.pem and other-test-ca.pem in
//      security/manager/ssl/tests/unit/test_cert_storage.js
// 6.5.1. Get the serial number value
//        openssl x509 -noout -in security/manager/ssl/tests/unit/bad_certs/test-int.pem -serial
// 6.5.2. base64 encode the hex value without the prefix
// 6.5.3. Update base64 encoded value in the test file.
// 7. Commit the changes: Mention the year of the update, the date of the
//    next expiration and add these instructions to the commit message.
// 8. Test the patches with a Try push.
// 9. Land the patches on all trees whose code will still be used when the
//    certificates expire in 3 weeks.
add_task(async function () {
  do_get_profile();
  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certDB, "bad_certs/test-ca.pem", "CTu,,");
  let threeWeeksFromNowInSeconds = Date.now() / 1000 + 3 * 7 * 24 * 60 * 60;
  let ee = constructCertFromFile("bad_certs/default-ee.pem");
  await checkCertErrorGenericAtTime(
    certDB,
    ee,
    PRErrorCodeSuccess,
    certificateUsageSSLServer,
    threeWeeksFromNowInSeconds,
    false,
    "test.example.com"
  );
});
