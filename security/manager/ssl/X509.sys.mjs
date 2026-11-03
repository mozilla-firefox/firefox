/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DER } from "moz-src:///security/manager/ssl/DER.sys.mjs";

const ERROR_UNSUPPORTED_ASN1 = "unsupported asn.1";
const ERROR_TIME_NOT_VALID = "Time not valid";
const ERROR_LIBRARY_FAILURE = "library failure";

const X509v3 = 2;

/**
 * Helper function to read a NULL tag from the given DER.
 *
 * @param {DER} der a DER object to read a NULL from
 * @returns {null} an object representing an ASN.1 NULL
 */
function readNULL(der) {
  return new NULL(der.readTagAndGetContents(DER.NULL));
}

/**
 * Class representing an ASN.1 NULL. When encoded as DER, the only valid value
 * is 05 00, and thus the contents should always be an empty array.
 */
class NULL {
  /**
   * @param {number[]} bytes the contents of the NULL tag (should be empty)
   */
  constructor(bytes) {
    // Lint TODO: bytes should be an empty array
    this._contents = bytes;
  }
}

/**
 * Helper function to read an OBJECT IDENTIFIER from the given DER.
 *
 * @param {DER} der the DER to read an OBJECT IDENTIFIER from
 * @returns {OID} the value of the OBJECT IDENTIFIER
 */
function readOID(der) {
  return new OID(der.readTagAndGetContents(DER.OBJECT_IDENTIFIER));
}

/** Class representing an ASN.1 OBJECT IDENTIFIER */
class OID {
  /**
   * @param {number[]} bytes the encoded contents of the OBJECT IDENTIFIER
   *                   (not including the ASN.1 tag or length bytes)
   */
  constructor(bytes) {
    this._values = [];
    // First octet has value 40 * value1 + value2
    // Lint TODO: validate that value1 is one of {0, 1, 2}
    // Lint TODO: validate that value2 is in [0, 39] if value1 is 0 or 1
    let value1 = Math.floor(bytes[0] / 40);
    let value2 = bytes[0] - 40 * value1;
    this._values.push(value1);
    this._values.push(value2);
    bytes.shift();
    let accumulator = 0;
    // Lint TODO: prevent overflow here
    while (bytes.length) {
      let value = bytes.shift();
      accumulator *= 128;
      if (value > 128) {
        accumulator += value - 128;
      } else {
        accumulator += value;
        this._values.push(accumulator);
        accumulator = 0;
      }
    }
  }
}

/**
 * Class that serves as an abstract base class for more specific classes that
 * represent datatypes from RFC 5280 and others. Given an array of bytes
 * representing the DER encoding of such types, this framework simplifies the
 * process of making a new DER object, attempting to parse the given bytes, and
 * catching and stashing thrown exceptions. Subclasses are to implement
 * parseOverride, which should read from this._der to fill out the structure's
 * values.
 */
class DecodedDER {
  constructor() {
    this._der = null;
    this._error = null;
  }

  /**
   * Returns the first exception encountered when decoding or null if none has
   * been encountered.
   *
   * @returns {Error} the first exception encountered when decoding or null
   */
  get error() {
    return this._error;
  }

  /**
   * Does the actual work of parsing the data. To be overridden by subclasses.
   * If an implementation of parseOverride throws an exception, parse will catch
   * that exception and stash it in the error property. This enables parent
   * levels in a nested decoding hierarchy to continue to decode as much as
   * possible.
   */
  parseOverride() {
    throw new Error(ERROR_LIBRARY_FAILURE);
  }

  /**
   * Public interface to be called to parse all data. Calls parseOverride inside
   * a try/catch block. If an exception is thrown, stashes the error, which can
   * be obtained via the error getter (above).
   *
   * @param {number[]} bytes encoded DER to be decoded
   */
  parse(bytes) {
    this._der = new DER.DERDecoder(bytes);
    try {
      this.parseOverride();
    } catch (e) {
      this._error = e;
    }
  }
}

/**
 * Helper function for reading the next SEQUENCE out of a DER and creating a new
 * DER out of the resulting bytes.
 *
 * @param {DER} der the underlying DER object
 * @returns {DER} the contents of the SEQUENCE
 */
function readSEQUENCEAndMakeDER(der) {
  return new DER.DERDecoder(der.readTagAndGetContents(DER.SEQUENCE));
}

/**
 * Helper function for reading the next item identified by tag out of a DER and
 * creating a new DER out of the resulting bytes.
 *
 * @param {DER} der the underlying DER object
 * @param {number} tag the expected next tag in the DER
 * @returns {DER} the contents of the tag
 */
function readTagAndMakeDER(der, tag) {
  return new DER.DERDecoder(der.readTagAndGetContents(tag));
}

// Certificate  ::=  SEQUENCE  {
//      tbsCertificate       TBSCertificate,
//      signatureAlgorithm   AlgorithmIdentifier,
//      signatureValue       BIT STRING  }
class Certificate extends DecodedDER {
  constructor() {
    super();
    this._tbsCertificate = new TBSCertificate();
    this._signatureAlgorithm = new AlgorithmIdentifier();
    this._signatureValue = [];
  }

  get tbsCertificate() {
    return this._tbsCertificate;
  }

  get signatureAlgorithm() {
    return this._signatureAlgorithm;
  }

  get signatureValue() {
    return this._signatureValue;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    this._tbsCertificate.parse(contents.readTLV());
    this._signatureAlgorithm.parse(contents.readTLV());

    let signatureValue = contents.readBIT_STRING();
    if (signatureValue.unusedBits != 0) {
      throw new Error(ERROR_UNSUPPORTED_ASN1);
    }
    this._signatureValue = signatureValue.contents;
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// TBSCertificate  ::=  SEQUENCE  {
//      version         [0]  EXPLICIT Version DEFAULT v1,
//      serialNumber         CertificateSerialNumber,
//      signature            AlgorithmIdentifier,
//      issuer               Name,
//      validity             Validity,
//      subject              Name,
//      subjectPublicKeyInfo SubjectPublicKeyInfo,
//      issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
//                           -- If present, version MUST be v2 or v3
//      subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
//                           -- If present, version MUST be v2 or v3
//      extensions      [3]  EXPLICIT Extensions OPTIONAL
//                           -- If present, version MUST be v3
//      }
class TBSCertificate extends DecodedDER {
  constructor() {
    super();
    this._version = null;
    this._serialNumber = [];
    this._signature = new AlgorithmIdentifier();
    this._issuer = new Name();
    this._validity = new Validity();
    this._subject = new Name();
    this._subjectPublicKeyInfo = new SubjectPublicKeyInfo();
    this._extensions = [];
  }

  get version() {
    return this._version;
  }

  get serialNumber() {
    return this._serialNumber;
  }

  get signature() {
    return this._signature;
  }

  get issuer() {
    return this._issuer;
  }

  get validity() {
    return this._validity;
  }

  get subject() {
    return this._subject;
  }

  get subjectPublicKeyInfo() {
    return this._subjectPublicKeyInfo;
  }

  get extensions() {
    return this._extensions;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);

    let versionTag = DER.CONTEXT_SPECIFIC | DER.CONSTRUCTED | 0;
    if (!contents.peekTag(versionTag)) {
      this._version = 1;
    } else {
      let versionContents = readTagAndMakeDER(contents, versionTag);
      let versionBytes = versionContents.readTagAndGetContents(DER.INTEGER);
      if (versionBytes.length == 1 && versionBytes[0] == X509v3) {
        this._version = 3;
      } else {
        // Lint TODO: warn about non-v3 certificates (this INTEGER could take up
        // multiple bytes, be negative, and so on).
        this._version = versionBytes;
      }
      versionContents.assertAtEnd();
    }

    let serialNumberBytes = contents.readTagAndGetContents(DER.INTEGER);
    this._serialNumber = serialNumberBytes;
    this._signature.parse(contents.readTLV());
    this._issuer.parse(contents.readTLV());
    this._validity.parse(contents.readTLV());
    this._subject.parse(contents.readTLV());
    this._subjectPublicKeyInfo.parse(contents.readTLV());

    // Lint TODO: warn about unsupported features
    let issuerUniqueIDTag = DER.CONTEXT_SPECIFIC | DER.CONSTRUCTED | 1;
    if (contents.peekTag(issuerUniqueIDTag)) {
      contents.readTagAndGetContents(issuerUniqueIDTag);
    }
    let subjectUniqueIDTag = DER.CONTEXT_SPECIFIC | DER.CONSTRUCTED | 2;
    if (contents.peekTag(subjectUniqueIDTag)) {
      contents.readTagAndGetContents(subjectUniqueIDTag);
    }

    let extensionsTag = DER.CONTEXT_SPECIFIC | DER.CONSTRUCTED | 3;
    if (contents.peekTag(extensionsTag)) {
      let extensionsSequence = readTagAndMakeDER(contents, extensionsTag);
      let extensionsContents = readSEQUENCEAndMakeDER(extensionsSequence);
      while (!extensionsContents.atEnd()) {
        // TODO: parse extensions
        this._extensions.push(extensionsContents.readTLV());
      }
      extensionsContents.assertAtEnd();
      extensionsSequence.assertAtEnd();
    }
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// AlgorithmIdentifier  ::=  SEQUENCE  {
//      algorithm               OBJECT IDENTIFIER,
//      parameters              ANY DEFINED BY algorithm OPTIONAL  }
class AlgorithmIdentifier extends DecodedDER {
  constructor() {
    super();
    this._algorithm = null;
    this._parameters = null;
  }

  get algorithm() {
    return this._algorithm;
  }

  get parameters() {
    return this._parameters;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    this._algorithm = readOID(contents);
    if (!contents.atEnd()) {
      if (contents.peekTag(DER.NULL)) {
        this._parameters = readNULL(contents);
      } else if (contents.peekTag(DER.OBJECT_IDENTIFIER)) {
        this._parameters = readOID(contents);
      }
    }
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// Name ::= CHOICE { -- only one possibility for now --
//   rdnSequence  RDNSequence }
//
// RDNSequence ::= SEQUENCE OF RelativeDistinguishedName
class Name extends DecodedDER {
  constructor() {
    super();
    this._rdns = [];
  }

  get rdns() {
    return this._rdns;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    while (!contents.atEnd()) {
      let rdn = new RelativeDistinguishedName();
      rdn.parse(contents.readTLV());
      this._rdns.push(rdn);
    }
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// RelativeDistinguishedName ::=
//   SET SIZE (1..MAX) OF AttributeTypeAndValue
class RelativeDistinguishedName extends DecodedDER {
  constructor() {
    super();
    this._avas = [];
  }

  get avas() {
    return this._avas;
  }

  parseOverride() {
    let contents = readTagAndMakeDER(this._der, DER.SET);
    // Lint TODO: enforce SET SIZE restrictions
    while (!contents.atEnd()) {
      let ava = new AttributeTypeAndValue();
      ava.parse(contents.readTLV());
      this._avas.push(ava);
    }
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// AttributeTypeAndValue ::= SEQUENCE {
//   type     AttributeType,
//   value    AttributeValue }
//
// AttributeType ::= OBJECT IDENTIFIER
//
// AttributeValue ::= ANY -- DEFINED BY AttributeType
class AttributeTypeAndValue extends DecodedDER {
  constructor() {
    super();
    this._type = null;
    this._value = new DirectoryString();
  }

  get type() {
    return this._type;
  }

  get value() {
    return this._value;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    this._type = readOID(contents);
    // We don't support universalString or bmpString.
    // IA5String is supported because it is valid if `type == id-emailaddress`.
    // Lint TODO: validate that the type of string is valid given `type`.
    this._value.parse(
      contents.readTLVChoice([
        DER.UTF8String,
        DER.PrintableString,
        DER.TeletexString,
        DER.IA5String,
      ])
    );
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// DirectoryString ::= CHOICE {
//       teletexString           TeletexString (SIZE (1..MAX)),
//       printableString         PrintableString (SIZE (1..MAX)),
//       universalString         UniversalString (SIZE (1..MAX)),
//       utf8String              UTF8String (SIZE (1..MAX)),
//       bmpString               BMPString (SIZE (1..MAX)) }
class DirectoryString extends DecodedDER {
  constructor() {
    super();
    this._type = null;
    this._value = null;
  }

  get type() {
    return this._type;
  }

  get value() {
    return this._value;
  }

  parseOverride() {
    if (this._der.peekTag(DER.UTF8String)) {
      this._type = DER.UTF8String;
    } else if (this._der.peekTag(DER.PrintableString)) {
      this._type = DER.PrintableString;
    } else if (this._der.peekTag(DER.TeletexString)) {
      this._type = DER.TeletexString;
    } else if (this._der.peekTag(DER.IA5String)) {
      this._type = DER.IA5String;
    }
    // Lint TODO: validate that the contents are actually valid for the type
    this._value = this._der.readTagAndGetContents(this._type);
    this._der.assertAtEnd();
  }
}

// Time ::= CHOICE {
//      utcTime        UTCTime,
//      generalTime    GeneralizedTime }
class Time extends DecodedDER {
  constructor() {
    super();
    this._type = null;
    this._time = null;
  }

  get time() {
    return this._time;
  }

  parseOverride() {
    if (this._der.peekTag(DER.UTCTime)) {
      this._type = DER.UTCTime;
    } else if (this._der.peekTag(DER.GeneralizedTime)) {
      this._type = DER.GeneralizedTime;
    }
    let contents = readTagAndMakeDER(this._der, this._type);
    let year;
    // Lint TODO: validate that the appropriate one of {UTCTime,GeneralizedTime}
    // is used according to RFC 5280 and what the value of the date is.
    // TODO TODO: explain this better (just quote the rfc).
    if (this._type == DER.UTCTime) {
      // UTCTime is YYMMDDHHMMSSZ in RFC 5280. If YY is greater than or equal
      // to 50, the year is 19YY. Otherwise, it is 20YY.
      let y1 = this._validateDigit(contents.readByte());
      let y2 = this._validateDigit(contents.readByte());
      let yy = y1 * 10 + y2;
      if (yy >= 50) {
        year = 1900 + yy;
      } else {
        year = 2000 + yy;
      }
    } else {
      // GeneralizedTime is YYYYMMDDHHMMSSZ in RFC 5280.
      year = 0;
      for (let i = 0; i < 4; i++) {
        let y = this._validateDigit(contents.readByte());
        year = year * 10 + y;
      }
    }

    let m1 = this._validateDigit(contents.readByte());
    let m2 = this._validateDigit(contents.readByte());
    let month = m1 * 10 + m2;
    if (month == 0 || month > 12) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }

    let d1 = this._validateDigit(contents.readByte());
    let d2 = this._validateDigit(contents.readByte());
    let day = d1 * 10 + d2;
    if (day == 0 || day > 31) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }

    let h1 = this._validateDigit(contents.readByte());
    let h2 = this._validateDigit(contents.readByte());
    let hour = h1 * 10 + h2;
    if (hour > 23) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }

    let min1 = this._validateDigit(contents.readByte());
    let min2 = this._validateDigit(contents.readByte());
    let minute = min1 * 10 + min2;
    if (minute > 59) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }

    let s1 = this._validateDigit(contents.readByte());
    let s2 = this._validateDigit(contents.readByte());
    let second = s1 * 10 + s2;
    if (second > 60) {
      // leap-seconds mean this can be as much as 60
      throw new Error(ERROR_TIME_NOT_VALID);
    }

    let z = contents.readByte();
    if (z != "Z".charCodeAt(0)) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }
    // Lint TODO: verify that the Time doesn't specify a nonsensical
    // month/day/etc.
    // months are zero-indexed in JS
    this._time = new Date(Date.UTC(year, month - 1, day, hour, minute, second));

    contents.assertAtEnd();
    this._der.assertAtEnd();
  }

  /**
   * Takes a byte that is supposed to be in the ASCII range for "0" to "9".
   * Validates the range and then converts it to the range 0 to 9.
   *
   * @param {number} d the digit in question (as ASCII in the range ["0", "9"])
   * @returns {number} the numerical value of the digit (in the range [0, 9])
   */
  _validateDigit(d) {
    if (d < "0".charCodeAt(0) || d > "9".charCodeAt(0)) {
      throw new Error(ERROR_TIME_NOT_VALID);
    }
    return d - "0".charCodeAt(0);
  }
}

// Validity ::= SEQUENCE {
//      notBefore      Time,
//      notAfter       Time }
class Validity extends DecodedDER {
  constructor() {
    super();
    this._notBefore = new Time();
    this._notAfter = new Time();
  }

  get notBefore() {
    return this._notBefore;
  }

  get notAfter() {
    return this._notAfter;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    this._notBefore.parse(
      contents.readTLVChoice([DER.UTCTime, DER.GeneralizedTime])
    );
    this._notAfter.parse(
      contents.readTLVChoice([DER.UTCTime, DER.GeneralizedTime])
    );
    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

// SubjectPublicKeyInfo  ::=  SEQUENCE  {
//      algorithm            AlgorithmIdentifier,
//      subjectPublicKey     BIT STRING  }
class SubjectPublicKeyInfo extends DecodedDER {
  constructor() {
    super();
    this._algorithm = new AlgorithmIdentifier();
    this._subjectPublicKey = null;
  }

  get algorithm() {
    return this._algorithm;
  }

  get subjectPublicKey() {
    return this._subjectPublicKey;
  }

  parseOverride() {
    let contents = readSEQUENCEAndMakeDER(this._der);
    this._algorithm.parse(contents.readTLV());
    let subjectPublicKeyBitString = contents.readBIT_STRING();
    if (subjectPublicKeyBitString.unusedBits != 0) {
      throw new Error(ERROR_UNSUPPORTED_ASN1);
    }
    this._subjectPublicKey = subjectPublicKeyBitString.contents;

    contents.assertAtEnd();
    this._der.assertAtEnd();
  }
}

export var X509 = { Certificate };
