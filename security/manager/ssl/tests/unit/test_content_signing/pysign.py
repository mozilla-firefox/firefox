#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Create an ECDSA signature on the P-384 curve using the SHA-384 hash of data from
stdin. The key used for the signature is the secp384r1Encoded key used in pykey
and pycert.

The certificates for the content signature tests make use of this program.
You can use pysign.py like this:

cat test.txt | python pysign.py > test.txt.signature
"""

import base64
import pathlib
import sys


# For pykey, find the relative file location and add it to path
toolsDir = (pathlib.Path(__file__).parents[4] / "tools").resolve()
sys.path.append(str(toolsDir))
import pykey

data = sys.stdin.buffer.read()

key = pykey.ECCKey("secp384r1")
sig = key.signRaw(b"Content-Signature:\00" + data, pykey.HASH_SHA384)
print(str(base64.b64encode(sig)).replace("+", "-").replace("/", "_"))
