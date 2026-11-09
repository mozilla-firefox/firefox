"use strict";

do_get_profile(); // must be called before getting nsIX509CertDB
const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

function load_cert(name, trust) {
  let filename = "test_intermediate_basic_usage_constraints/" + name + ".pem";
  addCertFromFile(certdb, filename, trust);
}

function test_cert_for_usages(certNick, certChainNicks, expected_usages) {
  let certs = [];
  for (let certNick of certChainNicks) {
    let certPEM = readFile(
      do_get_file(
        "test_intermediate_basic_usage_constraints/" + certNick + ".pem"
      ),
      false
    );
    certs.push(certdb.addCertFromBase64(pemToBase64(certPEM), ",,"));
  }

  let certPEM = readFile(
    do_get_file(
      "test_intermediate_basic_usage_constraints/" + certNick + ".pem"
    ),
    false
  );
  let cert = certdb.constructX509FromBase64(pemToBase64(certPEM));
  return asyncTestCertificateUsages(certdb, cert, expected_usages);
}

add_task(async function () {
  let ee_usages = [
    Ci.nsIX509CertDB.verifyUsageTLSClient,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    Ci.nsIX509CertDB.verifyUsageEmailSigner,
    Ci.nsIX509CertDB.verifyUsageEmailRecipient,
  ];
  let ca_usages = [Ci.nsIX509CertDB.verifyUsageTLSServerCA];
  let eku_usages = [
    Ci.nsIX509CertDB.verifyUsageTLSClient,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
  ];

  // Load the ca into mem
  let ca_name = "ca";
  load_cert(ca_name, "CTu,CTu,CTu");
  await test_cert_for_usages(ca_name, [], ca_usages);

  // A certificate with no basicConstraints extension is considered an EE.
  await test_cert_for_usages("int-no-extensions", [], ee_usages);

  // int-no-extensions is an EE (see previous case), so no certs can chain to
  // it.
  await test_cert_for_usages("ee-int-no-extensions", ["int-no-extensions"], []);

  // a certificate with basicConstraints.cA==false is considered an EE.
  await test_cert_for_usages("int-not-a-ca", [], ee_usages);

  // int-not-a-ca is an EE (see previous case), so no certs can chain to it.
  await test_cert_for_usages("ee-int-not-a-ca", ["int-not-a-ca"], []);

  // a certificate with basicConstraints.cA==false but with the keyCertSign
  // key usage may not act as a CA (it can act like an end-entity).
  await test_cert_for_usages("int-cA-FALSE-asserts-keyCertSign", [], ee_usages);
  await test_cert_for_usages(
    "ee-int-cA-FALSE-asserts-keyCertSign",
    ["int-cA-FALSE-asserts-keyCertSign"],
    []
  );

  // int-limited-depth has cA==true and a path length constraint of zero.
  await test_cert_for_usages("int-limited-depth", [], ca_usages);

  // path length constraints do not affect the ability of a non-CA cert to
  // chain to to the CA cert.
  await test_cert_for_usages(
    "ee-int-limited-depth",
    ["int-limited-depth"],
    ee_usages
  );

  // ca
  //   int-limited-depth (cA==true, pathLenConstraint==0)
  //      int-limited-depth-invalid (cA==true)
  //
  await test_cert_for_usages(
    "int-limited-depth-invalid",
    ["int-limited-depth"],
    []
  );
  await test_cert_for_usages(
    "ee-int-limited-depth-invalid",
    ["int-limited-depth-invalid", "int-limited-depth"],
    []
  );

  // int-valid-ku-no-eku has keyCertSign
  await test_cert_for_usages("int-valid-ku-no-eku", [], ca_usages);
  await test_cert_for_usages(
    "ee-int-valid-ku-no-eku",
    ["int-valid-ku-no-eku"],
    ee_usages
  );

  // int-bad-ku-no-eku has basicConstraints.cA==true and has a KU extension
  // but the KU extension is missing keyCertSign. Note that mozilla::pkix
  // doesn't validate certificates with basicConstraints.Ca==true for non-CA
  // uses.
  await test_cert_for_usages("int-bad-ku-no-eku", [], []);
  await test_cert_for_usages("ee-int-bad-ku-no-eku", ["int-bad-ku-no-eku"], []);

  // int-no-ku-no-eku has basicConstraints.cA==true and no KU extension.
  // We treat a missing KU as "any key usage is OK".
  await test_cert_for_usages("int-no-ku-no-eku", [], ca_usages);
  await test_cert_for_usages(
    "ee-int-no-ku-no-eku",
    ["int-no-ku-no-eku"],
    ee_usages
  );

  // int-valid-ku-server-eku has basicConstraints.cA==true, keyCertSign in KU,
  // and EKU=={id-kp-serverAuth,id-kp-clientAuth}.
  await test_cert_for_usages("int-valid-ku-server-eku", [], ca_usages);
  await test_cert_for_usages(
    "ee-int-valid-ku-server-eku",
    ["int-valid-ku-server-eku"],
    eku_usages
  );

  // int-bad-ku-server-eku has basicConstraints.cA==true, a KU without
  // keyCertSign, and EKU=={id-kp-serverAuth,id-kp-clientAuth}.
  await test_cert_for_usages("int-bad-ku-server-eku", [], []);
  await test_cert_for_usages(
    "ee-int-bad-ku-server-eku",
    ["int-bad-ku-server-eku"],
    []
  );

  // int-bad-ku-server-eku has basicConstraints.cA==true, no KU, and
  // EKU=={id-kp-serverAuth,id-kp-clientAuth}.
  await test_cert_for_usages("int-no-ku-server-eku", [], ca_usages);
  await test_cert_for_usages(
    "ee-int-no-ku-server-eku",
    ["int-no-ku-server-eku"],
    eku_usages
  );
});
