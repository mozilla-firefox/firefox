/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  Certificate,
  ECNamedCurves,
  ECPublicKey,
  RSAPublicKey,
} from "./vendor/pkijs.js";
import { hexToIpv6Repr, b64ToPEM } from "./components/utils.mjs";

const getTimeZone = () => {
  let timeZone = new Date().toString().match(/\(([A-Za-z\s].*)\)/);
  if (timeZone === null) {
    // America/Chicago
    timeZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  } else if (timeZone.length > 1) {
    timeZone = timeZone[1]; // Central Daylight Time
  } else {
    timeZone = "Local Time"; // not sure if this is right, but let's go with it for now
  }
  return timeZone;
};

const getPublicKeyInfo = x509 => {
  let publicKey = x509.subjectPublicKeyInfo.parsedKey;
  if (publicKey instanceof RSAPublicKey) {
    let modulusJSON = publicKey.modulus.toJSON();
    let modulusHex = modulusJSON.valueBlock.valueHex;
    return {
      e: publicKey.publicExponent.toJSON().valueBlock.valueDec,
      kty: "RSA",
      n: hashify(modulusHex),
      keysize: modulusHex.length * 4, // key size in bits
    };
  }
  if (publicKey instanceof ECPublicKey) {
    let x = hashify(publicKey.x);
    let y = hashify(publicKey.y);
    let curve = ECNamedCurves.find(publicKey.namedCurve);
    let keysize = curve ? curve.size * 8 : undefined;
    return {
      kty: "Elliptic Curve",
      keysize,
      x, // x coordinate
      y, // y coordinate
      xy: `04:${x}:${y}`, // 04 (uncompressed) public key
    };
  }
  // pkijs only parses RSA and EC keys; resolve other algorithms by OID.
  let algorithmId = getObjPath(
    x509,
    "subjectPublicKeyInfo.algorithm.algorithmId"
  );
  if (algorithmId === "2.16.840.1.101.3.4.3.19") {
    let keyView = getObjPath(
      x509,
      "subjectPublicKeyInfo.subjectPublicKey.valueBlock.valueHexView"
    );
    let keyHex = keyView
      ? Array.from(keyView, b => b.toString(16).padStart(2, "0")).join("")
      : undefined;
    return {
      kty: "ML-DSA-87",
      keysize: keyView ? keyView.length * 8 : undefined,
      xy: keyHex ? hashify(keyHex) : undefined,
    };
  }
  return { kty: "Unknown" };
};

const getX509Ext = (extensions, v) => {
  for (var extension in extensions) {
    if (extensions[extension].extnID === v) {
      return extensions[extension].toJSON().parsedValue;
    }
  }
  return undefined;
};

const getKeyUsages = (x509, criticalExtensions) => {
  let keyUsages = {
    critical: criticalExtensions.includes("2.5.29.15"),
    purposes: [],
  };

  let keyUsagesExt = getX509Ext(x509.extensions, "2.5.29.15");
  if (keyUsagesExt !== undefined) {
    // zero pad or truncate the hex value to 4 digits
    let keyUsagesHex = keyUsagesExt.valueBlock.valueHex
      .slice(0, 4)
      .padEnd(4, "0");

    let keyUsagesInt = parseInt(keyUsagesHex, 16);

    // Clear any unused bits (accounting for padding or truncation above).
    let unusedBits = keyUsagesExt.valueBlock.unusedBits;
    if (keyUsagesExt.valueBlock.valueHex.length == 2) {
      unusedBits += 8;
    } else if (keyUsagesExt.valueBlock.valueHex.length > 4) {
      unusedBits = 0;
    }
    keyUsagesInt &= ~((1 << unusedBits) - 1);

    // iterate through the bit string
    strings.keyUsages.forEach(usage => {
      if (keyUsagesInt & 0x8000) {
        keyUsages.purposes.push(usage);
      }

      keyUsagesInt = keyUsagesInt << 1;
    });
  }

  return keyUsages;
};

const parseSubsidiary = distinguishedNames => {
  const subsidiary = {
    cn: "",
    dn: [],
    entries: [],
  };

  distinguishedNames.forEach(dn => {
    const distinguishedName = strings.names[dn.type];
    const value = dn.value.valueBlock.value;

    if (distinguishedName === undefined) {
      subsidiary.dn.push(`OID.${dn.type}=${value}`);
      subsidiary.entries.push([`OID.${dn.type}`, value]);
    } else if (distinguishedName.short === undefined) {
      subsidiary.dn.push(`OID.${dn.type}=${value}`);
      subsidiary.entries.push([distinguishedName.long, value]);
    } else {
      subsidiary.dn.push(`${distinguishedName.short}=${value}`);
      subsidiary.entries.push([distinguishedName.long, value]);

      // add the common name for tab display
      if (distinguishedName.short === "cn") {
        subsidiary.cn = value;
      }
    }
  });

  // turn path into a string
  subsidiary.dn = subsidiary.dn.join(", ");

  return subsidiary;
};

const getSubjectAltNames = (x509, criticalExtensions) => {
  let san = getX509Ext(x509.extensions, "2.5.29.17");
  if (san && san.hasOwnProperty("altNames")) {
    san = Object.keys(san.altNames).map(index => {
      const type = san.altNames[index].type;

      switch (type) {
        case 4: // directory
          return [
            strings.san[type],
            parseSubsidiary(san.altNames[index].value.typesAndValues).dn,
          ];
        case 7: {
          // ip address
          let address = san.altNames[index].value.valueBlock.valueHex;

          if (address.length === 8) {
            // ipv4
            return [
              strings.san[type],
              address
                .match(/.{1,2}/g)
                .map(x => parseInt(x, 16))
                .join("."),
            ];
          } else if (address.length === 32) {
            // ipv6
            return [strings.san[type], hexToIpv6Repr(address)];
          }
          return [strings.san[type], "Unknown IP address"];
        }
        default:
          return [strings.san[type], san.altNames[index].value];
      }
    });
  } else {
    san = [];
  }
  san = {
    altNames: san,
    critical: criticalExtensions.includes("2.5.29.17"),
  };
  return san;
};

const getBasicConstraints = (x509, criticalExtensions) => {
  let basicConstraints;
  const basicConstraintsExt = getX509Ext(x509.extensions, "2.5.29.19");
  if (basicConstraintsExt) {
    basicConstraints = {
      cA: basicConstraintsExt.cA !== undefined && basicConstraintsExt.cA,
      critical: criticalExtensions.includes("2.5.29.19"),
    };
  }
  return basicConstraints;
};

const getEKeyUsages = (x509, criticalExtensions) => {
  let eKeyUsages = getX509Ext(x509.extensions, "2.5.29.37");
  if (eKeyUsages) {
    eKeyUsages = {
      critical: criticalExtensions.includes("2.5.29.37"),
      purposes: eKeyUsages.keyPurposes.map(x => strings.eKU[x] || x),
    };
  }
  return eKeyUsages;
};

const getSubjectKeyID = (x509, criticalExtensions) => {
  let sKID = getX509Ext(x509.extensions, "2.5.29.14");
  if (sKID) {
    sKID = {
      critical: criticalExtensions.includes("2.5.29.14"),
      id: hashify(sKID.valueBlock.valueHex),
    };
  }
  return sKID;
};

const getAuthorityKeyID = (x509, criticalExtensions) => {
  let aKID = getX509Ext(x509.extensions, "2.5.29.35");
  if (!aKID || !aKID.keyIdentifier) {
    return null;
  }
  aKID = {
    critical: criticalExtensions.includes("2.5.29.35"),
    id: hashify(aKID.keyIdentifier.valueBlock.valueHex),
  };
  return aKID;
};

const getCRLPoints = (x509, criticalExtensions) => {
  let crlPoints = getX509Ext(x509.extensions, "2.5.29.31");
  if (crlPoints) {
    crlPoints = {
      critical: criticalExtensions.includes("2.5.29.31"),
      points: crlPoints.distributionPoints.map(
        x => x.distributionPoint[0].value
      ),
    };
  }
  return crlPoints;
};

const getOcspStaple = (x509, criticalExtensions) => {
  let ocspStaple = getX509Ext(x509.extensions, "1.3.6.1.5.5.7.1.24");
  if (ocspStaple && ocspStaple.valueBeforeDecode === "3003020105") {
    ocspStaple = {
      critical: criticalExtensions.includes("1.3.6.1.5.5.7.1.24"),
      required: true,
    };
  } else {
    ocspStaple = {
      critical: criticalExtensions.includes("1.3.6.1.5.5.7.1.24"),
      required: false,
    };
  }
  return ocspStaple;
};

const getAuthorityInfoAccess = (x509, criticalExtensions) => {
  let aia = getX509Ext(x509.extensions, "1.3.6.1.5.5.7.1.1");
  if (aia) {
    aia = aia.accessDescriptions.map(x => {
      return {
        location: x.accessLocation.value,
        method: strings.aia[x.accessMethod],
      };
    });
  }

  aia = {
    descriptions: aia,
    critical: criticalExtensions.includes("1.3.6.1.5.5.7.1.1"),
  };
  return aia;
};

const getSCTs = (x509, criticalExtensions) => {
  let scts = getX509Ext(x509.extensions, "1.3.6.1.4.1.11129.2.4.2");
  if (scts) {
    scts = Object.keys(scts.timestamps).map(x => {
      let logId = scts.timestamps[x].logID.toLowerCase();
      let sctsTimestamp = scts.timestamps[x].timestamp;
      return {
        logId: hashify(logId),
        name: ctLogNames.hasOwnProperty(logId) ? ctLogNames[logId] : undefined,
        signatureAlgorithm: `${scts.timestamps[x].hashAlgorithm.replace(
          "sha",
          "SHA-"
        )} ${scts.timestamps[x].signatureAlgorithm.toUpperCase()}`,
        timestamp: `${sctsTimestamp.toLocaleString()} (${getTimeZone()})`,
        timestampUTC: sctsTimestamp.toUTCString(),
        version: scts.timestamps[x].version + 1,
      };
    });
  } else {
    scts = [];
  }

  scts = {
    critical: criticalExtensions.includes("1.3.6.1.4.1.11129.2.4.2"),
    timestamps: scts,
  };
  return scts;
};

const getCertificatePolicies = (x509, criticalExtensions) => {
  let cp = getX509Ext(x509.extensions, "2.5.29.32");
  if (cp && cp.hasOwnProperty("certificatePolicies")) {
    cp = cp.certificatePolicies.map(x => {
      let id = x.policyIdentifier;
      let certName = strings.cps.hasOwnProperty(id)
        ? strings.cps[id].name
        : undefined;
      let qualifiers = undefined;
      let value = strings.cps.hasOwnProperty(id)
        ? strings.cps[id].value
        : undefined;

      // ansi organization identifiers
      if (id.startsWith("2.16.840.")) {
        value = id;
        id = "2.16.840";
        certName = strings.cps["2.16.840"].name;
      }

      // statement identifiers
      if (id.startsWith("1.3.6.1.4.1")) {
        value = id;
        id = "1.3.6.1.4.1";
        certName = strings.cps["1.3.6.1.4.1"].name;
      }

      if (x.hasOwnProperty("policyQualifiers")) {
        qualifiers = x.policyQualifiers.map(qualifier => {
          let qualifierId = qualifier.policyQualifierId;
          let qualifierName = strings.cps.hasOwnProperty(qualifierId)
            ? strings.cps[qualifierId].name
            : undefined;
          let qualifierValue = qualifier.qualifier.valueBlock.value;

          // sometimes they are multiple qualifier subblocks, and for now we'll
          // only return the first one because it's getting really messy at this point
          if (Array.isArray(qualifierValue) && qualifierValue.length === 1) {
            qualifierValue = qualifierValue[0].valueBlock.value;
          } else if (
            Array.isArray(qualifierValue) &&
            qualifierValue.length > 1
          ) {
            qualifierValue = "(currently unsupported)";
          }

          return {
            qualifierId,
            qualifierName,
            qualifierValue,
          };
        });
      }

      return {
        id,
        name: certName,
        qualifiers,
        value,
      };
    });
  }

  cp = {
    critical: criticalExtensions.includes("2.5.29.32"),
    policies: cp,
  };
  return cp;
};

const getMicrosoftCryptographicExtensions = (x509, criticalExtensions) => {
  // now let's parse the Microsoft cryptographic extensions
  let msCrypto = {
    caVersion: getX509Ext(x509.extensions, "1.3.6.1.4.1.311.21.1"),
    certificatePolicies: getX509Ext(x509.extensions, "1.3.6.1.4.1.311.21.10"),
    certificateTemplate: getX509Ext(x509.extensions, "1.3.6.1.4.1.311.21.7"),
    certificateType: getX509Ext(x509.extensions, "1.3.6.1.4.1.311.20.2"),
    previousHash: getX509Ext(x509.extensions, "1.3.6.1.4.1.311.21.2"),
  };

  if (
    msCrypto.caVersion &&
    Number.isInteger(msCrypto.caVersion.keyIndex) &&
    Number.isInteger(msCrypto.caVersion.certificateIndex)
  ) {
    msCrypto.caVersion = {
      critical: criticalExtensions.includes("1.3.6.1.4.1.311.21.1"),
      caRenewals: msCrypto.caVersion.certificateIndex,
      keyReuses:
        msCrypto.caVersion.certificateIndex - msCrypto.caVersion.keyIndex,
    };
  }

  if (msCrypto.certificatePolicies) {
    msCrypto.certificatePolicies = {
      critical: criticalExtensions.includes("1.3.6.1.4.1.311.21.10"),
      purposes: msCrypto.certificatePolicies.certificatePolicies.map(
        x => strings.eKU[x.policyIdentifier] || x.policyIdentifier
      ),
    };
  }

  if (msCrypto.certificateTemplate) {
    msCrypto.certificateTemplate = {
      critical: criticalExtensions.includes("1.3.6.1.4.1.311.21.7"),
      id: msCrypto.certificateTemplate.extnID,
      major: msCrypto.certificateTemplate.templateMajorVersion,
      minor: msCrypto.certificateTemplate.templateMinorVersion,
    };
  }

  if (msCrypto.certificateType) {
    msCrypto.certificateType = {
      critical: criticalExtensions.includes("1.3.6.1.4.1.311.20.2"),
      type:
        strings.microsoftCertificateTypes[
          msCrypto.certificateType.valueBlock.value
        ] || "Unknown",
    };
  }

  if (msCrypto.previousHash) {
    msCrypto.previousHash = {
      critical: criticalExtensions.includes("1.3.6.1.4.1.311.21.2"),
      previousHash: hashify(msCrypto.previousHash.valueBlock.valueHex),
    };
  }

  msCrypto.exists = !!(
    msCrypto.caVersion ||
    msCrypto.certificatePolicies ||
    msCrypto.certificateTemplate ||
    msCrypto.certificateType ||
    msCrypto.previousHash
  );

  return msCrypto;
};

export const parse = async certificate => {
  // certificate could be an array of BER or an array of buffers
  const supportedExtensions = [
    "1.3.6.1.4.1.311.20.2", // microsoft certificate type
    "1.3.6.1.4.1.311.21.2", // microsoft certificate previous hash
    "1.3.6.1.4.1.311.21.7", // microsoft certificate template
    "1.3.6.1.4.1.311.21.1", // microsoft certification authority renewal
    "1.3.6.1.4.1.311.21.10", // microsoft certificate policies
    "1.3.6.1.4.1.11129.2.4.2", // embedded scts
    "1.3.6.1.5.5.7.1.1", // authority info access
    "1.3.6.1.5.5.7.1.24", // ocsp stapling
    "1.3.101.77", // ct redaction - deprecated and not displayed
    "2.5.29.14", // subject key identifier
    "2.5.29.15", // key usages
    "2.5.29.17", // subject alt names
    "2.5.29.19", // basic constraints
    "2.5.29.31", // crl points
    "2.5.29.32", // certificate policies
    "2.5.29.35", // authority key identifier
    "2.5.29.37", // extended key usage
  ];

  let timeZone = getTimeZone();

  // parse the certificate
  let x509 = Certificate.fromBER(certificate);

  // convert the cert to PEM
  const certPEM = b64ToPEM(
    btoa(String.fromCharCode.apply(null, new Uint8Array(certificate)))
  );

  // get which extensions are critical
  const criticalExtensions = [];
  if (x509.extensions) {
    x509.extensions.forEach(ext => {
      if (ext.hasOwnProperty("critical") && ext.critical === true) {
        criticalExtensions.push(ext.extnID);
      }
    });
  }
  const spki = getPublicKeyInfo(x509);
  const keyUsages = getKeyUsages(x509, criticalExtensions);
  const san = getSubjectAltNames(x509, criticalExtensions);
  const basicConstraints = getBasicConstraints(x509, criticalExtensions);
  const eKeyUsages = getEKeyUsages(x509, criticalExtensions);
  const sKID = getSubjectKeyID(x509, criticalExtensions);
  const aKID = getAuthorityKeyID(x509, criticalExtensions);
  const crlPoints = getCRLPoints(x509, criticalExtensions);
  const ocspStaple = getOcspStaple(x509, criticalExtensions);
  const aia = getAuthorityInfoAccess(x509, criticalExtensions);
  const scts = getSCTs(x509, criticalExtensions);
  const cp = getCertificatePolicies(x509, criticalExtensions);
  const msCrypto = getMicrosoftCryptographicExtensions(
    x509,
    criticalExtensions
  );

  // determine which extensions weren't supported
  let unsupportedExtensions = [];
  if (x509.extensions) {
    x509.extensions.forEach(ext => {
      if (!supportedExtensions.includes(ext.extnID)) {
        unsupportedExtensions.push(ext.extnID);
      }
    });
  }

  // the output shell
  return {
    ext: {
      aia,
      aKID,
      basicConstraints,
      crlPoints,
      cp,
      eKeyUsages,
      keyUsages,
      msCrypto,
      ocspStaple,
      scts,
      sKID,
      san,
    },
    files: {
      der: undefined, // TODO: implement!
      pem: encodeURI(certPEM),
    },
    fingerprint: {
      sha1: await hash("SHA-1", certificate),
      sha256: await hash("SHA-256", certificate),
    },
    issuer: parseSubsidiary(x509.issuer.typesAndValues),
    notBefore: `${x509.notBefore.value.toLocaleString()} (${timeZone})`,
    notBeforeUTC: x509.notBefore.value.toUTCString(),
    notAfter: `${x509.notAfter.value.toLocaleString()} (${timeZone})`,
    notAfterUTC: x509.notAfter.value.toUTCString(),
    subject: parseSubsidiary(x509.subject.typesAndValues),
    serialNumber: hashify(getObjPath(x509, "serialNumber.valueBlock.valueHex")),
    signature: {
      name: strings.signature[getObjPath(x509, "signature.algorithmId")],
      type: getObjPath(x509, "signature.algorithmId"),
    },
    subjectPublicKeyInfo: spki,
    unsupportedExtensions,
    version: (x509.version + 1).toString(),
  };
};

const ctLogNames = {
  "9606c02c690033aa1d145f59c6e2648d0549f0df96aab8db915a70d8ecf390a5":
    "Akamai CT",
  "39376f545f7b4607f59742d768cd5d2437bf3473b6534a4834bcf72e681c83c9":
    "Alpha CT",
  a577ac9ced7548dd8f025b67a241089df86e0f476ec203c2ecbedb185f282638: "CNNIC CT",
  cdb5179b7fc1c046feea31136a3f8f002e6182faf8896fecc8b2f5b5ab604900: "Certly.IO",
  "1fbc36e002ede97f40199e86b3573b8a4217d80187746ad0da03a06054d20df4":
    "Cloudflare “Nimbus2017”",
  db74afeecb29ecb1feca3e716d2ce5b9aabb36f7847183c75d9d4f37b61fbf64:
    "Cloudflare “Nimbus2018”",
  "747eda8331ad331091219cce254f4270c2bffd5e422008c6373579e6107bcc56":
    "Cloudflare “Nimbus2019”",
  "5ea773f9df56c0e7b536487dd049e0327a919a0c84a112128418759681714558":
    "Cloudflare “Nimbus2020”",
  "4494652eb0eeceafc44007d8a8fe28c0dae682bed8cb31b53fd33396b5b681a8":
    "Cloudflare “Nimbus2021”",
  "41c8cab1df22464a10c6a13a0942875e4e318b1b03ebeb4bc768f090629606f6":
    "Cloudflare “Nimbus2022”",
  "7a328c54d8b72db620ea38e0521ee98416703213854d3bd22bc13a57a352eb52":
    "Cloudflare “Nimbus2023”",
  "6ff141b5647e4222f7ef052cefae7c21fd608e27d2af5a6e9f4b8a37d6633ee5":
    "DigiCert Nessie2018",
  fe446108b1d01ab78a62ccfeab6ab2b2babff3abdad80a4d8b30df2d0008830c:
    "DigiCert Nessie2019",
  c652a0ec48ceb3fcab170992c43a87413309e80065a26252401ba3362a17c565:
    "DigiCert Nessie2020",
  eec095ee8d72640f92e3c3b91bc712a3696a097b4b6a1a1438e647b2cbedc5f9:
    "DigiCert Nessie2021",
  "51a3b0f5fd01799c566db837788f0ca47acc1b27cbf79e88429a0dfed48b05e5":
    "DigiCert Nessie2022",
  b3737707e18450f86386d605a9dc11094a792db1670c0b87dcf0030e7936a59a:
    "DigiCert Nessie2023",
  "5614069a2fd7c2ecd3f5e1bd44b23ec74676b9bc99115cc0ef949855d689d0dd":
    "DigiCert Server",
  "8775bfe7597cf88c43995fbdf36eff568d475636ff4ab560c1b4eaff5ea0830f":
    "DigiCert Server 2",
  c1164ae0a772d2d4392dc80ac10770d4f0c49bde991a4840c1fa075164f63360:
    "DigiCert Yeti2018",
  e2694bae26e8e94009e8861bb63b83d43ee7fe7488fba48f2893019dddf1dbfe:
    "DigiCert Yeti2019",
  f095a459f200d18240102d2f93888ead4bfe1d47e399e1d034a6b0a8aa8eb273:
    "DigiCert Yeti2020",
  "5cdc4392fee6ab4544b15e9ad456e61037fbd5fa47dca17394b25ee6f6c70eca":
    "DigiCert Yeti2021",
  "2245450759552456963fa12ff1f76d86e0232663adc04b7f5dc6835c6ee20f02":
    "DigiCert Yeti2022",
  "35cf191bbfb16c57bf0fad4c6d42cbbbb627202651ea3fe12aefa803c33bd64c":
    "DigiCert Yeti2023",
  "717ea7420975be84a2723553f1777c26dd51af4e102144094d9019b462fb6668": "GDCA 1",
  "14308d90ccd030135005c01ca526d81e84e87624e39b6248e08f724aea3bb42a": "GDCA 2",
  c9cf890a21109c666cc17a3ed065c930d0e0135a9feba85af14210b8072421aa:
    "GDCA CT #1",
  "924a30f909336ff435d6993a10ac75a2c641728e7fc2d659ae6188ffad40ce01":
    "GDCA CT #2",
  fad4c97cc49ee2f8ac85c5ea5cea09d0220dbbf4e49c6b50662ff868f86b8c28:
    "Google “Argon2017”",
  a4501269055a15545e6211ab37bc103f62ae5576a45e4b1714453e1b22106a25:
    "Google “Argon2018”",
  "63f2dbcde83bcc2ccf0b728427576b33a48d61778fbd75a638b1c768544bd88d":
    "Google “Argon2019”",
  b21e05cc8ba2cd8a204e8766f92bb98a2520676bdafa70e7b249532def8b905e:
    "Google “Argon2020”",
  f65c942fd1773022145418083094568ee34d131933bfdf0c2f200bcc4ef164e3:
    "Google “Argon2021”",
  "2979bef09e393921f056739f63a577e5be577d9c600af8f94d5d265c255dc784":
    "Google “Argon2022”",
  "68f698f81f6482be3a8ceeb9281d4cfc71515d6793d444d10a67acbb4f4ffbc4":
    "Google “Aviator”",
  c3bf03a7e1ca8841c607bae3ff4270fca5ec45b186ebbe4e2cf3fc778630f5f6:
    "Google “Crucible”",
  "1d024b8eb1498b344dfd87ea3efc0996f7506f235d1d497061a4773c439c25fb":
    "Google “Daedalus”",
  "293c519654c83965baaa50fc5807d4b76fbf587a2972dca4c30cf4e54547f478":
    "Google “Icarus”",
  a4b90990b418581487bb13a2cc67700a3c359804f91bdfb8e377cd0ec80ddc10:
    "Google “Pilot”",
  ee4bbdb775ce60bae142691fabe19e66a30f7e5fb072d88300c47b897aa8fdcb:
    "Google “Rocketeer”",
  bbd9dfbc1f8a71b593942397aa927b473857950aab52e81a909664368e1ed185:
    "Google “Skydiver”",
  "52eb4b225ec896974850675f23e43bc1d021e3214ce52ecd5fa87c203cdfca03":
    "Google “Solera2018”",
  "0b760e9a8b9a682f88985b15e947501a56446bba8830785c3842994386450c00":
    "Google “Solera2019”",
  "1fc72ce5a1b799f400c359bff96ca3913548e8644220610952e9ba1774f7bac7":
    "Google “Solera2020”",
  a3c99845e80ab7ce00157b3742df0207dd272b2b602ecf98ee2c12db9c5ae7e7:
    "Google “Solera2021”",
  "697aafca1a6b536fae21205046debad7e0eaea13d2432e6e9d8fb379f2b9aaf3":
    "Google “Solera2022”",
  a899d8780c9290aaf462f31880ccfbd52451e970d0fbf591ef75b0d99b645681:
    "Google “Submariner”",
  b0cc83e5a5f97d6baf7c09cc284904872ac7e88b132c6350b7c6fd26e16c6c77:
    "Google “Testtube”",
  b10cd559a6d67846811f7df9a51532739ac48d703bea0323da5d38755bc0ad4e:
    "Google “Xenon2018”",
  "084114980071532c16190460bcfc47fdc2653afa292c72b37ff863ae29ccc9f0":
    "Google “Xenon2019”",
  "07b75c1be57d68fff1b0c61d2315c7bae6577c5794b76aeebc613a1a69d3a21c":
    "Google “Xenon2020”",
  "7d3ef2f88fff88556824c2c0ca9e5289792bc50e78097f2e6a9768997e22f0d7":
    "Google “Xenon2021”",
  "46a555eb75fa912030b5a28969f4f37d112c4174befd49b885abf2fc70fe6d47":
    "Google “Xenon2022”",
  "7461b4a09cfb3d41d75159575b2e7649a445a8d27709b0cc564a6482b7eb41a3": "Izenpe",
  "8941449c70742e06b9fc9ce7b116ba0024aa36d59af44f0204404f00f7ea8566":
    "Izenpe “Argi”",
  "296afa2d568bca0d2ea844956ae9721fc35fa355ecda99693aafd458a71aefdd":
    "Let“s Encrypt ”Clicky”",
  "537b69a3564335a9c04904e39593b2c298eb8d7a6e83023635c627248cd6b440":
    "Nordu “flimsy”",
  aae70b7f3cb8d566c86c2f16979c9f445f69ab0eb4535589b2f77a030104f3cd:
    "Nordu “plausible”",
  e0127629e90496564e3d0147984498aa48f8adb16600eb7902a1ef9909906273:
    "PuChuangSiDa CT",
  cf55e28923497c340d5206d05353aeb25834b52f1f8dc9526809f212efdd7ca6:
    "SHECA CT 1",
  "32dc59c2d4c41968d56e14bc61ac8f0e45db39faf3c155aa4252f5001fa0c623":
    "SHECA CT 2",
  db76fdadac65e7d09508886e2159bd8b90352f5fead3e3dc5e22eb350acc7b98:
    "Sectigo (Comodo) “Dodo” CT",
  "6f5376ac31f03119d89900a45115ff77151c11d902c10029068db2089a37d913":
    "Sectigo (Comodo) “Mammoth” CT",
  "5581d4c2169036014aea0b9b573c53f0c0e43878702508172fa3aa1d0713d30c":
    "Sectigo (Comodo) “Sabre” CT",
  "34bb6ad6c3df9c03eea8a499ff7891486c9d5e5cac92d01f7bfd1bce19db48ef":
    "StartCom",
  ddeb1d2b7a0d4fa6208b81ad8168707e2e8e9d01d55c888d3d11c4cdb6ecbecc: "Symantec",
  a7ce4a4e6207e0addee5fdaa4b1f86768767b5d002a55d47310e7e670a95eab2:
    "Symantec Deneb",
  "15970488d7b997a05beb52512adee8d2e8b4a3165264121a9fabfbd5f85ad93f":
    "Symantec “Sirius”",
  bc78e1dfc5f63c684649334da10fa15f0979692009c081b4f3f6917f3ed9b8a5:
    "Symantec “Vega”",
  b0b784bc81c0ddc47544e883f05985bb9077d134d8ab88b2b2e533980b8e508b:
    "Up In The Air “Behind the Sofa”",
  ac3b9aed7fa9674757159e6d7d575672f9d98100941e9bdeffeca1313b75782d: "Venafi",
  "03019df3fd85a69a8ebd1facc6da9ba73e469774fe77f579fc5a08b8328c1d6b":
    "Venafi Gen2 CT",
  "41b2dc2e89e63ce4af1ba7bb29bf68c6dee6f9f1cc047e30dffae3b3ba259263": "WoSign",
  "63d0006026dde10bb0601f452446965ee2b6ea2cd4fbc95ac866a550af9075b7":
    "WoSign 2",
  "9e4ff73dc3ce220b69217c899e468076abf8d78636d5ccfc85a31a75628ba88b":
    "WoSign CT #1",
  "659b3350f43b12cc5ea5ab4ec765d3fde6c88243777778e72003f9eb2b8c3129":
    "Let's Encrypt Oak 2019",
  e712f2b0377e1a62fb8ec90c6184f1ea7b37cb561d11265bf3e0f34bf241546e:
    "Let's Encrypt Oak 2020",
  "9420bc1e8ed58d6c88731f828b222c0dd1da4d5e6c4f943d61db4e2f584da2c2":
    "Let's Encrypt Oak 2021",
  dfa55eab68824f1f6cadeeb85f4e3e5aeacda212a46a5e8e3b12c020445c2a73:
    "Let's Encrypt Oak 2022",
  b73efb24df9c4dba75f239c5ba58f46c5dfc42cf7a9f35c49e1d098125edb499:
    "Let's Encrypt Oak 2023",
  "849f5f7f58d2bf7b54ecbd74611cea45c49c98f1d6481bc6f69e8c174f24f3cf":
    "Let's Encrypt Testflume 2019",
  c63f2218c37d56a6aa06b596da8e53d4d7156d1e9bac8e44d2202de64d69d9dc:
    "Let's Encrypt Testflume 2020",
  "03edf1da9776b6f38c341e39ed9d707a7570369cf9844f327fe9e14138361b60":
    "Let's Encrypt Testflume 2021",
  "2327efda352510dbc019ef491ae3ff1cc5a479bce37878360ee318cffb64f8c8":
    "Let's Encrypt Testflume 2022",
  "5534b7ab5a6ac3a7cbeba65487b2a2d71b48f650fa17c5197c97a0cb2076f3c6":
    "Let's Encrypt Testflume 2023",
};

const strings = {
  ux: {
    upload: "Upload Certificate",
  },

  names: {
    // Directory Pilot Attributes
    "0.9.2342.19200300.100.1.1": {
      short: "uid",
      long: "User ID",
    },
    "0.9.2342.19200300.100.1.25": {
      short: "dc",
      long: "Domain Component",
    },

    // PKCS-9
    "1.2.840.113549.1.9.1": {
      short: "e",
      long: "Email Address",
    },

    // Incorporated Locations
    "1.3.6.1.4.1.311.60.2.1.1": {
      short: undefined,
      long: "Inc. Locality",
    },
    "1.3.6.1.4.1.311.60.2.1.2": {
      short: undefined,
      long: "Inc. State / Province",
    },
    "1.3.6.1.4.1.311.60.2.1.3": {
      short: undefined,
      long: "Inc. Country",
    },

    // microsoft cryptographic extensions
    "1.3.6.1.4.1.311.21.7": {
      name: {
        short: "Certificate Template",
        long: "Microsoft Certificate Template",
      },
    },
    "1.3.6.1.4.1.311.21.10": {
      name: {
        short: "Certificate Policies",
        long: "Microsoft Certificate Policies",
      },
    },

    // certificate extensions
    "1.3.6.1.4.1.11129.2.4.2": {
      name: {
        short: "Embedded SCTs",
        long: "Embedded Signed Certificate Timestamps",
      },
    },
    "1.3.6.1.5.5.7.1.1": {
      name: {
        short: undefined,
        long: "Authority Information Access",
      },
    },
    "1.3.6.1.5.5.7.1.24": {
      name: {
        short: "OCSP Stapling",
        long: "Online Certificate Status Protocol Stapling",
      },
    },

    // X.500 attribute types
    "2.5.4.1": {
      short: undefined,
      long: "Aliased Entry",
    },
    "2.5.4.2": {
      short: undefined,
      long: "Knowledge Information",
    },
    "2.5.4.3": {
      short: "cn",
      long: "Common Name",
    },
    "2.5.4.4": {
      short: "sn",
      long: "Surname",
    },
    "2.5.4.5": {
      short: "serialNumber",
      long: "Serial Number",
    },
    "2.5.4.6": {
      short: "c",
      long: "Country",
    },
    "2.5.4.7": {
      short: "l",
      long: "Locality",
    },
    "2.5.4.8": {
      short: "s",
      long: "State / Province",
    },
    "2.5.4.9": {
      short: "street",
      long: "Stress Address",
    },
    "2.5.4.10": {
      short: "o",
      long: "Organization",
    },
    "2.5.4.11": {
      short: "ou",
      long: "Organizational Unit",
    },
    "2.5.4.12": {
      short: "t",
      long: "Title",
    },
    "2.5.4.13": {
      short: "description",
      long: "Description",
    },
    "2.5.4.14": {
      short: undefined,
      long: "Search Guide",
    },
    "2.5.4.15": {
      short: undefined,
      long: "Business Category",
    },
    "2.5.4.16": {
      short: undefined,
      long: "Postal Address",
    },
    "2.5.4.17": {
      short: "postalCode",
      long: "Postal Code",
    },
    "2.5.4.18": {
      short: "POBox",
      long: "PO Box",
    },
    "2.5.4.19": {
      short: undefined,
      long: "Physical Delivery Office Name",
    },
    "2.5.4.20": {
      short: "phone",
      long: "Phone Number",
    },
    "2.5.4.21": {
      short: undefined,
      long: "Telex Number",
    },
    "2.5.4.22": {
      short: undefined,
      long: "Teletex Terminal Identifier",
    },
    "2.5.4.23": {
      short: undefined,
      long: "Fax Number",
    },
    "2.5.4.24": {
      short: undefined,
      long: "X.121 Address",
    },
    "2.5.4.25": {
      short: undefined,
      long: "International ISDN Number",
    },
    "2.5.4.26": {
      short: undefined,
      long: "Registered Address",
    },
    "2.5.4.27": {
      short: undefined,
      long: "Destination Indicator",
    },
    "2.5.4.28": {
      short: undefined,
      long: "Preferred Delivery Method",
    },
    "2.5.4.29": {
      short: undefined,
      long: "Presentation Address",
    },
    "2.5.4.30": {
      short: undefined,
      long: "Supported Application Context",
    },
    "2.5.4.31": {
      short: undefined,
      long: "Member",
    },
    "2.5.4.32": {
      short: undefined,
      long: "Owner",
    },
    "2.5.4.33": {
      short: undefined,
      long: "Role Occupant",
    },
    "2.5.4.34": {
      short: undefined,
      long: "See Also",
    },
    "2.5.4.35": {
      short: undefined,
      long: "User Password",
    },
    "2.5.4.36": {
      short: undefined,
      long: "User Certificate",
    },
    "2.5.4.37": {
      short: undefined,
      long: "CA Certificate",
    },
    "2.5.4.38": {
      short: undefined,
      long: "Authority Revocation List",
    },
    "2.5.4.39": {
      short: undefined,
      long: "Certificate Revocation List",
    },
    "2.5.4.40": {
      short: undefined,
      long: "Cross-certificate Pair",
    },
    "2.5.4.41": {
      short: undefined,
      long: "Name",
    },
    "2.5.4.42": {
      short: "g",
      long: "Given Name",
    },
    "2.5.4.43": {
      short: "i",
      long: "Initials",
    },
    "2.5.4.44": {
      short: undefined,
      long: "Generation Qualifier",
    },
    "2.5.4.45": {
      short: undefined,
      long: "Unique Identifier",
    },
    "2.5.4.46": {
      short: undefined,
      long: "DN Qualifier",
    },
    "2.5.4.47": {
      short: undefined,
      long: "Enhanced Search Guide",
    },
    "2.5.4.48": {
      short: undefined,
      long: "Protocol Information",
    },
    "2.5.4.49": {
      short: "dn",
      long: "Distinguished Name",
    },
    "2.5.4.50": {
      short: undefined,
      long: "Unique Member",
    },
    "2.5.4.51": {
      short: undefined,
      long: "House Identifier",
    },
    "2.5.4.52": {
      short: undefined,
      long: "Supported Algorithms",
    },
    "2.5.4.53": {
      short: undefined,
      long: "Delta Revocation List",
    },
    "2.5.4.58": {
      short: undefined,
      long: "Attribute Certificate Attribute", // huh
    },
    "2.5.4.65": {
      short: undefined,
      long: "Pseudonym",
    },

    // extensions
    "2.5.29.14": {
      name: {
        short: "Subject Key ID",
        long: "Subject Key Identifier",
      },
    },
    "2.5.29.15": {
      name: {
        short: undefined,
        long: "Key Usages",
      },
    },
    "2.5.29.17": {
      name: {
        short: "Subject Alt Names",
        long: "Subject Alternative Names",
      },
    },
    "2.5.29.19": {
      name: {
        short: undefined,
        long: "Basic Constraints",
      },
    },
    "2.5.29.31": {
      name: {
        short: "CRL Endpoints",
        long: "Certificate Revocation List Endpoints",
      },
    },
    "2.5.29.32": {
      name: {
        short: undefined,
        long: "Certificate Policies",
      },
    },
    "2.5.29.35": {
      name: {
        short: "Authority Key ID",
        long: "Authority Key Identifier",
      },
    },
    "2.5.29.37": {
      name: {
        short: undefined,
        long: "Extended Key Usages",
      },
    },
  },

  keyUsages: [
    "Digital Signature",
    "Non-Repudiation",
    "Key Encipherment",
    "Data Encipherment",
    "Key Agreement",
    "Certificate Signing",
    "CRL Signing",
    "Encipher Only",
    "Decipher Only",
  ],

  san: [
    "Other Name",
    "RFC 822 Name",
    "DNS Name",
    "X.400 Address",
    "Directory Name",
    "EDI Party Name",
    "URI",
    "IP Address",
    "Registered ID",
  ],

  eKU: {
    "1.3.6.1.4.1.311.10.3.1": "Certificate Trust List (CTL) Signing",
    "1.3.6.1.4.1.311.10.3.2": "Timestamp Signing",
    "1.3.6.1.4.1.311.10.3.4": "EFS Encryption",
    "1.3.6.1.4.1.311.10.3.4.1": "EFS Recovery",
    "1.3.6.1.4.1.311.10.3.5":
      "Windows Hardware Quality Labs (WHQL) Cryptography",
    "1.3.6.1.4.1.311.10.3.7": "Windows NT 5 Cryptography",
    "1.3.6.1.4.1.311.10.3.8": "Windows NT Embedded Cryptography",
    "1.3.6.1.4.1.311.10.3.10": "Qualified Subordination",
    "1.3.6.1.4.1.311.10.3.11": "Escrowed Key Recovery",
    "1.3.6.1.4.1.311.10.3.12": "Document Signing",
    "1.3.6.1.4.1.311.10.5.1": "Digital Rights Management",
    "1.3.6.1.4.1.311.10.6.1": "Key Pack Licenses",
    "1.3.6.1.4.1.311.10.6.2": "License Server",
    "1.3.6.1.4.1.311.20.2.1": "Enrollment Agent",
    "1.3.6.1.4.1.311.20.2.2": "Smartcard Login",
    "1.3.6.1.4.1.311.21.5": "Certificate Authority Private Key Archival",
    "1.3.6.1.4.1.311.21.6": "Key Recovery Agent",
    "1.3.6.1.4.1.311.21.19": "Directory Service Email Replication",
    "1.3.6.1.5.5.7.3.1": "Server Authentication",
    "1.3.6.1.5.5.7.3.2": "Client Authentication",
    "1.3.6.1.5.5.7.3.3": "Code Signing",
    "1.3.6.1.5.5.7.3.4": "E-mail Protection",
    "1.3.6.1.5.5.7.3.5": "IPsec End System",
    "1.3.6.1.5.5.7.3.6": "IPsec Tunnel",
    "1.3.6.1.5.5.7.3.7": "IPSec User",
    "1.3.6.1.5.5.7.3.8": "Timestamping",
    "1.3.6.1.5.5.7.3.9": "OCSP Signing",
    "1.3.6.1.5.5.8.2.2": "Internet Key Exchange (IKE)",
  },

  signature: {
    "1.2.840.113549.1.1.4": "MD5 with RSA Encryption",
    "1.2.840.113549.1.1.5": "SHA-1 with RSA Encryption",
    "1.2.840.113549.1.1.11": "SHA-256 with RSA Encryption",
    "1.2.840.113549.1.1.12": "SHA-384 with RSA Encryption",
    "1.2.840.113549.1.1.13": "SHA-512 with RSA Encryption",
    "1.2.840.10040.4.3": "DSA with SHA-1",
    "2.16.840.1.101.3.4.3.2": "DSA with SHA-256",
    "1.2.840.10045.4.1": "ECDSA with SHA-1",
    "1.2.840.10045.4.3.2": "ECDSA with SHA-256",
    "1.2.840.10045.4.3.3": "ECDSA with SHA-384",
    "1.2.840.10045.4.3.4": "ECDSA with SHA-512",
    "2.16.840.1.101.3.4.3.19": "ML-DSA-87",
  },

  aia: {
    "1.3.6.1.5.5.7.48.1": "Online Certificate Status Protocol (OCSP)",
    "1.3.6.1.5.5.7.48.2": "CA Issuers",
  },

  // this includes qualifiers as well
  cps: {
    "1.3.6.1.4.1": {
      name: "Statement Identifier",
      value: undefined,
    },
    "1.3.6.1.5.5.7.2.1": {
      name: "Practices Statement",
      value: undefined,
    },
    "1.3.6.1.5.5.7.2.2": {
      name: "User Notice",
      value: undefined,
    },
    "2.16.840": {
      name: "ANSI Organizational Identifier",
      value: undefined,
    },
    "2.23.140.1.1": {
      name: "Certificate Type",
      value: "Extended Validation",
    },
    "2.23.140.1.2.1": {
      name: "Certificate Type",
      value: "Domain Validation",
    },
    "2.23.140.1.2.2": {
      name: "Certificate Type",
      value: "Organization Validation",
    },
    "2.23.140.1.2.3": {
      name: "Certificate Type",
      value: "Individual Validation",
    },
    "2.23.140.1.3": {
      name: "Certificate Type",
      value: "Extended Validation (Code Signing)",
    },
    "2.23.140.1.31": {
      name: "Certificate Type",
      value: ".onion Extended Validation",
    },
    "2.23.140.2.1": {
      name: "Certificate Type",
      value: "Test Certificate",
    },
  },

  microsoftCertificateTypes: {
    Administrator: "Administrator",
    CA: "Root Certification Authority",
    CAExchange: "CA Exchange",
    CEPEncryption: "CEP Encryption",
    CertificateRequestAgent: "Certificate Request Agent",
    ClientAuth: "Authenticated Session",
    CodeSigning: "Code Signing",
    CrossCA: "Cross Certification Authority",
    CTLSigning: "Trust List Signing",
    DirectoryEmailReplication: "Directory Email Replication",
    DomainController: "Domain Controller",
    DomainControllerAuthentication: "Domain Controller Authentication",
    EFS: "Basic EFS",
    EFSRecovery: "EFS Recovery Agent",
    EnrollmentAgent: "Enrollment Agent",
    EnrollmentAgentOffline: "Exchange Enrollment Agent (Offline request)",
    ExchangeUser: "Exchange User",
    ExchangeUserSignature: "Exchange Signature Only",
    IPSECIntermediateOffline: "IPSec (Offline request)",
    IPSECIntermediateOnline: "IPSEC",
    KerberosAuthentication: "Kerberos Authentication",
    KeyRecoveryAgent: "Key Recovery Agent",
    Machine: "Computer",
    MachineEnrollmentAgent: "Enrollment Agent (Computer)",
    OCSPResponseSigning: "OCSP Response Signing",
    OfflineRouter: "Router (Offline request)",
    RASAndIASServer: "RAS and IAS Server",
    SmartcardLogon: "Smartcard Logon",
    SmartcardUser: "Smartcard User",
    SubCA: "Subordinate Certification Authority",
    User: "User",
    UserSignature: "User Signature Only",
    WebServer: "Web Server",
    Workstation: "Workstation Authentication",
  },
};

function stringToArrayBuffer(string) {
  let result = new Uint8Array(string.length);
  for (let i = 0; i < string.length; i++) {
    result[i] = string.charCodeAt(i);
  }
  return result;
}

// this particular prototype override makes it easy to chain down complex objects
const getObjPath = (obj, path) => {
  path = path.split(".");
  for (let i = 0, len = path.length; i < len; i++) {
    if (Array.isArray(obj[path[i]])) {
      obj = obj[path[i]][path[i + 1]];
      i++;
    } else {
      obj = obj[path[i]];
    }
  }
  return obj;
};

const arrayBufferToHex = arrayBuffer => {
  const array = Array.from(new Uint8Array(arrayBuffer));

  return array
    .map(b => ("00" + b.toString(16)).slice(-2))
    .join(":")
    .toUpperCase();
};

const hash = async (algo, buffer) => {
  const hashBuffer = await crypto.subtle.digest(algo, buffer);
  return arrayBufferToHex(hashBuffer);
};

const hashify = rawHash => {
  if (typeof rawHash === "string") {
    return rawHash.match(/.{2}/g).join(":").toUpperCase();
  }
  if (rawHash instanceof ArrayBuffer) {
    return arrayBufferToHex(rawHash);
  }
  return rawHash.join(":").toUpperCase();
};

export const pemToDER = pem => {
  return stringToArrayBuffer(atob(pem));
};
