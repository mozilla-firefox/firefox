#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
This script downloads and repacks official rust language builds
with the necessary tool and target support for the Firefox
build environment.
"""

import argparse
import errno
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
import textwrap
from contextlib import contextmanager

import zstandard


def log(msg):
    print("repack: %s" % msg, flush=True)


def fetch_file(url):
    """Download a file from the given url if it's not already present.

    Returns the SHA-2 256-bit hash of the received file."""
    import requests

    filename = os.path.basename(url)
    sha = hashlib.sha256()
    size = 4096
    if os.path.exists(filename):
        with open(filename, "rb") as fd:
            while True:
                block = fd.read(size)
                if not block:
                    return sha.hexdigest()
                sha.update(block)
        log("Could not calculate checksum!")
        return None
    r = requests.get(url, stream=True)
    r.raise_for_status()
    with open(filename, "wb") as fd:
        for chunk in r.iter_content(size):
            fd.write(chunk)
            sha.update(chunk)
        return sha.hexdigest()


def check_call_with_input(cmd, input_data):
    """Invoke a command, passing the input String over stdin.

    This is like subprocess.check_call, but allows piping
    input to interactive commands."""
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    p.communicate(input_data)
    if p.wait():
        raise subprocess.CalledProcessError(p.returncode, cmd)


def setup_gpg():
    """Add the signing key to the current gpg config.

    Import a hard-coded copy of the release signing public key
    and mark it trusted in the gpg database so subsequent
    signature checks can succeed or fail cleanly."""
    keyid = "0x85AB96E6FA1BE5FE"
    log("Importing signing key %s..." % keyid)
    key = b"""
-----BEGIN PGP PUBLIC KEY BLOCK-----

mQINBFJEwMkBEADlPACa2K7reD4x5zd8afKx75QYKmxqZwywRbgeICeD4bKiQoJZ
dUjmn1LgrGaXuBMKXJQhyA34e/1YZel/8et+HPE5XpljBfNYXWbVocE1UMUTnFU9
CKXa4AhJ33f7we2/QmNRMUifw5adPwGMg4D8cDKXk02NdnqQlmFByv0vSaArR5kn
gZKnLY6o0zZ9Buyy761Im/ShXqv4ATUgYiFc48z33G4j+BDmn0ryGr1aFdP58tHp
gjWtLZs0iWeFNRDYDje6ODyu/MjOyuAWb2pYDH47Xu7XedMZzenH2TLM9yt/hyOV
xReDPhvoGkaO8xqHioJMoPQi1gBjuBeewmFyTSPS4deASukhCFOcTsw/enzJagiS
ZAq6Imehduke+peAL1z4PuRmzDPO2LPhVS7CDXtuKAYqUV2YakTq8MZUempVhw5n
LqVaJ5/XiyOcv405PnkT25eIVVVghxAgyz6bOU/UMjGQYlkUxI7YZ9tdreLlFyPR
OUL30E8q/aCd4PGJV24yJ1uit+yS8xjyUiMKm4J7oMP2XdBN98TUfLGw7SKeAxyU
92BHlxg7yyPfI4TglsCzoSgEIV6xoGOVRRCYlGzSjUfz0bCMCclhTQRBkegKcjB3
sMTyG3SPZbjTlCqrFHy13e6hGl37Nhs8/MvXUysq2cluEISn5bivTKEeeQARAQAB
tERSdXN0IExhbmd1YWdlIChUYWcgYW5kIFJlbGVhc2UgU2lnbmluZyBLZXkpIDxy
dXN0LWtleUBydXN0LWxhbmcub3JnPokCOAQTAQIAIgUCUkTAyQIbAwYLCQgHAwIG
FQgCCQoLBBYCAwECHgECF4AACgkQhauW5vob5f5fYQ//b1DWK1NSGx5nZ3zYZeHJ
9mwGCftIaA2IRghAGrNf4Y8DaPqR+w1OdIegWn8kCoGfPfGAVW5XXJg+Oxk6QIaD
2hJojBUrq1DALeCZVewzTVw6BN4DGuUexsc53a8DcY2Yk5WE3ll6UKq/YPiWiPNX
9r8FE2MJwMABB6mWZLqJeg4RCrriBiCG26NZxGE7RTtPHyppoVxWKAFDiWyNdJ+3
UnjldWrT9xFqjqfXWw9Bhz8/EoaGeSSbMIAQDkQQpp1SWpljpgqvctZlc5fHhsG6
lmzW5RM4NG8OKvq3UrBihvgzwrIfoEDKpXbk3DXqaSs1o81NH5ftVWWbJp/ywM9Q
uMC6n0YWiMZMQ1cFBy7tukpMkd+VPbPkiSwBhPkfZIzUAWd74nanN5SKBtcnymgJ
+OJcxfZLiUkXRj0aUT1GLA9/7wnikhJI+RvwRfHBgrssXBKNPOfXGWajtIAmZc2t
kR1E8zjBVLId7r5M8g52HKk+J+y5fVgJY91nxG0zf782JjtYuz9+knQd55JLFJCO
hhbv3uRvhvkqgauHagR5X9vCMtcvqDseK7LXrRaOdOUDrK/Zg/abi5d+NIyZfEt/
ObFsv3idAIe/zpU6xa1nYNe3+Ixlb6mlZm3WCWGxWe+GvNW/kq36jZ/v/8pYMyVO
p/kJqnf9y4dbufuYBg+RLqC5Ag0EUkTAyQEQANxy2tTSeRspfrpBk9+ju+KZ3zc4
umaIsEa5DxJ2zIKHywVAR67Um0K1YRG07/F5+tD9TIRkdx2pcmpjmSQzqdk3zqa9
2Zzeijjz2RNyBY8qYmyE08IncjTsFFB8OnvdXcsAgjCFmI1BKnePxrABL/2k8X18
aysPb0beWqQVsi5FsSpAHu6k1kaLKc+130x6Hf/YJAjeo+S7HeU5NeOz3zD+h5bA
Q25qMiVHX3FwH7rFKZtFFog9Ogjzi0TkDKKxoeFKyADfIdteJWFjOlCI9KoIhfXq
Et9JMnxApGqsJElJtfQjIdhMN4Lnep2WkudHAfwJ/412fe7wiW0rcBMvr/BlBGRY
vM4sTgN058EwIuY9Qmc8RK4gbBf6GsfGNJjWozJ5XmXElmkQCAvbQFoAfi5TGfVb
77QQrhrQlSpfIYrvfpvjYoqj618SbU6uBhzh758gLllmMB8LOhxWtq9eyn1rMWyR
KL1fEkfvvMc78zP+Px6yDMa6UIez8jZXQ87Zou9EriLbzF4QfIYAqR9LUSMnLk6K
o61tSFmFEDobC3tc1jkSg4zZe/wxskn96KOlmnxgMGO0vJ7ASrynoxEnQE8k3WwA
+/YJDwboIR7zDwTy3Jw3mn1FgnH+c7Rb9h9geOzxKYINBFz5Hd0MKx7kZ1U6WobW
KiYYxcCmoEeguSPHABEBAAGJAh8EGAECAAkFAlJEwMkCGwwACgkQhauW5vob5f7f
FA//Ra+itJF4NsEyyhx4xYDOPq4uj0VWVjLdabDvFjQtbBLwIyh2bm8uO3AY4r/r
rM5WWQ8oIXQ2vvXpAQO9g8iNlFez6OLzbfdSG80AG74pQqVVVyCQxD7FanB/KGge
tAoOstFxaCAg4nxFlarMctFqOOXCFkylWl504JVIOvgbbbyj6I7qCUmbmqazBSMU
K8c/Nz+FNu2Uf/lYWOeGogRSBgS0CVBcbmPUpnDHLxZWNXDWQOCxbhA1Uf58hcyu
036kkiWHh2OGgJqlo2WIraPXx1cGw1Ey+U6exbtrZfE5kM9pZzRG7ZY83CXpYWMp
kyVXNWmf9JcIWWBrXvJmMi0FDvtgg3Pt1tnoxqdilk6yhieFc8LqBn6CZgFUBk0t
NSaWk3PsN0N6Ut8VXY6sai7MJ0Gih1gE1xadWj2zfZ9sLGyt2jZ6wK++U881YeXA
ryaGKJ8sIs182hwQb4qN7eiUHzLtIh8oVBHo8Q4BJSat88E5/gOD6IQIpxc42iRL
T+oNZw1hdwNyPOT1GMkkn86l3o7klwmQUWCPm6vl1aHp3omo+GHC63PpNFO5RncJ
Ilo3aBKKmoE5lDSMGE8KFso5awTo9z9QnVPkRsk6qeBYit9xE3x3S+iwjcSg0nie
aAkc0N00nc9V9jfPvt4z/5A5vjHh+NhFwH5h2vBJVPdsz6m5Ag0EVI9keAEQAL3R
oVsHncJTmjHfBOV4JJsvCum4DuJDZ/rDdxauGcjMUWZaG338ZehnDqG1Yn/ys7zE
aKYUmqyT+XP+M2IAQRTyxwlU1RsDlemQfWrESfZQCCmbnFScL0E7cBzy4xvtInQe
UaFgJZ1BmxbzQrx+eBBdOTDv7RLnNVygRmMzmkDhxO1IGEu1+3ETIg/DxFE7VQY0
It/Ywz+nHu1o4Hemc/GdKxu9hcYvcRVc/Xhueq/zcIM96l0m+CFbs0HMKCj8dgMe
Ng6pbbDjNM+cV+5BgpRdIpE2l9W7ImpbLihqcZt47J6oWt/RDRVoKOzRxjhULVyV
2VP9ESr48HnbvxcpvUAEDCQUhsGpur4EKHFJ9AmQ4zf91gWLrDc6QmlACn9o9ARU
fOV5aFsZI9ni1MJEInJTP37stz/uDECRie4LTL4O6P4Dkto8ROM2wzZq5CiRNfnT
PP7ARfxlCkpg+gpLYRlxGUvRn6EeYwDtiMQJUQPfpGHSvThUlgDEsDrpp4SQSmdA
CB+rvaRqCawWKoXs0In/9wylGorRUupeqGC0I0/rh+f5mayFvORzwy/4KK4QIEV9
aYTXTvSRl35MevfXU1Cumlaqle6SDkLr3ZnFQgJBqap0Y+Nmmz2HfO/pohsbtHPX
92SN3dKqaoSBvzNGY5WT3CsqxDtik37kR3f9/DHpABEBAAGJBD4EGAECAAkFAlSP
ZHgCGwICKQkQhauW5vob5f7BXSAEGQECAAYFAlSPZHgACgkQXLSpNHs7CdwemA/+
KFoGuFqU0uKT9qblN4ugRyil5itmTRVffl4tm5OoWkW8uDnu7Ue3vzdzy+9NV8X2
wRG835qjXijWP++AGuxgW6LB9nV5OWiKMCHOWnUjJQ6pNQMAgSN69QzkFXVF/q5f
bkma9TgSbwjrVMyPzLSRwq7HsT3V02Qfr4cyq39QeILGy/NHW5z6LZnBy3BaVSd0
lGjCEc3yfH5OaB79na4W86WCV5n4IT7cojFM+LdL6P46RgmEtWSG3/CDjnJl6BLR
WqatRNBWLIMKMpn+YvOOL9TwuP1xbqWr1vZ66wksm53NIDcWhptpp0KEuzbU0/Dt
OltBhcX8tOmO36LrSadX9rwckSETCVYklmpAHNxPml011YNDThtBidvsicw1vZwR
HsXn+txlL6RAIRN+J/Rw3uOiJAqN9Qgedpx2q+E15t8MiTg/FXtB9SysnskFT/BH
z0USNKJUY0btZBw3eXWzUnZf59D8VW1M/9JwznCHAx0c9wy/gRDiwt9w4RoXryJD
VAwZg8rwByjldoiThUJhkCYvJ0R3xH3kPnPlGXDW49E9R8C2umRC3cYOL4U9dOQ1
5hSlYydF5urFGCLIvodtE9q80uhpyt8L/5jj9tbwZWv6JLnfBquZSnCGqFZRfXlb
Jphk9+CBQWwiZSRLZRzqQ4ffl4xyLuolx01PMaatkQbRaw/+JpgRNlurKQ0PsTrO
8tztO/tpBBj/huc2DGkSwEWvkfWElS5RLDKdoMVs/j5CLYUJzZVikUJRm7m7b+OA
P3W1nbDhuID+XV1CSBmGifQwpoPTys21stTIGLgznJrIfE5moFviOLqD/LrcYlsq
CQg0yleu7SjOs//8dM3mC2FyLaE/dCZ8l2DCLhHw0+ynyRAvSK6aGCmZz6jMjmYF
MXgiy7zESksMnVFMulIJJhR3eB0wx2GitibjY/ZhQ7tD3i0yy9ILR07dFz4pgkVM
afxpVR7fmrMZ0t+yENd+9qzyAZs0ksxORoc2ze90SCx2jwEX/3K+m4I0hP2H/w5W
gqdvuRLiqf+4BGW4zqWkLLlNIe/okt0r82SwHtDN0Ui1asmZTGj6sm8SXtwx+5cE
38MttWqjDiibQOSthRVcETByRYM8KcjYSUCi4PoBc3NpDONkFbZm6XofR/f5mTcl
2jDw6fIeVc4Hd1jBGajNzEqtneqqbdAkPQaLsuD2TMkQfTDJfE/IljwjrhDa9Mi+
odtnMWq8vlwOZZ24/8/BNK5qXuCYL67O7AJB4ZQ6BT+g4z96iRLbupzu/XJyXkQF
rOY/Ghegvn7fDrnt2KC9MpgeFBXzUp+k5rzUdF8jbCx5apVjA1sWXB9Kh3L+DUwF
Mve696B5tlHyc1KxjHR6w9GRsh4=
=5FXw
-----END PGP PUBLIC KEY BLOCK-----
"""
    check_call_with_input(["gpg", "--import"], key)
    check_call_with_input(
        ["gpg", "--command-fd", "0", "--edit-key", keyid], b"trust\n5\ny\n"
    )


def verify_sha(filename, sha):
    """Verify that the checksum file matches the given sha digest."""
    sha_filename = filename + ".sha256"
    with open(sha_filename) as f:
        # Older sha256 files would contain `sha filename`, but more recent
        # ones only contain `sha`.
        checksum = f.readline().split()[0]
        if checksum != sha:
            raise ValueError("Checksum mismatch in %s" % filename)
        return True
    log("No checksum file for %s!" % filename)
    return False


def fetch(url, validate=True):
    """Download and verify a package url."""
    base = os.path.basename(url)
    log("Fetching %s..." % base)
    if validate:
        fetch_file(url + ".asc")
        fetch_file(url + ".sha256")
    sha = fetch_file(url)
    if validate:
        log("Verifying %s..." % base)
        verify_sha(base, sha)
        subprocess.check_call([
            "gpg",
            "--keyid-format",
            "0xlong",
            "--verify",
            base + ".asc",
            base,
        ])
    return sha


def install(filename, target):
    """Run a package's installer script against the given target directory."""
    log("Unpacking %s..." % filename)
    subprocess.check_call(["tar", "xf", filename])
    basename = filename.split(".tar")[0]
    log("Installing %s..." % basename)
    install_cmd = [os.path.join(basename, "install.sh")]
    install_cmd += ["--prefix=" + os.path.abspath(target)]
    install_cmd += ["--disable-ldconfig"]
    subprocess.check_call(install_cmd)
    log("Cleaning %s..." % basename)
    shutil.rmtree(basename)


def package(manifest, pkg, target):
    """Pull out the package dict for a particular package and target
    from the given manifest."""
    version = manifest["pkg"][pkg]["version"]
    if target in manifest["pkg"][pkg]["target"]:
        info = manifest["pkg"][pkg]["target"][target]
    else:
        # rust-src is the same for all targets, and has a literal '*' in the
        # section key/name instead of a target
        info = manifest["pkg"][pkg]["target"]["*"]
    if "xz_url" in info:
        info["url"] = info.pop("xz_url")
        info["hash"] = info.pop("xz_hash")
    return (version, info)


def fetch_package(manifest, pkg, host):
    version, info = package(manifest, pkg, host)
    if not info["available"]:
        log("%s marked unavailable for %s" % (pkg, host))
        raise KeyError

    log("%s %s\n  %s\n  %s" % (pkg, version, info["url"], info["hash"]))
    sha = fetch(info["url"], info["hash"] is not None)
    if info["hash"] and sha != info["hash"]:
        log(
            "Checksum mismatch: package resource is different from manifest\n  %s" % sha
        )
        raise AssertionError
    return info


def fetch_std(manifest, targets):
    stds = []
    for target in targets:
        stds.append(fetch_package(manifest, "rust-std", target))
        analysis = fetch_optional(manifest, "rust-analysis", target)
        if analysis:
            stds.append(analysis)
        else:
            log(f"Missing rust-analysis for {target}")
            # If it's missing for one of the searchfox targets, explicitly
            # error out.
            if target in (
                "x86_64-unknown-linux-gnu",
                "x86_64-apple-darwin",
                "x86_64-pc-windows-msvc",
                "thumbv7neon-linux-androideabi",
            ):
                raise AssertionError

    return stds


def fetch_optional(manifest, pkg, host):
    try:
        return fetch_package(manifest, pkg, host)
    except KeyError:
        # The package is not available, oh well!
        return None


@contextmanager
def chdir(path):
    d = os.getcwd()
    log('cd "%s"' % path)
    os.chdir(path)
    try:
        yield
    finally:
        log('cd "%s"' % d)
        os.chdir(d)


def build_tar_package(name, base, directory):
    name = os.path.realpath(name)
    log(f"tarring {name} from {base}/{directory}")
    assert name.endswith(".tar.zst")

    cctx = zstandard.ZstdCompressor()
    with open(name, "wb") as f, cctx.stream_writer(f) as z:
        with tarfile.open(mode="w|", fileobj=z) as tf:
            with chdir(base):
                tf.add(directory)


def fetch_manifest(channel="stable", host=None, targets=()):
    import requests
    import toml

    if channel.startswith("bors-"):
        assert host
        rev = channel[len("bors-") :]
        base_url = "https://s3-us-west-1.amazonaws.com/rust-lang-ci2/rustc-builds"
        manifest = {
            "date": "some date",
            "pkg": {},
        }

        def target(url):
            return {
                "url": url,
                "hash": None,
                "available": requests.head(url).status_code == 200,
            }

        for pkg in (
            "cargo",
            "rustc",
            "rustfmt-preview",
            "clippy-preview",
            "rust-analyzer-preview",
        ):
            manifest["pkg"][pkg] = {
                "version": "bors",
                "target": {
                    host: target(f"{base_url}/{rev}/{pkg}-nightly-{host}.tar.xz"),
                },
            }
        manifest["pkg"]["rust-src"] = {
            "version": "bors",
            "target": {
                "*": target(f"{base_url}/{rev}/rust-src-nightly.tar.xz"),
            },
        }
        for pkg in ("rust-std", "rust-analysis"):
            manifest["pkg"][pkg] = {
                "version": "bors",
                "target": {
                    t: target(f"{base_url}/{rev}/{pkg}-nightly-{t}.tar.xz")
                    for t in sorted(set(targets) | set([host]))
                },
            }
        return manifest
    if channel.startswith(("beta-", "nightly-")):
        channel, date = channel.split("-", 1)
        prefix = "/" + date
    else:
        prefix = ""
    url = "https://static.rust-lang.org/dist%s/channel-rust-%s.toml" % (prefix, channel)
    req = requests.get(url)
    req.raise_for_status()
    manifest = toml.loads(req.text)
    if manifest["manifest-version"] != "2":
        raise NotImplementedError(
            "Unrecognized manifest version %s." % manifest["manifest-version"]
        )
    return manifest


def patch_src(patch, module):
    log(f"Patching Rust src... {module} with {patch}")
    patch = os.path.realpath(patch)
    subprocess.check_call(["patch", "-d", module, "-p1", "-i", patch, "--fuzz=0", "-s"])


def build_src(install_dir, host, targets, patches):
    install_dir = os.path.abspath(install_dir)
    fetches = os.environ["MOZ_FETCHES_DIR"]
    rust_dir = os.path.join(fetches, "rust")
    patch_dir = os.path.join(os.environ["GECKO_PATH"], "build", "build-rust")

    # Clear and remake any previous install directory.
    try:
        shutil.rmtree(install_dir)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise
    os.makedirs(install_dir)

    # Patch the src (see the --patch flag's description for details)
    for p in patches:
        module, colon, file = p.partition(":")
        if not colon:
            module, file = "", p
        patch_file = os.path.join(patch_dir, file)
        patch_module = os.path.join(rust_dir, module)
        patch_src(patch_file, patch_module)

    log("Building Rust...")

    example_config = ""
    for example_toml in ("config.example.toml", "config.toml.example"):
        path = os.path.join(rust_dir, example_toml)
        if os.path.exists(path):
            with open(path) as file:
                example_config = file.read()
                break

    if "ignore-git" in example_config:
        omit_git_hash = "ignore-git"
    else:
        omit_git_hash = "omit-git-hash"

    # Rust builds are configured primarily through a config.toml file.
    #
    # `sysconfdir` is overloaded to be relative instead of absolute.
    # This is the default of `install.sh`, but for whatever reason
    # `x.py install` has its own default of `/etc` which we don't want.
    base_config = textwrap.dedent(
        """
        [build]
        docs = false
        sanitizers = true
        profiler = true
        extended = true
        tools = ["analysis", "cargo", "rustdoc", "rustfmt", "clippy", "src", "rust-analyzer"]
        cargo-native-static = true
        """
    )

    if any("android" in target for target in targets):
        base_config += f"""android-ndk = "{fetches}/android-ndk"\n"""

    base_config += textwrap.dedent(
        f"""
        [rust]
        {omit_git_hash} = false
        use-lld = true

        [install]
        prefix = "/"

        [llvm]
        download-ci-llvm = false
        """
    )
    if "msvc" in host:
        assert all("msvc" in target for target in targets)
        base_config += textwrap.dedent(
            f"""
            ldflags = "-winsysroot:{fetches}/vs"

            [llvm.build-config]
            CMAKE_MT = "llvm-mt.exe"
            CMAKE_ASM_MASM_COMPILER = "ml64.exe"
            """
        )
    else:
        assert all("msvc" not in target for target in targets)

    def target_config(target):
        if "msvc" in target:
            return textwrap.dedent(
                f"""
                [target.{target}]
                cc = "clang-cl.bat"
                cxx = "clang-cl.bat"
                linker = "lld-link.bat"
                ar = "llvm-lib"
                """
            )
        elif "android" in target:
            # Let x.py pick the compiler from the NDK
            return textwrap.dedent(
                f"""
                [target.{target}]
                """
            )
        else:
            # Rust requires these to be specified per-target
            return textwrap.dedent(
                f"""
                [target.{target}]
                cc = "clang"
                cxx = "clang++"
                linker = "clang"
                """
            )

    final_config = base_config
    for target in sorted(set(targets) | set([host])):
        final_config = final_config + target_config(target)

    with open(os.path.join(rust_dir, "config.toml"), "w") as file:
        file.write(final_config)

    # Setup the env so compilers and toolchains are visible
    clang = os.path.join(fetches, "clang")
    clang_bin = os.path.join(clang, "bin")
    clang_lib = os.path.join(clang, "lib")
    sysroot = os.path.join(fetches, "sysroot")

    with tempfile.TemporaryDirectory() as tmpdir:
        # The rust build doesn't offer much in terms of overriding compiler flags
        # when it builds LLVM's compiler-rt, but we want to build with a sysroot.
        # So, we create wrappers for clang and clang++ that add the sysroot to the
        # command line.
        for exe in ("clang", "clang++"):
            tmp_exe = os.path.join(tmpdir, exe)
            with open(tmp_exe, "w") as fh:
                fh.write("#!/bin/sh\n")
                fh.write(f'exec {clang_bin}/{exe} --sysroot={sysroot} "$@"\n')
            os.chmod(tmp_exe, 0o755)

        if "msvc" in host:
            # Same thing for the linker on windows, where we want to add the MSVC
            # sysroot
            with open(os.path.join(tmpdir, "lld-link.bat"), "w") as fh:
                fh.write("@echo off\n")
                fh.write(f"{clang_bin}/lld-link.exe -winsysroot:{fetches}/vs %*")
            # And clang-cl.
            with open(os.path.join(tmpdir, "clang-cl.bat"), "w") as fh:
                fh.write("@echo off\n")
                fh.write(
                    f"{clang_bin}/clang-cl.exe -Xclang -ivfsoverlay -Xclang {fetches}/vs/overlay.yaml -winsysroot {fetches}/vs %*"
                )
            # cc-rs expects to find ml64.exe to build asm files, without a way to override it.
            shutil.copy(f"{clang_bin}/llvm-ml.exe", f"{tmpdir}/ml64.exe")

        env = os.environ.copy()
        env.update({
            "PATH": os.pathsep.join((tmpdir, clang_bin, os.environ["PATH"])),
            "LD_LIBRARY_PATH": clang_lib,
            "DESTDIR": install_dir,
        })
        if "msvc" in host:
            cmake_bin = os.path.join(fetches, "cmake", "bin")
            ninja_bin = os.path.join(fetches, "ninja", "bin")
            env.update({
                "PATH": os.pathsep.join((env["PATH"], cmake_bin, ninja_bin)),
                "CC_x86_64_pc_windows_msvc": "clang-cl.bat",
                "CXX_x86_64_pc_windows_msvc": "clang-cl.bat",
                "AR_x86_64_pc_windows_msvc": "llvm-lib",
                "RUSTC_WINDOWS_RC": "llvm-rc",
            })

        # x.py install does everything we need for us.
        # If you're running into issues, consider using `-vv` to debug it.
        command = [
            "python3",
            "x.py",
            "install",
            "-v",
            "--host",
            host,
            "--build",
            host,
            "--target",
            ",".join(targets),
        ]

        subprocess.check_call(command, stderr=subprocess.STDOUT, env=env, cwd=rust_dir)


def repack(
    host,
    targets,
    channel="stable",
    cargo_channel=None,
    patches=[],
):
    install_dir = "rustc"
    if channel == "dev":
        build_src(install_dir, host, targets, patches)
    else:
        if patches:
            raise ValueError(
                'Patch specified, but channel "%s" is not "dev"!'
                "\nPatches are only for building from source." % channel
            )
        setup_gpg()
        log("Repacking rust for %s supporting %s..." % (host, targets))
        manifest = fetch_manifest(channel, host, targets)
        log("Using manifest for rust %s as of %s." % (channel, manifest["date"]))
        if cargo_channel == channel:
            cargo_manifest = manifest
        else:
            cargo_manifest = fetch_manifest(cargo_channel, host, targets)
            log(
                "Using manifest for cargo %s as of %s."
                % (cargo_channel, cargo_manifest["date"])
            )

        log("Fetching packages...")
        rustc = fetch_package(manifest, "rustc", host)
        cargo = fetch_package(cargo_manifest, "cargo", host)
        stds = fetch_std(manifest, targets)
        rustsrc = fetch_package(manifest, "rust-src", host)
        rustfmt = fetch_optional(manifest, "rustfmt-preview", host)
        clippy = fetch_optional(manifest, "clippy-preview", host)
        rust_analyzer = fetch_optional(manifest, "rust-analyzer-preview", host)

        log("Installing packages...")

        # Clear any previous install directory.
        try:
            shutil.rmtree(install_dir)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
        install(os.path.basename(rustc["url"]), install_dir)
        install(os.path.basename(cargo["url"]), install_dir)
        install(os.path.basename(rustsrc["url"]), install_dir)
        if rustfmt:
            install(os.path.basename(rustfmt["url"]), install_dir)
        if clippy:
            install(os.path.basename(clippy["url"]), install_dir)
        if rust_analyzer:
            install(os.path.basename(rust_analyzer["url"]), install_dir)
        for std in stds:
            install(os.path.basename(std["url"]), install_dir)
            pass

    log("Creating archive...")
    tar_file = install_dir + ".tar.zst"
    build_tar_package(tar_file, ".", install_dir)
    shutil.rmtree(install_dir)
    log("%s is ready." % tar_file)

    upload_dir = os.environ.get("UPLOAD_DIR")
    if upload_dir:
        # Create the upload directory if it doesn't exist.
        try:
            log("Creating upload directory in %s..." % os.path.abspath(upload_dir))
            os.makedirs(upload_dir)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        # Move the tarball to the output directory for upload.
        log("Moving %s to the upload directory..." % tar_file)
        shutil.move(tar_file, upload_dir)


def expand_platform(name):
    """Expand a shortcut name to a full Rust platform string."""
    platforms = {
        "android": "armv7-linux-androideabi",
        "android_x86": "i686-linux-android",
        "android_x86-64": "x86_64-linux-android",
        "android_aarch64": "aarch64-linux-android",
        "linux64": "x86_64-unknown-linux-gnu",
        "mac": "x86_64-apple-darwin",
        "macos": "x86_64-apple-darwin",
        "mac64": "x86_64-apple-darwin",
        "mac32": "i686-apple-darwin",
        "win64": "x86_64-pc-windows-msvc",
        "win32": "i686-pc-windows-msvc",
    }
    return platforms.get(name, name)


def validate_channel(channel):
    """Require a specific release version.

    Packaging from meta-channels, like `stable`, `beta`, or `nightly`
    doesn't give repeatable output. Reject such channels."""
    channel_prefixes = ("stable", "beta", "nightly")
    if any([channel.startswith(c) for c in channel_prefixes]):
        if "-" not in channel:
            raise ValueError(
                'Generic channel "%s" specified!'
                "\nPlease give a specific release version"
                ' like "1.24.0" or "beta-2018-02-20".' % channel
            )


def args():
    """Read command line arguments and return options."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--channel",
        help="Release channel to use:"
        " 1.xx.y, beta-yyyy-mm-dd,"
        " nightly-yyyy-mm-dd,"
        " bors-$rev (grab a build from rust's CI),"
        " or dev (build from source).",
        required=True,
    )
    parser.add_argument(
        "--allow-generic-channel",
        action="store_true",
        help='Allow to use e.g. "nightly" without a date as a channel.',
    )
    parser.add_argument(
        "--patch",
        dest="patches",
        action="append",
        default=[],
        help="apply the given patch file to a dev build."
        " Patch files should be placed in /build/build-rust."
        " Patches can be prefixed with `module-path:` to specify they"
        " apply to that git submodule in the Rust source."
        " e.g. `src/llvm-project:mypatch.diff` patches rust's llvm."
        " Can be given more than once.",
    )
    parser.add_argument(
        "--cargo-channel",
        help="Release channel version to use for cargo."
        " Defaults to the same as --channel.",
    )
    parser.add_argument(
        "--host",
        help="Host platform for the toolchain executable:"
        " e.g. linux64 or aarch64-linux-android."
        " Defaults to linux64.",
    )
    parser.add_argument(
        "--target",
        dest="targets",
        action="append",
        default=[],
        help="Additional target platform to support:"
        " e.g. linux64 or i686-pc-windows-gnu."
        " can be given more than once.",
    )
    args = parser.parse_args()
    if not args.cargo_channel:
        args.cargo_channel = args.channel
    if not args.allow_generic_channel:
        validate_channel(args.channel)
        validate_channel(args.cargo_channel)
    if not args.host:
        args.host = "linux64"
    args.host = expand_platform(args.host)
    args.targets = [expand_platform(t) for t in args.targets]
    delattr(args, "allow_generic_channel")

    return args


if __name__ == "__main__":
    args = vars(args())
    repack(**args)
