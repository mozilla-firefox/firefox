#!/usr/bin/env python3
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Given a directory of files, packages them up and signs the
resulting zip file. Mainly for creating test inputs to the
nsIX509CertDB.openSignedAppFileAsync API.
"""
from base64 import b64encode
from cbor2 import dumps
from cbor2.types import CBORTag
from hashlib import sha1, sha256
import argparse
from io import StringIO
import os
import re
import sys
import zipfile

# These libraries moved to security/manager/tools/ in bug 1699294.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
import pycert
import pycms
import pykey

ES256 = -7
ES384 = -35
ES512 = -36
KID = 4
ALG = 1
COSE_Sign = 98


def coseAlgorithmToPykeyHash(algorithm):
    """Helper function that takes one of (ES256, ES384, ES512)
    and returns the corresponding pykey.HASH_* identifier."""
    if algorithm == ES256:
        return pykey.HASH_SHA256
    if algorithm == ES384:
        return pykey.HASH_SHA384
    if algorithm == ES512:
        return pykey.HASH_SHA512
    raise UnknownCOSEAlgorithmError(algorithm)


# COSE_Signature = [
#     protected : serialized_map,
#     unprotected : {},
#     signature : bstr
# ]


def coseSignature(payload, algorithm, signingKey, signingCertificate, bodyProtected):
    """Returns a COSE_Signature structure.
    payload is a string representing the data to be signed
    algorithm is one of (ES256, ES384, ES512)
    signingKey is a pykey.ECKey to sign the data with
    signingCertificate is a byte string
    bodyProtected is the serialized byte string of the protected body header
    """
    protected = {ALG: algorithm, KID: signingCertificate}
    protectedEncoded = dumps(protected)
    # Sig_structure = [
    #     context : "Signature"
    #     body_protected : bodyProtected
    #     sign_protected : protectedEncoded
    #     external_aad : nil
    #     payload : bstr
    # ]
    sigStructure = ["Signature", bodyProtected, protectedEncoded, None, payload]
    sigStructureEncoded = dumps(sigStructure)
    pykeyHash = coseAlgorithmToPykeyHash(algorithm)
    signature = signingKey.signRaw(sigStructureEncoded, pykeyHash)
    return [protectedEncoded, {}, signature]


# COSE_Sign = [
#     protected : serialized_map,
#     unprotected : {},
#     payload : nil,
#     signatures : [+ COSE_Signature]
# ]


def coseSig(payload, intermediates, signatures):
    """Returns the entire (tagged) COSE_Sign structure.
    payload is a string representing the data to be signed
    intermediates is an array of byte strings
    signatures is an array of (algorithm, signingKey,
               signingCertificate) triplets to be passed to
               coseSignature
    """
    protected = {KID: intermediates}
    protectedEncoded = dumps(protected)
    coseSignatures = []
    for algorithm, signingKey, signingCertificate in signatures:
        coseSignatures.append(
            coseSignature(
                payload, algorithm, signingKey, signingCertificate, protectedEncoded
            )
        )
    tagged = CBORTag(COSE_Sign, [protectedEncoded, {}, None, coseSignatures])
    return dumps(tagged)


def walkDirectory(directory):
    """Given a relative path to a directory, enumerates the
    files in the tree rooted at that location. Returns a list
    of pairs of paths to those files. The first in each pair
    is the full path to the file. The second in each pair is
    the path to the file relative to the directory itself."""
    paths = []
    for path, _dirs, files in os.walk(directory):
        for f in files:
            fullPath = os.path.join(path, f)
            internalPath = re.sub(r"^/", "", fullPath.replace(directory, ""))
            paths.append((fullPath, internalPath))
    return paths


def addManifestEntry(filename, hashes, contents, entries):
    """Helper function to fill out a manifest entry.
    Takes the filename, a list of (hash function, hash function name)
    pairs to use, the contents of the file, and the current list
    of manifest entries."""
    entry = "Name: %s\n" % filename
    for hashFunc, name in hashes:
        base64hash = b64encode(hashFunc(contents).digest()).decode("ascii")
        entry += "%s-Digest: %s\n" % (name, base64hash)
    entries.append(entry)


def getCert(subject, keyName, issuerName, ee, issuerKey="", validity=""):
    """Helper function to create an X509 cert from a specification.
    Takes the subject, the subject key name to use, the issuer name,
    a bool whether this is an EE cert or not, and optionally an issuer key
    name."""
    certSpecification = (
        "issuer:%s\n" % issuerName
        + "subject:"
        + subject
        + "\n"
        + "subjectKey:%s\n" % keyName
    )
    if ee:
        certSpecification += "extension:keyUsage:digitalSignature"
    else:
        certSpecification += (
            "extension:basicConstraints:cA,\n"
            + "extension:keyUsage:cRLSign,keyCertSign"
        )
    if issuerKey:
        certSpecification += "\nissuerKey:%s" % issuerKey
    if validity:
        certSpecification += "\nvalidity:%s" % validity
    certSpecificationStream = StringIO()
    print(certSpecification, file=certSpecificationStream)
    certSpecificationStream.seek(0)
    return pycert.Certificate(certSpecificationStream)


def coseAlgorithmToSignatureParams(coseAlgorithm, issuerName, issuerKey, certValidity):
    """Given a COSE algorithm ('ES256', 'ES384', 'ES512'), an issuer
    name, the name of the issuer's key, and a validity period, returns a
    (algorithm id, pykey.ECCKey, encoded certificate) triplet for use
    with coseSig.
    """
    if coseAlgorithm == "ES256":
        keyName = "secp256r1"
        algId = ES256
    elif coseAlgorithm == "ES384":
        keyName = "secp384r1"
        algId = ES384
    elif coseAlgorithm == "ES512":
        keyName = "secp521r1"  # COSE uses the hash algorithm; this is the curve
        algId = ES512
    else:
        raise UnknownCOSEAlgorithmError(coseAlgorithm)
    key = pykey.ECCKey(keyName)
    # The subject must differ to avoid errors when importing into NSS later.
    ee = getCert(
        "xpcshell signed app test signer " + keyName,
        keyName,
        issuerName,
        True,
        issuerKey,
        certValidity,
    )
    return (algId, key, ee.toDER())


def signZip(
    appDirectory,
    outputFile,
    issuerName,
    rootName,
    rootKey,
    certValidity,
    manifestHashes,
    signatureHashes,
    pkcs7Hashes,
    coseAlgorithms,
    emptySignerInfos,
    headerPaddingFactor,
):
    """Given a directory containing the files to package up, an output
    filename to write to, the name of the issuer of the signing
    certificate, the name of trust anchor, the name of the trust
    anchor's key, a list of hash algorithms to use in the manifest file,
    a similar list for the signature file, a similar list for the pkcs#7
    signature, a list of COSE signature algorithms to include, whether
    the pkcs#7 signer info should be kept empty, and how many MB to pad
    the manifests by (to test handling large manifest files), packages
    up the files in the directory and creates the output as
    appropriate."""
    # The header of each manifest starts with the magic string
    # 'Manifest-Version: 1.0' and ends with a blank line. There can be
    # essentially anything after the first line before the blank line.
    mfEntries = ["Manifest-Version: 1.0"]
    if headerPaddingFactor > 0:
        # In this format, each line can only be 72 bytes long. We make
        # our padding 50 bytes per line (49 of content and one newline)
        # so the math is easy.
        singleLinePadding = "a" * 49
        # 1000000 / 50 = 20000
        allPadding = [singleLinePadding] * (headerPaddingFactor * 20000)
        mfEntries.extend(allPadding)
    # Append the blank line.
    mfEntries.append("")

    issuerKey = rootKey
    with zipfile.ZipFile(outputFile, "w", zipfile.ZIP_DEFLATED) as outZip:
        for fullPath, internalPath in walkDirectory(appDirectory):
            with open(fullPath, "rb") as inputFile:
                contents = inputFile.read()
            outZip.writestr(internalPath, contents)

            # Add the entry to the manifest we're building
            addManifestEntry(internalPath, manifestHashes, contents, mfEntries)

        if len(coseAlgorithms) > 0:
            coseManifest = "\n".join(mfEntries)
            outZip.writestr("META-INF/cose.manifest", coseManifest)
            coseManifestBytes = coseManifest.encode()
            addManifestEntry(
                "META-INF/cose.manifest", manifestHashes, coseManifestBytes, mfEntries
            )
            intermediates = []
            coseIssuerName = issuerName
            if rootName:
                issuerKey = "default"
                coseIssuerName = "xpcshell signed app test issuer"
                intermediate = getCert(
                    coseIssuerName,
                    issuerKey,
                    rootName,
                    False,
                    rootKey,
                    certValidity,
                )
                intermediate = intermediate.toDER()
                intermediates.append(intermediate)
            signatures = [
                coseAlgorithmToSignatureParams(
                    coseAlgorithm,
                    coseIssuerName,
                    issuerKey,
                    certValidity,
                )
                for coseAlgorithm in coseAlgorithms
            ]
            coseSignatureBytes = coseSig(coseManifestBytes, intermediates, signatures)
            outZip.writestr("META-INF/cose.sig", coseSignatureBytes)
            addManifestEntry(
                "META-INF/cose.sig", manifestHashes, coseSignatureBytes, mfEntries
            )

        if len(pkcs7Hashes) != 0 or emptySignerInfos:
            mfContents = "\n".join(mfEntries)
            sfContents = "Signature-Version: 1.0\n"
            for hashFunc, name in signatureHashes:
                hashed = hashFunc(mfContents.encode()).digest()
                base64hash = b64encode(hashed).decode("ascii")
                sfContents += "%s-Digest-Manifest: %s\n" % (name, base64hash)

            cmsSpecification = ""
            for name in pkcs7Hashes:
                hashFunc, _ = hashNameToFunctionAndIdentifier(name)
                cmsSpecification += "%s:%s\n" % (
                    name,
                    hashFunc(sfContents.encode()).hexdigest(),
                )
            cmsSpecification += (
                "signer:\n"
                + "issuer:%s\n" % issuerName
                + "subject:xpcshell signed app test signer\n"
                + "extension:keyUsage:digitalSignature"
            )
            if issuerKey != "default":
                cmsSpecification += "\nissuerKey:%s" % issuerKey
            if certValidity:
                cmsSpecification += "\nvalidity:%s" % certValidity
            cmsSpecificationStream = StringIO()
            print(cmsSpecification, file=cmsSpecificationStream)
            cmsSpecificationStream.seek(0)
            cms = pycms.CMS(cmsSpecificationStream)
            p7 = cms.toDER()
            outZip.writestr("META-INF/A.RSA", p7)
            outZip.writestr("META-INF/A.SF", sfContents)
            outZip.writestr("META-INF/MANIFEST.MF", mfContents)


class Error(Exception):
    """Base class for exceptions in this module."""

    pass


class UnknownHashAlgorithmError(Error):
    """Helper exception type to handle unknown hash algorithms."""

    def __init__(self, name):
        super(UnknownHashAlgorithmError, self).__init__()
        self.name = name

    def __str__(self):
        return "Unknown hash algorithm %s" % repr(self.name)


class UnknownCOSEAlgorithmError(Error):
    """Helper exception type to handle unknown COSE algorithms."""

    def __init__(self, name):
        super(UnknownCOSEAlgorithmError, self).__init__()
        self.name = name

    def __str__(self):
        return "Unknown COSE algorithm %s" % repr(self.name)


def hashNameToFunctionAndIdentifier(name):
    if name == "sha1":
        return (sha1, "SHA1")
    if name == "sha256":
        return (sha256, "SHA256")
    raise UnknownHashAlgorithmError(name)


def main(outputFile, appPath, *args):
    """Main entrypoint. Given an already-opened file-like
    object, a path to the app directory to sign, and some
    optional arguments, signs the contents of the directory and
    writes the resulting package to the 'file'."""
    parser = argparse.ArgumentParser(description="Sign an app.")
    parser.add_argument(
        "-i",
        "--issuer",
        action="store",
        help="Issuer name",
        default="xpcshell signed apps test root",
    )
    parser.add_argument("-r", "--root", action="store", help="Root name", default="")
    parser.add_argument(
        "-k",
        "--root-key",
        action="store",
        help="Root key name",
        default="default",
    )
    parser.add_argument(
        "--cert-validity",
        action="store",
        help="Certificate validity; YYYYMMDD-YYYYMMDD or duration in days",
        default="",
    )
    parser.add_argument(
        "-m",
        "--manifest-hash",
        action="append",
        help="Hash algorithms to use in manifest",
        default=[],
    )
    parser.add_argument(
        "-s",
        "--signature-hash",
        action="append",
        help="Hash algorithms to use in signature file",
        default=[],
    )
    parser.add_argument(
        "-c",
        "--cose-sign",
        action="append",
        help="Append a COSE signature with the given "
        + "algorithms (out of ES256, ES384, and ES512)",
        default=[],
    )
    parser.add_argument(
        "-z",
        "--pad-headers",
        action="store",
        default=0,
        help="Pad the header sections of the manifests "
        + "with X MB of repetitive data",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "-p",
        "--pkcs7-hash",
        action="append",
        help="Hash algorithms to use in PKCS#7 signature",
        default=[],
    )
    group.add_argument(
        "-e",
        "--empty-signerInfos",
        action="store_true",
        help="Emit pkcs#7 SignedData with empty signerInfos",
    )
    parsed = parser.parse_args(args)
    if len(parsed.manifest_hash) == 0:
        parsed.manifest_hash.append("sha256")
    if len(parsed.signature_hash) == 0:
        parsed.signature_hash.append("sha256")
    signZip(
        appPath,
        outputFile,
        parsed.issuer,
        parsed.root,
        parsed.root_key,
        parsed.cert_validity,
        [hashNameToFunctionAndIdentifier(h) for h in parsed.manifest_hash],
        [hashNameToFunctionAndIdentifier(h) for h in parsed.signature_hash],
        parsed.pkcs7_hash,
        parsed.cose_sign,
        parsed.empty_signerInfos,
        int(parsed.pad_headers),
    )
