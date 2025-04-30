#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Reads a certificate specification from stdin or a file and outputs a
signed x509 certificate with the desired properties.

The input format is as follows:

issuer:<issuer distinguished name specification>
subject:<subject distinguished name specification>
[version:{1,2,3,4}]
[validity:<YYYYMMDD-YYYYMMDD|duration in days>]
[issuerKey:<key specification>]
[subjectKey:<key specification>]
[signature:{sha256WithRSAEncryption,sha1WithRSAEncryption,
            md5WithRSAEncryption,ecdsaWithSHA256,ecdsaWithSHA384,
            ecdsaWithSHA512}]
[serialNumber:<integer in the interval [1, 127]>]
[extension:<extension name:<extension-specific data>>]
[...]

Known extensions are:
basicConstraints:[cA],[pathLenConstraint]
keyUsage:[digitalSignature,nonRepudiation,keyEncipherment,
          dataEncipherment,keyAgreement,keyCertSign,cRLSign]
extKeyUsage:[serverAuth,clientAuth,codeSigning,emailProtection
             nsSGC, # Netscape Server Gated Crypto
             OCSPSigning,timeStamping]
subjectAlternativeName:[<dNSName|directoryName|"ip4:"iPV4Address>,...]
authorityInformationAccess:<OCSP URI>
certificatePolicies:[<policy OID>,...]
nameConstraints:{permitted,excluded}:[<dNSName|directoryName>,...]
nsCertType:sslServer
TLSFeature:[<TLSFeature>,...]
embeddedSCTList:[<key specification>:<YYYYMMDD>,...]
delegationUsage:

Where:
  [] indicates an optional field or component of a field
  <> indicates a required component of a field
  {} indicates a choice of exactly one value among a set of values
  [a,b,c] indicates a list of potential values, of which zero or more
          may be used

For instance, the version field is optional. However, if it is
specified, it must have exactly one value from the set {1,2,3,4}.

Most fields have reasonable default values. By default one shared RSA
key is used for all signatures and subject public key information
fields. Using "issuerKey:<key specification>" or
"subjectKey:<key specification>" causes a different key be used for
signing or as the subject public key information field, respectively.
See pykey.py for the list of available specifications.
The signature algorithm is sha256WithRSAEncryption by default.

The validity period may be specified as either concrete notBefore and
notAfter values or as a validity period centered around 'now'. For the
latter, this will result in a notBefore of 'now' - duration/2 and a
notAfter of 'now' + duration/2.

Issuer and subject distinguished name specifications are of the form
'[stringEncoding]/C=XX/O=Example/CN=example.com'. C (country name), ST
(state or province name), L (locality name), O (organization name), OU
(organizational unit name), CN (common name) and emailAddress (email
address) are currently supported. The optional stringEncoding field may
be 'utf8String' or 'printableString'. If the given string does not
contain a '/', it is assumed to represent a common name. If an empty
string is provided, then an empty distinguished name is returned.
DirectoryNames also use this format. When specifying a directoryName in
a nameConstraints extension, the implicit form may not be used.

If an extension name has '[critical]' after it, it will be marked as
critical. Otherwise (by default), it will not be marked as critical.

TLSFeature values can either consist of a named value (currently only
'OCSPMustStaple' which corresponds to status_request) or a numeric TLS
feature value (see rfc7633 for more information).

If a serial number is not explicitly specified, it is automatically
generated based on the contents of the certificate.
"""

import base64
import datetime
import hashlib
import re
import socket
import sys
from struct import pack

import pyct
import pykey
from pyasn1.codec.der import decoder, encoder
from pyasn1.type import constraint, tag, univ, useful
from pyasn1_modules import rfc2459


class Error(Exception):
    """Base class for exceptions in this module."""

    pass


class UnknownBaseError(Error):
    """Base class for handling unexpected input in this module."""

    def __init__(self, value):
        super(UnknownBaseError, self).__init__()
        self.value = value
        self.category = "input"

    def __str__(self):
        return 'Unknown %s type "%s"' % (self.category, repr(self.value))


class UnknownAlgorithmTypeError(UnknownBaseError):
    """Helper exception type to handle unknown algorithm types."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "algorithm"


class UnknownParameterTypeError(UnknownBaseError):
    """Helper exception type to handle unknown input parameters."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "parameter"


class UnknownExtensionTypeError(UnknownBaseError):
    """Helper exception type to handle unknown input extensions."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "extension"


class UnknownKeyPurposeTypeError(UnknownBaseError):
    """Helper exception type to handle unknown key purposes."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "keyPurpose"


class UnknownKeyTargetError(UnknownBaseError):
    """Helper exception type to handle unknown key targets."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "key target"


class UnknownVersionError(UnknownBaseError):
    """Helper exception type to handle unknown specified versions."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "version"


class UnknownNameConstraintsSpecificationError(UnknownBaseError):
    """Helper exception type to handle unknown specified
    nameConstraints."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "nameConstraints specification"


class UnknownDNTypeError(UnknownBaseError):
    """Helper exception type to handle unknown DN types."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "DN"


class UnknownNSCertTypeError(UnknownBaseError):
    """Helper exception type to handle unknown nsCertType types."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "nsCertType"


class UnknownTLSFeature(UnknownBaseError):
    """Helper exception type to handle unknown TLS Features."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "TLSFeature"


class UnknownDelegatedCredentialError(UnknownBaseError):
    """Helper exception type to handle unknown Delegated Credential args."""

    def __init__(self, value):
        UnknownBaseError.__init__(self, value)
        self.category = "delegatedCredential"


class InvalidSCTSpecification(Error):
    """Helper exception type to handle invalid SCT specifications."""

    def __init__(self, value):
        super(InvalidSCTSpecification, self).__init__()
        self.value = value

    def __str__(self):
        return repr('invalid SCT specification "{}"' % self.value)


class InvalidSerialNumber(Error):
    """Exception type to handle invalid serial numbers."""

    def __init__(self, value):
        super(InvalidSerialNumber, self).__init__()
        self.value = value

    def __str__(self):
        return repr(self.value)


def getASN1Tag(asn1Type):
    """Helper function for returning the base tag value of a given
    type from the pyasn1 package"""
    return asn1Type.tagSet.baseTag.tagId


def stringToAccessDescription(string):
    """Helper function that takes a string representing a URI
    presumably identifying an OCSP authority information access
    location. Returns an AccessDescription usable by pyasn1."""
    accessMethod = rfc2459.id_ad_ocsp
    accessLocation = rfc2459.GeneralName()
    accessLocation["uniformResourceIdentifier"] = string
    sequence = univ.Sequence()
    sequence.setComponentByPosition(0, accessMethod)
    sequence.setComponentByPosition(1, accessLocation)
    return sequence


def stringToDN(string, tag=None):
    """Takes a string representing a distinguished name or directory
    name and returns a Name for use by pyasn1. See the documentation
    for the issuer and subject fields for more details. Takes an
    optional implicit tag in cases where the Name needs to be tagged
    differently."""
    if string and "/" not in string:
        string = "/CN=%s" % string
    rdns = rfc2459.RDNSequence()
    pattern = "/(C|ST|L|O|OU|CN|emailAddress)="
    split = re.split(pattern, string)
    # split should now be [[encoding], <type>, <value>, <type>, <value>, ...]
    if split[0]:
        encoding = split[0]
    else:
        encoding = "utf8String"
    for pos, (nameType, value) in enumerate(zip(split[1::2], split[2::2])):
        ava = rfc2459.AttributeTypeAndValue()
        if nameType == "C":
            ava["type"] = rfc2459.id_at_countryName
            nameComponent = rfc2459.X520countryName(value)
        elif nameType == "ST":
            ava["type"] = rfc2459.id_at_stateOrProvinceName
            nameComponent = rfc2459.X520StateOrProvinceName()
        elif nameType == "L":
            ava["type"] = rfc2459.id_at_localityName
            nameComponent = rfc2459.X520LocalityName()
        elif nameType == "O":
            ava["type"] = rfc2459.id_at_organizationName
            nameComponent = rfc2459.X520OrganizationName()
        elif nameType == "OU":
            ava["type"] = rfc2459.id_at_organizationalUnitName
            nameComponent = rfc2459.X520OrganizationalUnitName()
        elif nameType == "CN":
            ava["type"] = rfc2459.id_at_commonName
            nameComponent = rfc2459.X520CommonName()
        elif nameType == "emailAddress":
            ava["type"] = rfc2459.emailAddress
            nameComponent = rfc2459.Pkcs9email(value)
        else:
            raise UnknownDNTypeError(nameType)
        if not nameType == "C" and not nameType == "emailAddress":
            # The value may have things like '\0' (i.e. a slash followed by
            # the number zero) that have to be decoded into the resulting
            # '\x00' (i.e. a byte with value zero).
            nameComponent[encoding] = value.encode().decode(encoding="unicode_escape")
        ava["value"] = nameComponent
        rdn = rfc2459.RelativeDistinguishedName()
        rdn.setComponentByPosition(0, ava)
        rdns.setComponentByPosition(pos, rdn)
    if tag:
        name = rfc2459.Name().subtype(implicitTag=tag)
    else:
        name = rfc2459.Name()
    name.setComponentByPosition(0, rdns)
    return name


def stringToAlgorithmIdentifiers(string):
    """Helper function that converts a description of an algorithm
    to a representation usable by the pyasn1 package and a hash
    algorithm constant for use by pykey."""
    algorithmIdentifier = rfc2459.AlgorithmIdentifier()
    algorithmType = None
    algorithm = None
    # We add Null parameters for RSA only
    addParameters = False
    if string == "sha1WithRSAEncryption":
        algorithmType = pykey.HASH_SHA1
        algorithm = rfc2459.sha1WithRSAEncryption
        addParameters = True
    elif string == "sha256WithRSAEncryption":
        algorithmType = pykey.HASH_SHA256
        algorithm = univ.ObjectIdentifier("1.2.840.113549.1.1.11")
        addParameters = True
    elif string == "md5WithRSAEncryption":
        algorithmType = pykey.HASH_MD5
        algorithm = rfc2459.md5WithRSAEncryption
        addParameters = True
    elif string == "ecdsaWithSHA256":
        algorithmType = pykey.HASH_SHA256
        algorithm = univ.ObjectIdentifier("1.2.840.10045.4.3.2")
    elif string == "ecdsaWithSHA384":
        algorithmType = pykey.HASH_SHA384
        algorithm = univ.ObjectIdentifier("1.2.840.10045.4.3.3")
    elif string == "ecdsaWithSHA512":
        algorithmType = pykey.HASH_SHA512
        algorithm = univ.ObjectIdentifier("1.2.840.10045.4.3.4")
    else:
        raise UnknownAlgorithmTypeError(string)
    algorithmIdentifier["algorithm"] = algorithm
    if addParameters:
        # Directly setting parameters to univ.Null doesn't currently work.
        nullEncapsulated = encoder.encode(univ.Null())
        algorithmIdentifier["parameters"] = univ.Any(nullEncapsulated)
    return (algorithmIdentifier, algorithmType)


def datetimeToTime(dt):
    """Takes a datetime object and returns an rfc2459.Time object with
    that time as its value as a GeneralizedTime"""
    time = rfc2459.Time()
    time["generalTime"] = useful.GeneralizedTime(dt.strftime("%Y%m%d%H%M%SZ"))
    return time


def serialBytesToString(serialBytes):
    """Takes a list of integers in the interval [0, 255] and returns
    the corresponding serial number string."""
    serialBytesLen = len(serialBytes)
    if serialBytesLen > 127:
        raise InvalidSerialNumber(f"{serialBytesLen} bytes is too long")
    # Prepend the ASN.1 INTEGER tag and length bytes.
    stringBytes = [getASN1Tag(univ.Integer), serialBytesLen] + serialBytes
    return bytes(stringBytes)


class Certificate:
    """Utility class for reading a certificate specification and
    generating a signed x509 certificate"""

    def __init__(self, paramStream):
        self.versionValue = 2  # a value of 2 is X509v3
        self.signature = "sha256WithRSAEncryption"
        self.issuer = "Default Issuer"
        actualNow = datetime.datetime.utcnow()
        self.now = datetime.datetime.strptime(str(actualNow.year), "%Y")
        aYearAndAWhile = datetime.timedelta(days=400)
        self.notBefore = self.now - aYearAndAWhile
        self.notAfter = self.now + aYearAndAWhile
        self.subject = "Default Subject"
        self.extensions = None
        # The serial number can be automatically generated from the
        # certificate specification. We need this value to depend in
        # part of what extensions are present. self.extensions are
        # pyasn1 objects. Depending on the string representation of
        # these objects can cause the resulting serial number to change
        # unexpectedly, so instead we depend on the original string
        # representation of the extensions as specified.
        self.extensionLines = None
        self.savedEmbeddedSCTListData = None
        self.subjectKey = pykey.keyFromSpecification("default")
        self.issuerKey = pykey.keyFromSpecification("default")
        self.serialNumber = None
        self.decodeParams(paramStream)
        # If a serial number wasn't specified, generate one based on
        # the certificate contents.
        if not self.serialNumber:
            self.serialNumber = self.generateSerialNumber()
        # This has to be last because the SCT signature depends on the
        # contents of the certificate.
        if self.savedEmbeddedSCTListData:
            self.addEmbeddedSCTListData()

    def generateSerialNumber(self):
        """Generates a serial number for this certificate based on its
        contents. Intended to be reproducible for compatibility with
        the build system on OS X (see the comment above main, later in
        this file)."""
        hasher = hashlib.sha256()
        hasher.update(str(self.versionValue).encode())
        hasher.update(self.signature.encode())
        hasher.update(self.issuer.encode())
        hasher.update(str(self.notBefore).encode())
        hasher.update(str(self.notAfter).encode())
        hasher.update(self.subject.encode())
        if self.extensionLines:
            for extensionLine in self.extensionLines:
                hasher.update(extensionLine.encode())
        if self.savedEmbeddedSCTListData:
            # savedEmbeddedSCTListData is
            # (embeddedSCTListSpecification, critical), where |critical|
            # may be None
            hasher.update(self.savedEmbeddedSCTListData[0].encode())
            if self.savedEmbeddedSCTListData[1]:
                hasher.update(self.savedEmbeddedSCTListData[1].encode())
        serialBytes = [c for c in hasher.digest()[:20]]
        # Ensure that the most significant bit isn't set (which would
        # indicate a negative number, which isn't valid for serial
        # numbers).
        serialBytes[0] &= 0x7F
        # Also ensure that the least significant bit on the most
        # significant byte is set (to prevent a leading zero byte,
        # which also wouldn't be valid).
        serialBytes[0] |= 0x01
        return serialBytesToString(serialBytes)

    def decodeParams(self, paramStream):
        for line in paramStream.readlines():
            self.decodeParam(line.strip())

    def decodeParam(self, line):
        param = line.split(":")[0]
        value = ":".join(line.split(":")[1:])
        if param == "version":
            self.setVersion(value)
        elif param == "subject":
            self.subject = value
        elif param == "issuer":
            self.issuer = value
        elif param == "validity":
            self.decodeValidity(value)
        elif param == "extension":
            self.decodeExtension(value)
        elif param == "issuerKey":
            self.setupKey("issuer", value)
        elif param == "subjectKey":
            self.setupKey("subject", value)
        elif param == "signature":
            self.signature = value
        elif param == "serialNumber":
            serialNumber = int(value)
            # Ensure only serial numbers that conform to the rules listed in
            # generateSerialNumber() are permitted.
            if serialNumber < 1 or serialNumber > 127:
                raise InvalidSerialNumber(value)
            self.serialNumber = serialBytesToString([serialNumber])
        else:
            raise UnknownParameterTypeError(param)

    def setVersion(self, version):
        intVersion = int(version)
        if intVersion >= 1 and intVersion <= 4:
            self.versionValue = intVersion - 1
        else:
            raise UnknownVersionError(version)

    def decodeValidity(self, duration):
        match = re.search("([0-9]{8})-([0-9]{8})", duration)
        if match:
            self.notBefore = datetime.datetime.strptime(match.group(1), "%Y%m%d")
            self.notAfter = datetime.datetime.strptime(match.group(2), "%Y%m%d")
        else:
            delta = datetime.timedelta(days=(int(duration) / 2))
            self.notBefore = self.now - delta
            self.notAfter = self.now + delta

    def decodeExtension(self, extension):
        match = re.search(r"([a-zA-Z]+)(\[critical\])?:(.*)", extension)
        if not match:
            raise UnknownExtensionTypeError(extension)
        extensionType = match.group(1)
        critical = match.group(2)
        value = match.group(3)
        if extensionType == "basicConstraints":
            self.addBasicConstraints(value, critical)
        elif extensionType == "keyUsage":
            self.addKeyUsage(value, critical)
        elif extensionType == "extKeyUsage":
            self.addExtKeyUsage(value, critical)
        elif extensionType == "subjectAlternativeName":
            self.addSubjectAlternativeName(value, critical)
        elif extensionType == "authorityInformationAccess":
            self.addAuthorityInformationAccess(value, critical)
        elif extensionType == "certificatePolicies":
            self.addCertificatePolicies(value, critical)
        elif extensionType == "nameConstraints":
            self.addNameConstraints(value, critical)
        elif extensionType == "nsCertType":
            self.addNSCertType(value, critical)
        elif extensionType == "TLSFeature":
            self.addTLSFeature(value, critical)
        elif extensionType == "embeddedSCTList":
            self.savedEmbeddedSCTListData = (value, critical)
        elif extensionType == "delegationUsage":
            self.addDelegationUsage(critical)
        else:
            raise UnknownExtensionTypeError(extensionType)

        if extensionType != "embeddedSCTList":
            if not self.extensionLines:
                self.extensionLines = []
            self.extensionLines.append(extension)

    def setupKey(self, subjectOrIssuer, value):
        if subjectOrIssuer == "subject":
            self.subjectKey = pykey.keyFromSpecification(value)
        elif subjectOrIssuer == "issuer":
            self.issuerKey = pykey.keyFromSpecification(value)
        else:
            raise UnknownKeyTargetError(subjectOrIssuer)

    def addExtension(self, extensionType, extensionValue, critical):
        if not self.extensions:
            self.extensions = []
        encapsulated = univ.OctetString(encoder.encode(extensionValue))
        extension = rfc2459.Extension()
        extension["extnID"] = extensionType
        # critical is either the string '[critical]' or None.
        # We only care whether or not it is truthy.
        if critical:
            extension["critical"] = True
        extension["extnValue"] = encapsulated
        self.extensions.append(extension)

    def addBasicConstraints(self, basicConstraints, critical):
        cA = basicConstraints.split(",")[0]
        pathLenConstraint = basicConstraints.split(",")[1]
        basicConstraintsExtension = rfc2459.BasicConstraints()
        basicConstraintsExtension["cA"] = cA == "cA"
        if pathLenConstraint:
            pathLenConstraintValue = univ.Integer(int(pathLenConstraint)).subtype(
                subtypeSpec=constraint.ValueRangeConstraint(0, float("inf"))
            )
            basicConstraintsExtension["pathLenConstraint"] = pathLenConstraintValue
        self.addExtension(
            rfc2459.id_ce_basicConstraints, basicConstraintsExtension, critical
        )

    def addKeyUsage(self, keyUsage, critical):
        keyUsageExtension = rfc2459.KeyUsage(keyUsage)
        self.addExtension(rfc2459.id_ce_keyUsage, keyUsageExtension, critical)

    def keyPurposeToOID(self, keyPurpose):
        if keyPurpose == "serverAuth":
            return rfc2459.id_kp_serverAuth
        if keyPurpose == "clientAuth":
            return rfc2459.id_kp_clientAuth
        if keyPurpose == "codeSigning":
            return rfc2459.id_kp_codeSigning
        if keyPurpose == "emailProtection":
            return rfc2459.id_kp_emailProtection
        if keyPurpose == "nsSGC":
            return univ.ObjectIdentifier("2.16.840.1.113730.4.1")
        if keyPurpose == "OCSPSigning":
            return univ.ObjectIdentifier("1.3.6.1.5.5.7.3.9")
        if keyPurpose == "timeStamping":
            return rfc2459.id_kp_timeStamping
        raise UnknownKeyPurposeTypeError(keyPurpose)

    def addExtKeyUsage(self, extKeyUsage, critical):
        extKeyUsageExtension = rfc2459.ExtKeyUsageSyntax()
        for count, keyPurpose in enumerate(extKeyUsage.split(",")):
            extKeyUsageExtension.setComponentByPosition(
                count, self.keyPurposeToOID(keyPurpose)
            )
        self.addExtension(rfc2459.id_ce_extKeyUsage, extKeyUsageExtension, critical)

    def addSubjectAlternativeName(self, names, critical):
        IPV4_PREFIX = "ip4:"

        subjectAlternativeName = rfc2459.SubjectAltName()
        for count, name in enumerate(names.split(",")):
            generalName = rfc2459.GeneralName()
            if "/" in name:
                directoryName = stringToDN(
                    name, tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 4)
                )
                generalName["directoryName"] = directoryName
            elif "@" in name:
                generalName["rfc822Name"] = name
            elif name.startswith(IPV4_PREFIX):
                generalName["iPAddress"] = socket.inet_pton(
                    socket.AF_INET, name[len(IPV4_PREFIX) :]
                )
            else:
                # The string may have things like '\0' (i.e. a slash
                # followed by the number zero) that have to be decoded into
                # the resulting '\x00' (i.e. a byte with value zero).
                generalName["dNSName"] = name.encode().decode("unicode_escape")
            subjectAlternativeName.setComponentByPosition(count, generalName)
        self.addExtension(
            rfc2459.id_ce_subjectAltName, subjectAlternativeName, critical
        )

    def addAuthorityInformationAccess(self, ocspURI, critical):
        sequence = univ.Sequence()
        accessDescription = stringToAccessDescription(ocspURI)
        sequence.setComponentByPosition(0, accessDescription)
        self.addExtension(rfc2459.id_pe_authorityInfoAccess, sequence, critical)

    def addCertificatePolicies(self, policyOIDs, critical):
        policies = rfc2459.CertificatePolicies()
        for pos, policyOID in enumerate(policyOIDs.split(",")):
            if policyOID == "any":
                policyOID = "2.5.29.32.0"
            policy = rfc2459.PolicyInformation()
            policyIdentifier = rfc2459.CertPolicyId(policyOID)
            policy["policyIdentifier"] = policyIdentifier
            policies.setComponentByPosition(pos, policy)
        self.addExtension(rfc2459.id_ce_certificatePolicies, policies, critical)

    def addNameConstraints(self, constraints, critical):
        nameConstraints = rfc2459.NameConstraints()
        if constraints.startswith("permitted:"):
            (subtreesType, subtreesTag) = ("permittedSubtrees", 0)
        elif constraints.startswith("excluded:"):
            (subtreesType, subtreesTag) = ("excludedSubtrees", 1)
        else:
            raise UnknownNameConstraintsSpecificationError(constraints)
        generalSubtrees = rfc2459.GeneralSubtrees().subtype(
            implicitTag=tag.Tag(
                tag.tagClassContext, tag.tagFormatConstructed, subtreesTag
            )
        )
        subtrees = constraints[(constraints.find(":") + 1) :]
        for pos, name in enumerate(subtrees.split(",")):
            generalName = rfc2459.GeneralName()
            if "/" in name:
                directoryName = stringToDN(
                    name, tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 4)
                )
                generalName["directoryName"] = directoryName
            else:
                generalName["dNSName"] = name
            generalSubtree = rfc2459.GeneralSubtree()
            generalSubtree["base"] = generalName
            generalSubtrees.setComponentByPosition(pos, generalSubtree)
        nameConstraints[subtreesType] = generalSubtrees
        self.addExtension(rfc2459.id_ce_nameConstraints, nameConstraints, critical)

    def addNSCertType(self, certType, critical):
        if certType != "sslServer":
            raise UnknownNSCertTypeError(certType)
        self.addExtension(
            univ.ObjectIdentifier("2.16.840.1.113730.1.1"),
            univ.BitString("'01'B"),
            critical,
        )

    def addDelegationUsage(self, critical):
        if critical:
            raise UnknownDelegatedCredentialError(critical)
        self.addExtension(
            univ.ObjectIdentifier("1.3.6.1.4.1.44363.44"), univ.Null(), critical
        )

    def addTLSFeature(self, features, critical):
        namedFeatures = {"OCSPMustStaple": 5}
        featureList = [f.strip() for f in features.split(",")]
        sequence = univ.Sequence()
        for pos, feature in enumerate(featureList):
            featureValue = 0
            try:
                featureValue = int(feature)
            except ValueError:
                try:
                    featureValue = namedFeatures[feature]
                except Exception:
                    raise UnknownTLSFeature(feature)
            sequence.setComponentByPosition(pos, univ.Integer(featureValue))
        self.addExtension(
            univ.ObjectIdentifier("1.3.6.1.5.5.7.1.24"), sequence, critical
        )

    def addEmbeddedSCTListData(self):
        (scts, critical) = self.savedEmbeddedSCTListData
        encodedSCTs = []
        for sctSpec in scts.split(","):
            match = re.search(r"(\w+):(\d{8})", sctSpec)
            if not match:
                raise InvalidSCTSpecification(sctSpec)
            keySpec = match.group(1)
            key = pykey.keyFromSpecification(keySpec)
            time = datetime.datetime.strptime(match.group(2), "%Y%m%d")
            tbsCertificate = self.getTBSCertificate()
            tbsDER = encoder.encode(tbsCertificate)
            sct = pyct.SCT(key, time, pyct.PrecertEntry(tbsDER, self.issuerKey))
            signed = sct.signAndEncode()
            lengthPrefix = pack("!H", len(signed))
            encodedSCTs.append(lengthPrefix + signed)
        encodedSCTBytes = b"".join(encodedSCTs)
        lengthPrefix = pack("!H", len(encodedSCTBytes))
        extensionBytes = lengthPrefix + encodedSCTBytes
        self.addExtension(
            univ.ObjectIdentifier("1.3.6.1.4.1.11129.2.4.2"),
            univ.OctetString(extensionBytes),
            critical,
        )

    def getVersion(self):
        return rfc2459.Version(self.versionValue).subtype(
            explicitTag=tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 0)
        )

    def getSerialNumber(self):
        return decoder.decode(self.serialNumber)[0]

    def getIssuer(self):
        return stringToDN(self.issuer)

    def getValidity(self):
        validity = rfc2459.Validity()
        validity["notBefore"] = self.getNotBefore()
        validity["notAfter"] = self.getNotAfter()
        return validity

    def getNotBefore(self):
        return datetimeToTime(self.notBefore)

    def getNotAfter(self):
        return datetimeToTime(self.notAfter)

    def getSubject(self):
        return stringToDN(self.subject)

    def getTBSCertificate(self):
        (signatureOID, _) = stringToAlgorithmIdentifiers(self.signature)
        tbsCertificate = rfc2459.TBSCertificate()
        tbsCertificate["version"] = self.getVersion()
        tbsCertificate["serialNumber"] = self.getSerialNumber()
        tbsCertificate["signature"] = signatureOID
        tbsCertificate["issuer"] = self.getIssuer()
        tbsCertificate["validity"] = self.getValidity()
        tbsCertificate["subject"] = self.getSubject()
        tbsCertificate["subjectPublicKeyInfo"] = (
            self.subjectKey.asSubjectPublicKeyInfo()
        )
        if self.extensions:
            extensions = rfc2459.Extensions().subtype(
                explicitTag=tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 3)
            )
            for count, extension in enumerate(self.extensions):
                extensions.setComponentByPosition(count, extension)
            tbsCertificate["extensions"] = extensions
        return tbsCertificate

    def toDER(self):
        (signatureOID, hashAlgorithm) = stringToAlgorithmIdentifiers(self.signature)
        certificate = rfc2459.Certificate()
        tbsCertificate = self.getTBSCertificate()
        certificate["tbsCertificate"] = tbsCertificate
        certificate["signatureAlgorithm"] = signatureOID
        tbsDER = encoder.encode(tbsCertificate)
        certificate["signatureValue"] = self.issuerKey.sign(tbsDER, hashAlgorithm)
        return encoder.encode(certificate)

    def toPEM(self):
        output = "-----BEGIN CERTIFICATE-----"
        der = self.toDER()
        b64 = base64.b64encode(der).decode()
        while b64:
            output += "\n" + b64[:64]
            b64 = b64[64:]
        output += "\n-----END CERTIFICATE-----"
        return output


# The build harness will call this function with an output
# file-like object and a path to a file containing a
# specification. This will read the specification and output
# the certificate as PEM.
def main(output, inputPath):
    with open(inputPath) as configStream:
        output.write(Certificate(configStream).toPEM() + "\n")


# When run as a standalone program, this will read a specification from
# stdin and output the certificate as PEM to stdout.
if __name__ == "__main__":
    print(Certificate(sys.stdin).toPEM())
