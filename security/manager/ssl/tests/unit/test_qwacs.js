// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

"use strict";

do_get_profile(); // must be called before getting nsIX509CertDB
const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

const { QWACs } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/QWACs.sys.mjs"
);

async function verify_1_qwacs(filename, expectSuccess, extraCertNames = []) {
  let cert = constructCertFromFile(filename);
  let result = await certdb.asyncVerifyQWAC(
    Ci.nsIX509CertDB.OneQWAC,
    cert,
    "example.com",
    extraCertNames.map(filename => constructCertFromFile(filename))
  );
  equal(
    result,
    expectSuccess,
    `${filename} ${expectSuccess ? "should" : "should not"} verify as 1-QWAC`
  );
}

add_task(async function test_verify_1_qwacs() {
  Services.prefs.clearUserPref("security.qwacs.enable_test_trust_anchors");
  // By default, the QWACs test trust anchors are not used.
  await verify_1_qwacs("test_qwacs/1-qwac.pem", false);
  await verify_1_qwacs("test_qwacs/1-qwac-qevcpw.pem", false);

  Services.prefs.setBoolPref("security.qwacs.enable_test_trust_anchors", true);

  await verify_1_qwacs("test_qwacs/1-qwac.pem", true);
  await verify_1_qwacs("test_qwacs/1-qwac-qevcpw.pem", true);

  await verify_1_qwacs("test_qwacs/1-qwac-other-optional-qcs.pem", true);
  await verify_1_qwacs("test_qwacs/1-qwac-unrelated-policy.pem", true);

  // One or more intermediates may be necessary for path building.
  await verify_1_qwacs("test_qwacs/1-qwac-via-intermediate.pem", false);
  await verify_1_qwacs("test_qwacs/1-qwac-via-intermediate.pem", true, [
    "test_qwacs/test-int.pem",
  ]);

  await verify_1_qwacs("test_qwacs/empty-qc-type-statement.pem", false);
  await verify_1_qwacs("test_qwacs/missing-qc-type-statement.pem", false);
  await verify_1_qwacs("test_qwacs/missing-qcs-compliance.pem", false);
  await verify_1_qwacs("test_qwacs/wrong-qc-type.pem", false);
  await verify_1_qwacs("test_qwacs/no-1-qwac-policies.pem", false);
  await verify_1_qwacs("test_qwacs/no-policies.pem", false);
  await verify_1_qwacs("test_qwacs/2-qwac.pem", false);
});

async function verify_2_qwacs(
  filename,
  expectSuccess,
  hostname = "example.com"
) {
  let cert = constructCertFromFile(filename);
  let result = await certdb.asyncVerifyQWAC(
    Ci.nsIX509CertDB.TwoQWAC,
    cert,
    hostname,
    []
  );
  equal(
    result,
    expectSuccess,
    `${filename} ${expectSuccess ? "should" : "should not"} verify as 2-QWAC`
  );
}

add_task(async function test_verify_2_qwacs() {
  Services.prefs.clearUserPref("security.qwacs.enable_test_trust_anchors");
  // By default, the QWACs test trust anchors are not used.
  await verify_2_qwacs("test_qwacs/2-qwac.pem", false);

  Services.prefs.setBoolPref("security.qwacs.enable_test_trust_anchors", true);

  await verify_2_qwacs("test_qwacs/2-qwac.pem", true);

  await verify_2_qwacs("test_qwacs/1-qwac.pem", false);
  await verify_2_qwacs("test_qwacs/2-qwac-no-eku.pem", false);
  await verify_2_qwacs("test_qwacs/2-qwac-tls-server-eku.pem", false);
  await verify_2_qwacs("test_qwacs/2-qwac-multiple-key-purpose-eku.pem", false);
  await verify_2_qwacs("test_qwacs/2-qwac.pem", false, "example.org");
});

// Produces base64url(hash(base64url(certificate DER)))
async function certificateHash(certificate, hashAlg) {
  let hash = await crypto.subtle.digest(
    hashAlg.replace("S", "SHA-"),
    new Uint8Array(
      stringToArray(
        QWACs.toBase64URLEncoding(arrayToString(certificate.getRawDER()))
      )
    )
  );
  return QWACs.toBase64URLEncoding(arrayToString(new Uint8Array(hash)));
}

const kTLSCertificateBindingEE = constructCertFromFile("test_qwacs/2-qwac.pem");

const kTLSCertificateBindingHeader = {
  alg: "",
  cty: "TLS-Certificate-Binding-v1",
  // RFC 7515 Section 4.1.6: "Each string in the array is a base64-encoded
  // (Section 4 of [RFC4648] -- not base64url-encoded) DER [ITU.X690.2008] PKIX
  // certificate value."
  x5c: [],
  sigD: {
    mId: "http://uri.etsi.org/19182/ObjectIdByURIHash",
    pars: [],
    hashM: "",
    hashV: [],
  },
};

async function makeBindingHeader(
  certificateChain,
  certificatesToBind,
  signingAlg,
  hashAlg
) {
  let header = structuredClone(kTLSCertificateBindingHeader);
  header.alg = signingAlg;
  header.x5c = certificateChain.map(c => btoa(arrayToString(c.getRawDER())));
  header.sigD.hashM = hashAlg;
  for (let toBind of certificatesToBind) {
    header.sigD.pars.push("");
    header.sigD.hashV.push(await certificateHash(toBind, hashAlg));
  }
  return header;
}

add_task(async function test_validate_tls_certificate_binding_header() {
  let serverCertificate = constructCertFromFile("bad_certs/default-ee.pem");
  let testHeader = await makeBindingHeader(
    [kTLSCertificateBindingEE],
    [serverCertificate],
    "RS256",
    "S256"
  );
  let validatedHeader = QWACs.validateTLSCertificateBindingHeader(testHeader);
  ok(validatedHeader, "header should validate successfully");
  deepEqual(validatedHeader.algorithm, {
    name: "RSASSA-PKCS1-v1_5",
    hash: "SHA-256",
  });
  equal(validatedHeader.certificates.length, 1);
  equal(validatedHeader.hashAlg, "SHA-256");
  equal(validatedHeader.hashes.length, 1);

  let headerWithExtraKey = structuredClone(testHeader);
  headerWithExtraKey.extra = "foo";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithExtraKey),
    "header with extra key should not validate"
  );

  let headerWithExtraSigDKey = structuredClone(testHeader);
  headerWithExtraSigDKey.sigD.additional = "bar";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithExtraSigDKey),
    "header with extra key in sigD should not parse"
  );

  let headerWithWrongCty = structuredClone(testHeader);
  headerWithWrongCty.cty = "TLS-Certificate-Binding-v2";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithWrongCty),
    "header with wrong cty should not parse"
  );

  let headerWithWrongMId = structuredClone(testHeader);
  headerWithWrongMId.sigD.mId = "http://example.org";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithWrongMId),
    "header with wrong sigD.mId should not parse"
  );

  let headerWithTooManyPars = structuredClone(testHeader);
  headerWithTooManyPars.sigD.pars.push("");
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithTooManyPars),
    "header with too many sigD.pars elements should not parse"
  );

  let headerWithInvalidHashV = structuredClone(testHeader);
  headerWithInvalidHashV.sigD.hashV[0] = 1234;
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithInvalidHashV),
    "header with invalid sigD.hashV should not parse"
  );

  let headerWithTooManyHashV = structuredClone(testHeader);
  headerWithTooManyHashV.sigD.hashV.push(headerWithTooManyHashV.sigD.hashV[0]);
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithTooManyHashV),
    "header with too many sigD.hashV elements should not parse"
  );

  let headerWithEmptyX5c = structuredClone(testHeader);
  headerWithEmptyX5c.x5c = [];
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithEmptyX5c),
    "header with empty x5c should not parse"
  );

  let headerWithUnsupportedAlg = structuredClone(testHeader);
  headerWithUnsupportedAlg.alg = "RS384";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithUnsupportedAlg),
    "header with unsupported alg should not parse"
  );

  let headerWithUnsupportedHashM = structuredClone(testHeader);
  headerWithUnsupportedHashM.sigD.hashM = "S224";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithUnsupportedHashM),
    "header with unsupported sigD.hashM should not parse"
  );

  let headerWithOptionalHeaders = structuredClone(testHeader);
  headerWithOptionalHeaders.kid = "optional kid";
  headerWithOptionalHeaders.iat = "optional iat";
  ok(
    QWACs.validateTLSCertificateBindingHeader(headerWithOptionalHeaders),
    "header with optional headers should parse"
  );

  let headerWithX5tS256Match = structuredClone(testHeader);
  let signingCertificateHash = await crypto.subtle.digest(
    "SHA-256",
    new Uint8Array(kTLSCertificateBindingEE.getRawDER())
  );
  headerWithX5tS256Match["x5t#S256"] = QWACs.toBase64URLEncoding(
    arrayToString(new Uint8Array(signingCertificateHash))
  );
  ok(
    QWACs.validateTLSCertificateBindingHeader(headerWithX5tS256Match),
    "header with matching x5t#S256 should parse"
  );

  let headerWithX5tS256Mismatch = structuredClone(testHeader);
  headerWithX5tS256Mismatch["x5t#S256"] =
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithX5tS256Mismatch),
    "header with x5t#S256 mismatch should not parse"
  );

  let headerWithBadExp = structuredClone(testHeader);
  headerWithBadExp.exp = "not a number";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(headerWithBadExp),
    "header with bad expiration time should not parse"
  );

  let expiredHeader = structuredClone(testHeader);
  expiredHeader.exp = "946684800";
  ok(
    !QWACs.validateTLSCertificateBindingHeader(expiredHeader),
    "expired header should not parse"
  );

  let unexpiredHeader = structuredClone(testHeader);
  unexpiredHeader.exp = "4102444800";
  ok(
    QWACs.validateTLSCertificateBindingHeader(unexpiredHeader),
    "unexpired header should parse"
  );
});

async function validate_tls_certificate_binding_header_with_algorithms(
  signatureAlg,
  hashAlg,
  expectedSignatureAlg,
  expectedHashAlg
) {
  let serverCertificate = constructCertFromFile("bad_certs/default-ee.pem");
  let testHeader = await makeBindingHeader(
    [kTLSCertificateBindingEE],
    [serverCertificate],
    signatureAlg,
    hashAlg
  );
  let validatedHeader = QWACs.validateTLSCertificateBindingHeader(testHeader);
  ok(validatedHeader, "header should validate successfully");
  deepEqual(validatedHeader.algorithm, expectedSignatureAlg);
  equal(validatedHeader.certificates.length, 1);
  equal(validatedHeader.hashAlg, expectedHashAlg);
  equal(validatedHeader.hashes.length, 1);
}

add_task(
  async function test_validate_tls_certificate_binding_header_with_algorithms() {
    let options = [
      {
        signatureAlg: "RS256",
        hashAlg: "S256",
        expectedSignatureAlg: { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
        expectedHashAlg: "SHA-256",
      },
      {
        signatureAlg: "PS256",
        hashAlg: "S384",
        expectedSignatureAlg: {
          name: "RSA-PSS",
          saltLength: 32,
          hash: "SHA-256",
        },
        expectedHashAlg: "SHA-384",
      },
      {
        signatureAlg: "ES256",
        hashAlg: "S512",
        expectedSignatureAlg: {
          name: "ECDSA",
          namedCurve: "P-256",
          hash: "SHA-256",
        },
        expectedHashAlg: "SHA-512",
      },
    ];
    for (let option of options) {
      await validate_tls_certificate_binding_header_with_algorithms(
        option.signatureAlg,
        option.hashAlg,
        option.expectedSignatureAlg,
        option.expectedHashAlg
      );
    }
  }
);

async function sign(privateKeyInfo, algorithm, header) {
  let toSign = new Uint8Array(stringToArray(header + "."));
  let key = await crypto.subtle.importKey(
    "pkcs8",
    privateKeyInfo,
    algorithm,
    true,
    ["sign"]
  );
  let signature = await crypto.subtle.sign(algorithm, key, toSign);
  return (
    header +
    ".." +
    QWACs.toBase64URLEncoding(arrayToString(new Uint8Array(signature)))
  );
}

async function signTLSCertificateBinding(header, key, algorithm) {
  return sign(
    key,
    algorithm,
    QWACs.toBase64URLEncoding(JSON.stringify(header))
  );
}

async function verify_tls_certificate_binding_signature(
  headerSignatureAlgorithm,
  headerHashAlgorithm,
  signatureAlgorithm,
  signingKeyFilename,
  bindingCertificateFilename,
  serverCertificateFilename,
  presentedServerCertificateFilename,
  expectSuccess,
  expectedCertificateSubject
) {
  let signingKey = new Uint8Array(
    stringToArray(
      QWACs.fromBase64URLEncoding(
        pemToBase64(readFile(do_get_file(signingKeyFilename, false)))
      )
    )
  );
  let bindingCertificate = constructCertFromFile(bindingCertificateFilename);
  let serverCertificate = constructCertFromFile(serverCertificateFilename);
  let testHeader = await makeBindingHeader(
    [bindingCertificate],
    [serverCertificate],
    headerSignatureAlgorithm,
    headerHashAlgorithm
  );
  let binding = await signTLSCertificateBinding(
    testHeader,
    signingKey,
    signatureAlgorithm
  );
  let presentedServerCertificate = constructCertFromFile(
    presentedServerCertificateFilename
  );
  let qwac = await QWACs.verifyTLSCertificateBinding(
    binding,
    presentedServerCertificate,
    "example.com"
  );
  equal(
    !!qwac,
    expectSuccess,
    `TLS certificate binding ${expectSuccess ? "should" : "should not"} verify correctly`
  );
  if (expectSuccess) {
    equal(
      qwac.commonName,
      expectedCertificateSubject,
      "Verification should return the expected certificate"
    );
  }
}

add_task(async function test_verify_tls_certificate_binding_signatures() {
  let options = [
    {
      headerSignatureAlgorithm: "RS256",
      headerHashAlgorithm: "S512",
      signatureAlgorithm: { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
      signingKeyFilename: "bad_certs/default-ee.key",
      bindingCertificateFilename: "test_qwacs/2-qwac.pem",
      serverCertificateFilename: "bad_certs/default-ee.pem",
      presentedServerCertificateFilename: "bad_certs/default-ee.pem",
      expectSuccess: true,
      expectedCertificateSubject: "2-QWAC",
    },
    // presented server certificate / bound server certificate mismatch
    {
      headerSignatureAlgorithm: "RS256",
      headerHashAlgorithm: "S512",
      signatureAlgorithm: { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
      signingKeyFilename: "bad_certs/default-ee.key",
      bindingCertificateFilename: "test_qwacs/2-qwac.pem",
      serverCertificateFilename: "bad_certs/default-ee.pem",
      presentedServerCertificateFilename:
        "bad_certs/ee-from-missing-intermediate.pem",
      expectSuccess: false,
      expectedCertificateSubject: null,
    },
    {
      headerSignatureAlgorithm: "PS256",
      headerHashAlgorithm: "S256",
      signatureAlgorithm: {
        name: "RSA-PSS",
        hash: "SHA-256",
        saltLength: 32,
      },
      signingKeyFilename: "bad_certs/default-ee.key",
      bindingCertificateFilename: "test_qwacs/2-qwac.pem",
      serverCertificateFilename: "bad_certs/default-ee.pem",
      presentedServerCertificateFilename: "bad_certs/default-ee.pem",
      expectSuccess: true,
      expectedCertificateSubject: "2-QWAC",
    },
    {
      headerSignatureAlgorithm: "ES256",
      headerHashAlgorithm: "S384",
      signatureAlgorithm: {
        name: "ECDSA",
        namedCurve: "P-256",
        hash: "SHA-256",
      },
      signingKeyFilename: "test_qwacs/secp256r1.key",
      bindingCertificateFilename: "test_qwacs/2-qwac-ec.pem",
      serverCertificateFilename: "bad_certs/default-ee.pem",
      presentedServerCertificateFilename: "bad_certs/default-ee.pem",
      expectSuccess: true,
      expectedCertificateSubject: "2-QWAC with EC key",
    },
    // header signature algorithm / actual signature algorithm mismatch
    {
      headerSignatureAlgorithm: "RS256",
      headerHashAlgorithm: "S384",
      signatureAlgorithm: {
        name: "ECDSA",
        namedCurve: "P-256",
        hash: "SHA-256",
      },
      signingKeyFilename: "test_qwacs/secp256r1.key",
      bindingCertificateFilename: "test_qwacs/2-qwac-ec.pem",
      serverCertificateFilename: "bad_certs/default-ee.pem",
      presentedServerCertificateFilename: "bad_certs/default-ee.pem",
      expectSuccess: false,
      expectedCertificateSubject: null,
    },
  ];

  for (let option of options) {
    await verify_tls_certificate_binding_signature(
      option.headerSignatureAlgorithm,
      option.headerHashAlgorithm,
      option.signatureAlgorithm,
      option.signingKeyFilename,
      option.bindingCertificateFilename,
      option.serverCertificateFilename,
      option.presentedServerCertificateFilename,
      option.expectSuccess,
      option.expectedCertificateSubject
    );
  }
});
