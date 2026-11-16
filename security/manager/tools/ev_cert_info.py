#!/usr/bin/env python3
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Extract EV root data from a certificate and print a kEVInfos entry.

Produces a block ready to paste into
security/certverifier/ExtendedValidation.cpp.

Usage:
    ./ev_cert_info.py [--oid OID] cert.cer

Accepts DER (.cer/.crt) or PEM input; the format is auto-detected. The OID
defaults to the CA/Browser Forum EV OID.

Requires the cryptography package.
"""

import argparse
import base64
import sys
import textwrap

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.x509.oid import NameOID

DEFAULT_OID = "2.23.140.1.1"

ATTR_LABELS = {
    NameOID.COMMON_NAME: "CN",
    NameOID.ORGANIZATION_NAME: "O",
    NameOID.ORGANIZATIONAL_UNIT_NAME: "OU",
    NameOID.LOCALITY_NAME: "L",
    NameOID.STATE_OR_PROVINCE_NAME: "ST",
    NameOID.COUNTRY_NAME: "C",
}


def load_cert(path):
    data = open(path, "rb").read()
    if b"-----BEGIN CERTIFICATE-----" in data:
        return x509.load_pem_x509_certificate(data)
    return x509.load_der_x509_certificate(data)


def format_name(name):
    """Format a distinguished name like the comments in ExtendedValidation.cpp,
    e.g. CN=...,O="...",C=JP (most-specific attribute first, values with commas
    quoted)."""
    parts = []
    for rdn in reversed(list(name.rdns)):
        for attr in rdn:
            label = ATTR_LABELS.get(attr.oid, attr.oid.dotted_string)
            value = attr.value
            if "," in value:
                value = f'"{value}"'
            parts.append(f"{label}={value}")
    return ",".join(parts)


def format_fingerprint(fp):
    hexbytes = [f"0x{b:02X}" for b in fp]
    lines = []
    for i in range(0, len(hexbytes), 12):
        lines.append(", ".join(hexbytes[i : i + 12]))
    body = ",\n      ".join(lines)
    return f"    {{ {body} }},"


def format_base64_literal(raw):
    """Split base64 into 64-char C string literals, one per line."""
    b64 = base64.b64encode(raw).decode()
    chunks = textwrap.wrap(b64, 64)
    return "\n".join(f'    "{c}"' for c in chunks) + ","


def serial_content_octets(serial):
    """Serial as stored in the cert (content octets, no INTEGER tag/length)."""
    if serial == 0:
        return b"\x00"
    length = (serial.bit_length() + 8) // 8
    return serial.to_bytes(length, "big")


def emit(path, oid):
    cert = load_cert(path)
    if cert.issuer != cert.subject:
        raise ValueError("issuer does not match subject")
    fp = cert.fingerprint(hashes.SHA256())
    issuer_der = cert.issuer.public_bytes()
    serial = serial_content_octets(cert.serial_number)

    print("  {")
    print(f"    // {format_name(cert.issuer)}")
    print(f'    "{oid}",')
    print(format_fingerprint(fp))
    print(format_base64_literal(issuer_der))
    print(f'    "{base64.b64encode(serial).decode()}",')
    print("  },")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("cert", help="certificate file, DER or PEM")
    parser.add_argument(
        "--oid",
        default=DEFAULT_OID,
        help="EV policy OID (default: %(default)s)",
    )
    args = parser.parse_args()
    try:
        emit(args.cert, args.oid)
    except Exception as e:
        print(f"error processing {args.cert}: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
