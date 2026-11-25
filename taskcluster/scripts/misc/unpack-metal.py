# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from http.client import HTTPException
from urllib.request import urlopen


def find_7zz():
    if fetches := os.environ.get("MOZ_FETCHES_DIR"):
        sevenz = os.path.join(fetches, "7zz", "7zz")
        if os.path.exists(sevenz):
            return sevenz
    raise FileNotFoundError(
        "Could not find 7zz. Is the linux64-7zz toolchain in fetches?"
    )


def download(url, sha512, path):
    error = None
    for attempt in range(3):
        if attempt != 0:
            print(f"Failed to download from {url}. Retrying", file=sys.stderr)

        try:
            hash = hashlib.sha512()
            with urlopen(url) as fh, open(path, "wb") as out:
                # Equivalent to shutil.copyfileobj, but computes sha512 at the
                # same time.
                while True:
                    buf = fh.read(1024 * 1024)
                    if not buf:
                        break
                    hash.update(buf)
                    out.write(buf)
        except (OSError, HTTPException) as e:
            error = e
            continue

        digest = hash.hexdigest()
        if digest == sha512:
            return
        error = Exception(f"(actual) {digest} != (expected) {sha512}")
    raise error


def unpack_metal_toolchain(url, sha512, extract_prefix, out_dir="."):
    with tempfile.TemporaryDirectory() as tmpdir:
        dmg = os.path.join(tmpdir, "metal.dmg")
        download(url, sha512, dmg)

        extract_dir = os.path.join(tmpdir, "extract")
        subprocess.check_call(
            [find_7zz(), "x", f"-o{extract_dir}", dmg],
            stdout=subprocess.DEVNULL,
        )

        toolchain = os.path.join(extract_dir, extract_prefix)
        if not os.path.isdir(toolchain):
            raise Exception(f"{extract_prefix} not found in {dmg}")

        os.makedirs(out_dir, exist_ok=True)
        for entry in os.listdir(toolchain):
            shutil.move(os.path.join(toolchain, entry), out_dir)

    for tool in ("metal", "metallib"):
        path = os.path.join(out_dir, "usr", "bin", tool)
        if not os.access(path, os.X_OK):
            raise Exception(f"{path} is missing or not executable")


if __name__ == "__main__":
    unpack_metal_toolchain(*sys.argv[1:])
