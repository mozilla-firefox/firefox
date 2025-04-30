# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import errno
import json
import os
import platform
import stat
import subprocess
import sys
import time
from enum import Enum
from pathlib import Path
from typing import Optional, Set, Union

import requests
from mach.util import get_state_dir
from tqdm import tqdm

from mozboot.bootstrap import MOZCONFIG_SUGGESTION_TEMPLATE

# We need the NDK version in multiple different places, and it's inconvenient
# to pass down the NDK version to all relevant places, so we have this global
# variable.
NDK_VERSION = "r28b"
CMDLINE_TOOLS_VERSION_STRING = "19.0"
CMDLINE_TOOLS_VERSION = "13114758"

BUNDLETOOL_VERSION = "1.18.1"
BUNDLETOOL_URL = f"https://github.com/google/bundletool/releases/download/{BUNDLETOOL_VERSION}/bundletool-all-{BUNDLETOOL_VERSION}.jar"

# We expect the emulator AVD definitions to be platform agnostic
X86_64_ANDROID_AVD = "linux64-android-avd-x86_64-repack"
ARM64_ANDROID_AVD = "linux64-android-avd-arm64-repack"

AVD_MANIFEST_X86_64 = Path(__file__).resolve().parent / "android-avds/x86_64.json"
AVD_MANIFEST_ARM64 = Path(__file__).resolve().parent / "android-avds/arm64.json"

MOZBUILD_PATH = Path(get_state_dir())
NDK_PATH = Path(
    os.environ.get("ANDROID_NDK_HOME", MOZBUILD_PATH / f"android-ndk-{NDK_VERSION}")
)
AVD_HOME_PATH = Path(
    os.environ.get("ANDROID_AVD_HOME", MOZBUILD_PATH / "android-device" / "avd")
)

JAVA_VERSION_MAJOR = "17"
JAVA_VERSION_MINOR = "0.15"
JAVA_VERSION_PATCH = "6"

ANDROID_NDK_EXISTS = """
Looks like you have the correct version of the Android NDK installed at:
%s
"""

ANDROID_SDK_EXISTS = """
Looks like you have the Android SDK installed at:
%s
We will install all required Android packages.
"""

ANDROID_SDK_TOO_OLD_UPDATE_IN_PLACE = """
Looks like you have an outdated Android SDK installed at:
%s
I can update outdated Android SDKs to have the required 'sdkmanager' tool. If
this fails, move it out of the way (or remove it entirely) and then run
bootstrap again.
"""

INSTALLING_ANDROID_PACKAGES = """
We are now installing the following Android packages:
%s
You may be prompted to agree to the Android license. You may see some of
output as packages are downloaded and installed.
"""

MOBILE_ANDROID_MOZCONFIG_TEMPLATE = """
# Build GeckoView/Firefox for Android:
ac_add_options --enable-project=mobile/android

# If --target is not specified it will default to host architecture for fast
# emulation (x86_64 or aarch64). For testing on physical phones you most likely
# want to use an aarch64 (ARM64) target.
# ac_add_options --target=aarch64

{extra_lines}
"""

MOBILE_ANDROID_ARTIFACT_MODE_MOZCONFIG_TEMPLATE = """
# Build GeckoView/Firefox for Android Artifact Mode:
ac_add_options --enable-project=mobile/android
ac_add_options --enable-artifact-builds

{extra_lines}
# Write build artifacts to:
mk_add_options MOZ_OBJDIR=./objdir-frontend
"""


class GetNdkVersionError(Exception):
    pass


def install_mobile_android_sdk_or_ndk(url: str, path: Path):
    """
    Fetch an Android SDK or NDK from |url| and unpack it into the given |path|.

    We use, and 'requests' respects, https. We could also include SHAs for a
    small improvement in the integrity guarantee we give. But this script is
    bootstrapped over https anyway, so it's a really minor improvement.

    We keep a cache of the downloaded artifacts, writing into |path|/mozboot.
    We don't yet clean the cache; it's better to waste some disk space and
    not require a long re-download than to wipe the cache prematurely.
    """

    download_path = path / "mozboot"
    try:
        download_path.mkdir(parents=True)
    except OSError as e:
        if e.errno == errno.EEXIST and download_path.is_dir():
            pass
        else:
            raise

    file_name = url.split("/")[-1]
    download_file_path = download_path / file_name
    download(url, download_file_path)

    if file_name.endswith(".tar.gz") or file_name.endswith(".tgz"):
        cmd = ["tar", "zxf", str(download_file_path)]
    elif file_name.endswith(".tar.bz2"):
        cmd = ["tar", "jxf", str(download_file_path)]
    elif file_name.endswith(".zip"):
        cmd = ["unzip", "-q", str(download_file_path)]
    elif file_name.endswith(".bin"):
        # Execute the .bin file, which unpacks the content.
        mode = os.stat(path).st_mode
        download_file_path.chmod(mode | stat.S_IXUSR)
        cmd = [str(download_file_path)]
    else:
        raise NotImplementedError(f"Don't know how to unpack file: {file_name}")

    print(f"Unpacking {download_file_path}...")

    with open(os.devnull, "w") as stdout:
        # These unpack commands produce a ton of output; ignore it.  The
        # .bin files are 7z archives; there's no command line flag to quiet
        # output, so we use this hammer.
        subprocess.check_call(cmd, stdout=stdout, cwd=str(path))

    print(f"Unpacking {download_file_path}... DONE")
    # Now delete the archive
    download_file_path.unlink()


def download(
    url: str,
    download_file_path: Path,
):
    with requests.Session() as session:
        request = session.head(url, allow_redirects=True)
        request.raise_for_status()
        remote_file_size = int(request.headers["content-length"])

        if download_file_path.is_file():
            local_file_size = download_file_path.stat().st_size

            if local_file_size == remote_file_size:
                print(
                    f"{download_file_path.name} already downloaded. Skipping download..."
                )
            else:
                print(f"Partial download detected. Resuming download of {url}...")
                download_internal(
                    download_file_path,
                    session,
                    url,
                    remote_file_size,
                    local_file_size,
                )
        else:
            print(f"Downloading {url}...")
            download_internal(download_file_path, session, url, remote_file_size)


def download_internal(
    download_file_path: Path,
    session,
    url: str,
    remote_file_size,
    resume_from_byte_pos: int = None,
):
    """
    Handles both a fresh SDK/NDK download, as well as resuming a partial one
    """
    # "ab" will behave same as "wb" if file does not exist
    with open(download_file_path, "ab") as file:
        # 64 KB/s should be fine on even the slowest internet connections
        chunk_size = 1024 * 64
        # https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Range#directives
        resume_header = (
            {"Range": f"bytes={resume_from_byte_pos}-"}
            if resume_from_byte_pos
            else None
        )

        request = session.get(
            url, stream=True, allow_redirects=True, headers=resume_header
        )

        with tqdm(
            total=int(remote_file_size),
            unit="B",
            unit_scale=True,
            unit_divisor=1024,
            desc=download_file_path.name,
            initial=resume_from_byte_pos if resume_from_byte_pos else 0,
        ) as progress_bar:
            for chunk in request.iter_content(chunk_size):
                file.write(chunk)
                progress_bar.update(len(chunk))


def get_ndk_version(ndk_path: Union[str, Path]):
    """Given the path to the NDK, return the version as a 3-tuple of (major,
    minor, human).
    """
    ndk_path = Path(ndk_path)
    with open(ndk_path / "source.properties") as f:
        revision = [line for line in f if line.startswith("Pkg.Revision")]
        if not revision:
            raise GetNdkVersionError(
                "Cannot determine NDK version from source.properties"
            )
        if len(revision) != 1:
            raise GetNdkVersionError("Too many Pkg.Revision lines in source.properties")

        (_, version) = revision[0].split("=")
        if not version:
            raise GetNdkVersionError(
                "Unexpected Pkg.Revision line in source.properties"
            )

        (major, minor, revision) = version.strip().split(".")
        if not major or not minor:
            raise GetNdkVersionError("Unexpected NDK version string: " + version)

        # source.properties contains a $MAJOR.$MINOR.$PATCH revision number,
        # but the more common nomenclature that Google uses is alphanumeric
        # version strings like "r20" or "r19c".  Convert the source.properties
        # notation into an alphanumeric string.
        int_minor = int(minor)
        alphas = "abcdefghijklmnop"
        ascii_minor = alphas[int_minor] if int_minor > 0 else ""
        human = f"r{major}{ascii_minor}"
        return (major, minor, human)


def get_sdk_path(os_name: str) -> Path:
    # The user may have an external Android SDK (in which case we
    # save them a lengthy download), or they may have already
    # completed the download. We unpack to
    # ~/.mozbuild/android-sdk-$OS_NAME.
    return Path(
        os.environ.get("ANDROID_SDK_HOME", MOZBUILD_PATH / f"android-sdk-{os_name}"),
    )


def get_sdkmanager_tool_path(sdk_path: Path) -> Path:
    # sys.platform is win32 even if Python/Win64.
    sdkmanager = "sdkmanager.bat" if sys.platform.startswith("win") else "sdkmanager"

    # We expect the |sdkmanager| tool to be at
    # ~/.mozbuild/android-sdk-$OS_NAME/tools/cmdline-tools/$CMDLINE_TOOLS_VERSION_STRING/bin/sdkmanager # NOQA: E501
    return (
        sdk_path / "cmdline-tools" / CMDLINE_TOOLS_VERSION_STRING / "bin" / sdkmanager
    )


def get_avdmanager_tool_path(sdk_path: Path) -> Path:
    # sys.platform is win32 even if Python/Win64.
    sdkmanager = "avdmanager.bat" if sys.platform.startswith("win") else "avdmanager"
    return (
        sdk_path / "cmdline-tools" / CMDLINE_TOOLS_VERSION_STRING / "bin" / sdkmanager
    )


def get_adb_tool_path(sdk_path: Path) -> Path:
    adb = "adb.bat" if sys.platform.startswith("win") else "adb"
    return sdk_path / "platform-tools" / adb


def get_emulator_tool_path(sdk_path: Path) -> Path:
    emulator = "emulator.bat" if sys.platform.startswith("win") else "emulator"
    return sdk_path / "emulator" / emulator


def get_avd_manifest(avd_manifest_path: Path):
    avd_manifest = None
    if avd_manifest_path is not None:
        with open(avd_manifest_path) as f:
            avd_manifest = json.load(f)

    return avd_manifest


def get_os_name_for_android():
    os_name_map = {
        "Darwin": "macosx",
        "Linux": "linux",
        "Windows": "windows",
    }
    os_name = os_name_map.get(platform.system())
    if os_name is None:
        raise NotImplementedError(f"Unsupported platform: {platform.system()}")
    return os_name


def get_os_tag_for_android(os_name: str):
    os_tag_map = {
        "macosx": "mac",
        "windows": "win",
    }
    return os_tag_map.get(os_name, os_name)


def ensure_android(
    os_name: str,
    os_arch: str,
    packages: Optional[Set[str]] = None,
    artifact_mode=False,
    avd_manifest_path: Optional[Path] = None,
    prewarm_avd=False,
    no_interactive=False,
    list_packages=False,
):
    """
    Ensure the Android SDK (and NDK, if `artifact_mode` is false) are
    installed. If not, fetch and unpack the SDK and/or NDK from the
    expected URLs. Ensure the required Android SDK packages are
    installed.

    `os_name` can be 'linux', 'macosx' or 'windows'.
    """

    if os_name == "windows" and os_arch == "ARM64":
        raise NotImplementedError(
            "Building for Android is not supported on ARM64 Windows because "
            "Google does not distribute emulator binary for ARM64 Windows. "
            "See also https://issuetracker.google.com/issues/264614669."
        )
    os_tag = get_os_tag_for_android(os_name)

    # Check for Android NDK only if we are not in artifact mode.
    if not artifact_mode:
        ensure_android_ndk(os_name)

    ensure_android_sdk(os_name, os_tag)
    ensure_bundletool()

    avd_manifest = get_avd_manifest(avd_manifest_path)

    ensure_android_packages(
        os_name,
        os_arch,
        packages=packages,
        avd_manifest=avd_manifest,
        no_interactive=no_interactive,
        list_packages=list_packages,
    )

    ensure_android_avd(
        os_name,
        os_arch,
        no_interactive=no_interactive,
        avd_manifest=avd_manifest,
        prewarm_avd=prewarm_avd,
    )


def ensure_android_ndk(os_name: str):
    """
    Ensure the Android NDK is installed. If it is not found,
    fetch and unpack the NDK from the URL into
    'mozbuild_path/{android-ndk-$VER}'.
    """
    ndk_url = android_ndk_url(os_name)

    # It's not particularly bad to overwrite the NDK toolchain, but it does take
    # a while to unpack, so let's avoid the disk activity if possible.  The SDK
    # may prompt about licensing, so we do this first.
    install_ndk = True
    if NDK_PATH.is_dir():
        try:
            _, _, human = get_ndk_version(NDK_PATH)
            if human == NDK_VERSION:
                print(ANDROID_NDK_EXISTS % NDK_PATH)
                install_ndk = False
        except GetNdkVersionError:
            pass  # Just do the install.
    if install_ndk:
        # The NDK archive unpacks into a top-level android-ndk-$VER directory.
        install_mobile_android_sdk_or_ndk(ndk_url, MOZBUILD_PATH)


def ensure_android_sdk(os_name: str, os_tag: str):
    """
    Ensure the Android SDK is installed. If it is not found,
    fetch and unpack the SDK from the URL into
    'mozbuild_path/{android-sdk-$OS_NAME}'.
    """
    sdk_path = get_sdk_path(os_name)
    sdk_url = f"https://dl.google.com/android/repository/commandlinetools-{os_tag}-{CMDLINE_TOOLS_VERSION}_latest.zip"

    # We don't want to blindly overwrite, since we use the
    # |sdkmanager| tool to install additional parts of the Android
    # toolchain.  If we overwrite, we lose whatever Android packages
    # the user may have already installed.
    if get_sdkmanager_tool_path(sdk_path).is_file():
        print(ANDROID_SDK_EXISTS % sdk_path)
    else:
        if sdk_path.is_dir():
            print(ANDROID_SDK_TOO_OLD_UPDATE_IN_PLACE % sdk_path)
        # The SDK archive used to include a top-level
        # android-sdk-$OS_NAME directory; it no longer does so.  We
        # preserve the old convention to smooth detecting existing SDK
        # installations.
        cmdline_tools_path = MOZBUILD_PATH / f"android-sdk-{os_name}" / "cmdline-tools"
        install_mobile_android_sdk_or_ndk(sdk_url, cmdline_tools_path)
        # The tools package *really* wants to be in
        # <sdk>/cmdline-tools/$CMDLINE_TOOLS_VERSION_STRING
        (cmdline_tools_path / "cmdline-tools").rename(
            cmdline_tools_path / CMDLINE_TOOLS_VERSION_STRING
        )


def ensure_bundletool():
    download(BUNDLETOOL_URL, MOZBUILD_PATH / "bundletool.jar")


def ensure_android_avd(
    os_name: str,
    os_arch: str,
    no_interactive=False,
    avd_manifest=None,
    prewarm_avd=False,
):
    """
    Use the given sdkmanager tool (like 'sdkmanager') to install required
    Android packages.
    """
    if avd_manifest is None:
        return

    # avdmanager needs Java
    ensure_java(os_name, os_arch)

    sdk_path = get_sdk_path(os_name)
    avdmanager_tool = get_avdmanager_tool_path(sdk_path)
    adb_tool = get_adb_tool_path(sdk_path)
    emulator_tool = get_emulator_tool_path(sdk_path)

    AVD_HOME_PATH.mkdir(parents=True, exist_ok=True)
    # The AVD needs this folder to boot, so make sure it exists here.
    (sdk_path / "platforms").mkdir(parents=True, exist_ok=True)

    java_bin_path = get_java_bin_path(os_name, MOZBUILD_PATH)
    env = os.environ.copy()
    env["JAVA_HOME"] = str(java_bin_path.parent)
    env["ANDROID_AVD_HOME"] = str(AVD_HOME_PATH)

    avd_name = avd_manifest["emulator_avd_name"]
    args = [
        str(avdmanager_tool),
        "--verbose",
        "create",
        "avd",
        "--force",
        "--name",
        avd_name,
        "--package",
        avd_manifest["emulator_package"],
    ]

    if not no_interactive:
        subprocess.check_call(args, env=env)
        return

    # Flush outputs before running sdkmanager.
    sys.stdout.flush()
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, env=env)
    proc.communicate(b"no\n")

    retcode = proc.poll()
    if retcode:
        cmd = args[0]
        e = subprocess.CalledProcessError(retcode, cmd)
        raise e

    avd_path = AVD_HOME_PATH / (str(avd_name) + ".avd")
    config_file_name = avd_path / "config.ini"

    print(f"Writing config at {config_file_name}")

    if config_file_name.is_file():
        with open(config_file_name, "a") as config:
            for key, value in avd_manifest["emulator_extra_config"].items():
                config.write(f"{key}={value}\n")
    else:
        raise NotImplementedError(
            f"Could not find config file at {config_file_name}, something went wrong"
        )

    # Some AVDs cannot be prewarmed in CI because they cannot run on linux64
    # (like the arm64 AVD).
    if avd_manifest and "emulator_prewarm" in avd_manifest:
        prewarm_avd = prewarm_avd and avd_manifest["emulator_prewarm"]

    if prewarm_avd:
        run_prewarm_avd(adb_tool, emulator_tool, env, avd_name, avd_manifest)
    # When running in headless mode, the emulator does not run the cleanup
    # step, and thus doesn't delete lock files. On some platforms, left-over
    # lock files can cause the emulator to not start, so we remove them here.
    for lock_file in ["hardware-qemu.ini.lock", "multiinstance.lock"]:
        lock_file_path = avd_path / lock_file
        try:
            lock_file_path.unlink()
            print(f"Removed lock file {lock_file_path}")
        except OSError:
            # The lock file is not there, nothing to do.
            pass


def run_prewarm_avd(
    adb_tool: Path,
    emulator_tool: Path,
    env,
    avd_name,
    avd_manifest,
):
    """
    Ensures the emulator is fully booted to save time on future iterations.
    """
    args = [str(emulator_tool), "-avd", avd_name] + avd_manifest["emulator_extra_args"]

    # Flush outputs before running emulator.
    sys.stdout.flush()
    proc = subprocess.Popen(args, env=env)

    booted = False
    for i in range(100):
        boot_completed_cmd = [str(adb_tool), "shell", "getprop", "sys.boot_completed"]
        try:
            result = subprocess.run(
                boot_completed_cmd,
                env=env,
                capture_output=True,
                timeout=30,
                text=True,
                check=False,
            )
            boot_completed = result.stdout.strip()
            print(f"sys.boot_completed = {boot_completed}")
            time.sleep(30)
            if boot_completed == "1":
                booted = True
                break
        except subprocess.TimeoutExpired:
            # Sometimes the adb command hangs, that's ok
            print("sys.boot_completed = Timeout")

    if not booted:
        raise NotImplementedError("Could not prewarm emulator")

    # Wait until the emulator completely shuts down
    subprocess.Popen([str(adb_tool), "emu", "kill"], env=env).wait()
    proc.wait()


class AndroidPackageList(Enum):
    ALL = "android-packages.txt"
    SYSTEM = "android-system-images-packages.txt"
    EMULATOR = "android-emulator-packages.txt"


def get_android_packages(
    package_list_type: AndroidPackageList = AndroidPackageList.ALL,
) -> Set[str]:
    packages_file_path = (Path(__file__).parent / package_list_type.value).resolve()

    content = packages_file_path.read_text()
    packages = {line.strip() for line in content.splitlines()}

    return packages


def ensure_android_packages(
    os_name: str,
    os_arch: str,
    packages: Optional[Set[str]],
    avd_manifest=None,
    no_interactive=False,
    list_packages=False,
):
    """
    Use the given sdkmanager tool (like 'sdkmanager') to install required
    Android packages.
    """
    if not packages:
        packages = get_android_packages(AndroidPackageList.ALL)

    sdkmanager_tool = get_sdkmanager_tool_path(sdk_path=get_sdk_path(os_name))

    if avd_manifest is not None:
        packages.add(avd_manifest["emulator_package"])

    print(INSTALLING_ANDROID_PACKAGES % "\n".join(packages))

    # This tries to install all the required Android packages.  The user
    # may be prompted to agree to the Android license.
    args = [str(sdkmanager_tool)]
    if os_name == "macosx" and os_arch == "arm64":
        # Support for Apple Silicon is still in nightly
        args.append("--channel=3")
    args.extend(packages)

    # sdkmanager needs JAVA_HOME
    ensure_java(os_name, os_arch)
    java_bin_path = get_java_bin_path(os_name, MOZBUILD_PATH)
    env = os.environ.copy()
    env["JAVA_HOME"] = str(java_bin_path.parent)

    if not no_interactive:
        subprocess.check_call(args, env=env)
        return

    # Flush outputs before running sdkmanager.
    sys.stdout.flush()
    sys.stderr.flush()
    # Emulate yes.  For a discussion of passing input to check_output,
    # see https://stackoverflow.com/q/10103551.
    yes = "\n".join(["y"] * 100).encode("UTF-8")
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, env=env)
    proc.communicate(yes)

    retcode = proc.poll()
    if retcode:
        cmd = args[0]
        e = subprocess.CalledProcessError(retcode, cmd)
        raise e
    if list_packages:
        subprocess.check_call([str(sdkmanager_tool), "--list"])


def generate_mozconfig(os_name: str, artifact_mode=False):
    extra_lines = []
    if extra_lines:
        extra_lines.append("")

    if artifact_mode:
        template = MOBILE_ANDROID_ARTIFACT_MODE_MOZCONFIG_TEMPLATE
    else:
        template = MOBILE_ANDROID_MOZCONFIG_TEMPLATE

    kwargs = dict(
        sdk_path=get_sdk_path(os_name),
        ndk_path=NDK_PATH,
        avd_home_path=AVD_HOME_PATH,
        moz_state_dir=MOZBUILD_PATH,
        extra_lines="\n".join(extra_lines),
    )
    return template.format(**kwargs).strip()


def android_ndk_url(os_name: str, ver=NDK_VERSION):
    # Produce a URL like
    # 'https://dl.google.com/android/repository/android-ndk-$VER-linux.zip
    base_url = "https://dl.google.com/android/repository/android-ndk"

    if os_name == "macosx":
        # |mach bootstrap| uses 'macosx', but Google uses 'darwin'.
        os_name = "darwin"

    return f"{base_url}-{ver}-{os_name}.zip"


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Install Android SDK/NDK and other components based on the given flags."
    )
    parser.add_argument(
        "-a",
        "--artifact-mode",
        dest="artifact_mode",
        action="store_true",
        help="If true, install only the Android SDK (and not the Android NDK).",
    )
    parser.add_argument(
        "--system-images-only",
        dest="system_images_only",
        action="store_true",
        help="If true, install only the system images for the AVDs.",
    )
    parser.add_argument(
        "--no-interactive",
        dest="no_interactive",
        action="store_true",
        help="Accept the Android SDK licenses without user interaction.",
    )
    parser.add_argument(
        "--avd-manifest",
        dest="avd_manifest_path",
        help="If present, generate AVD from the manifest pointed by this argument.",
    )
    parser.add_argument(
        "--prewarm-avd",
        dest="prewarm_avd",
        action="store_true",
        help="If true, boot the AVD and wait until completed to speed up subsequent boots.",
    )
    parser.add_argument(
        "--list-packages",
        dest="list_packages",
        action="store_true",
        help="If true, list installed packages.",
    )

    exclusive_group = parser.add_mutually_exclusive_group()

    exclusive_group.add_argument(
        "--emulator-only",
        dest="emulator_only",
        action="store_true",
        help="If true, install only the Android emulator (and not the SDK or NDK).",
    )
    exclusive_group.add_argument(
        "--jdk-only",
        dest="jdk_only",
        action="store_true",
        help="If true, install only the Java JDK.",
    )
    exclusive_group.add_argument(
        "--ndk-only",
        dest="ndk_only",
        action="store_true",
        help="If true, install only the Android NDK (and not the Android SDK).",
    )

    options = parser.parse_args()

    if options.artifact_mode and options.ndk_only:
        raise NotImplementedError("Use no options to install the NDK and the SDK.")

    if options.artifact_mode and options.emulator_only:
        raise NotImplementedError("Use no options to install the SDK and emulators.")

    os_name = get_os_name_for_android()
    os_tag = get_os_tag_for_android(os_name)
    os_arch = platform.machine()

    avd_manifest_path = (
        Path(options.avd_manifest_path) if options.avd_manifest_path else None
    )

    if options.emulator_only:
        # We need the sdk to install the emulator
        ensure_android_sdk(os_name, os_tag)
        ensure_bundletool()

        packages = get_android_packages(AndroidPackageList.EMULATOR)

        ensure_android_packages(
            os_name,
            os_arch,
            packages=packages,
            avd_manifest=get_avd_manifest(avd_manifest_path),
            no_interactive=options.no_interactive,
            list_packages=options.list_packages,
        )
        return 0

    if options.jdk_only:
        ensure_java(os_name, os_arch)
        return 0

    if options.ndk_only:
        ensure_android_ndk(os_name)
        return 0

    packages = None

    if options.system_images_only:
        packages = get_android_packages(AndroidPackageList.SYSTEM)

    ensure_android(
        os_name,
        os_arch,
        packages=packages,
        artifact_mode=options.artifact_mode,
        avd_manifest_path=avd_manifest_path,
        prewarm_avd=options.prewarm_avd,
        no_interactive=options.no_interactive,
        list_packages=options.list_packages,
    )
    mozconfig = generate_mozconfig(os_name, options.artifact_mode)

    # |./mach bootstrap| automatically creates a mozconfig file for you if it doesn't
    # exist. However, here, we don't know where the "topsrcdir" is, and it's not worth
    # pulling in CommandContext (and its dependencies) to find out.
    # So, instead, we'll politely ask users to create (or update) the file themselves.
    suggestion = MOZCONFIG_SUGGESTION_TEMPLATE % ("$topsrcdir/mozconfig", mozconfig)
    print("\n" + suggestion)

    return 0


def ensure_java(os_name: str, os_arch: str):
    if os_name == "macosx":
        os_tag = "mac"
    else:
        os_tag = os_name

    if os_arch == "x86_64":
        arch = "x64"
    elif os_arch.lower() == "arm64":
        arch = "aarch64"
    else:
        arch = os_arch

    java_path = get_java_bin_path(os_name, MOZBUILD_PATH)
    if not java_path:
        raise NotImplementedError(f"Could not bootstrap java for {os_name}.")

    if not java_path.exists():
        ext = "zip" if os_name == "windows" else "tar.gz"

        # e.g. https://github.com/adoptium/temurin17-binaries/releases/
        #      download/jdk-17.0.15%2B6/OpenJDK17U-jdk_x64_linux_hotspot_17.0.15_6.tar.gz
        java_url = (
            f"https://github.com/adoptium/temurin{JAVA_VERSION_MAJOR}-binaries/releases/"
            f"download/jdk-{JAVA_VERSION_MAJOR}.{JAVA_VERSION_MINOR}%2B{JAVA_VERSION_PATCH}/"
            f"OpenJDK{JAVA_VERSION_MAJOR}U-jdk_{arch}_{os_tag}_hotspot_{JAVA_VERSION_MAJOR}.{JAVA_VERSION_MINOR}_{JAVA_VERSION_PATCH}.{ext}"
        )
        install_mobile_android_sdk_or_ndk(java_url, MOZBUILD_PATH / "jdk")


def get_java_bin_path(os_name: str, toolchain_path: Path):
    # Like jdk-17.0.15+6
    jdk_folder = f"jdk-{JAVA_VERSION_MAJOR}.{JAVA_VERSION_MINOR}+{JAVA_VERSION_PATCH}"

    java_path = toolchain_path / "jdk" / jdk_folder

    if os_name == "macosx":
        return java_path / "Contents" / "Home" / "bin"
    elif os_name == "linux":
        return java_path / "bin"
    elif os_name == "windows":
        return java_path / "bin"
    else:
        return None


def locate_java_bin_path(host_kernel: str, toolchain_path: Union[str, Path]):
    if host_kernel == "WINNT":
        os_name = "windows"
    elif host_kernel == "Darwin":
        os_name = "macosx"
    elif host_kernel == "Linux":
        os_name = "linux"
    else:
        # Default to Linux
        os_name = "linux"
    path = get_java_bin_path(os_name, Path(toolchain_path))
    if not path.is_dir():
        raise JavaLocationFailedException(
            f"Could not locate Java at {path}, please run "
            "./mach bootstrap --no-system-changes"
        )
    return str(path)


class JavaLocationFailedException(Exception):
    pass


if __name__ == "__main__":
    sys.exit(main())
