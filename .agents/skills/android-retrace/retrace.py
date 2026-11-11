#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Retrace an R8-optimised Fenix or Focus stack trace.

The mapping comes from one of:
  --build-id   the Gecko build ID shown on the Socorro crash report; the
               mapping.txt of the matching shipped app bundle is downloaded
               from Taskcluster
  --task-id    a build-apk or build-bundle task that published mapping.txt
  --aab        a local .aab, e.g. downloaded from the Google Play Console
  --mapping    a local mapping.txt

With no stack trace file, only the mapping is fetched and its path printed.
"""

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

ROOT = "https://firefox-ci-tc.services.mozilla.com/api"
CHANNELS = {
    "nightly": "mozilla-central",
    "beta": "mozilla-beta",
    "release": "mozilla-release",
}
AAB_MAPPING = "BUNDLE-METADATA/com.android.tools.build.obfuscation/proguard.map"
CACHE = Path(__file__).resolve().parents[3] / "artifacts" / "retrace"


def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "android-retrace"})
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def find_signing_task(build_id, product, channels):
    date = f"{build_id[:4]}.{build_id[4:6]}.{build_id[6:8]}"
    for channel in channels:
        namespace = (
            f"gecko.v2.{CHANNELS[channel]}.pushdate.{date}.{build_id}"
            f".mobile.{product}-{channel}"
        )
        try:
            return channel, get_json(f"{ROOT}/index/v1/task/{namespace}")["taskId"]
        except urllib.error.HTTPError as e:
            if e.code != 404:
                raise
    return None, None


def find_task_in_group(group_id, name):
    token = None
    while True:
        url = f"{ROOT}/queue/v1/task-group/{group_id}/list"
        if token:
            url += f"?continuationToken={token}"
        page = get_json(url)
        for entry in page["tasks"]:
            if entry["task"]["metadata"]["name"] == name:
                return entry["status"]["taskId"]
        token = page.get("continuationToken")
        if not token:
            return None


def find_bundle_build(build_id, product, channels):
    channel, signing_apk = find_signing_task(build_id, product, channels)
    if not signing_apk:
        sys.exit(
            f"No {product} build with Gecko build ID {build_id} on "
            f"{', '.join(channels)}. Check the build ID and product, or get "
            "the .aab from the Google Play Console and pass --aab."
        )
    group = get_json(f"{ROOT}/queue/v1/task/{signing_apk}")["taskGroupId"]
    signing_bundle = find_task_in_group(group, f"signing-bundle-{product}-{channel}")
    if not signing_bundle:
        sys.exit(f"Found {product}-{channel} but no signing-bundle task in its graph.")
    for dep in get_json(f"{ROOT}/queue/v1/task/{signing_bundle}")["dependencies"]:
        if get_json(f"{ROOT}/queue/v1/task/{dep}")["metadata"]["name"].startswith(
            "build-bundle-"
        ):
            return channel, dep
    sys.exit(f"No build-bundle task found for {signing_bundle}.")


def download_mapping(task_id, dest):
    if dest.exists():
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    url = f"{ROOT}/queue/v1/task/{task_id}/artifacts/public/build/mapping.txt"
    tmp = dest.with_suffix(".part")
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "android-retrace"})
        with urllib.request.urlopen(request) as response, open(tmp, "wb") as out:
            shutil.copyfileobj(response, out)
    except urllib.error.HTTPError as e:
        tmp.unlink(missing_ok=True)
        sys.exit(
            f"Task {task_id} has no usable mapping.txt (HTTP {e.code}). Builds "
            "before bug 2086064 did not publish one, and artifacts expire; get "
            "the .aab from the Google Play Console and pass --aab instead."
        )
    tmp.rename(dest)
    return dest


def extract_from_aab(aab, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(aab) as bundle:
        if AAB_MAPPING not in bundle.namelist():
            sys.exit(f"{aab} has no mapping, so it was not R8-optimised.")
        with bundle.open(AAB_MAPPING) as src, open(dest, "wb") as out:
            shutil.copyfileobj(src, out)
    return dest


def find_retrace():
    if found := shutil.which("retrace"):
        return found
    os_name = {"Darwin": "macosx", "Linux": "linux", "Windows": "windows"}[
        platform.system()
    ]
    sdks = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    sdks.append(str(Path.home() / ".mozbuild" / f"android-sdk-{os_name}"))
    exe = "retrace.bat" if os_name == "windows" else "retrace"
    for sdk in filter(None, sdks):
        tools = Path(sdk) / "cmdline-tools"
        candidates = [
            d / "bin" / exe
            for d in tools.glob("*")
            if re.fullmatch(r"[\d.]+|latest", d.name) and (d / "bin" / exe).exists()
        ]
        if candidates:
            return str(
                max(
                    candidates,
                    key=lambda p: (
                        p.parent.parent.name == "latest",
                        [int(x) for x in re.findall(r"\d+", p.parent.parent.name)],
                    ),
                )
            )
    sys.exit("Could not find retrace. Run `./mach bootstrap` for Android.")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--build-id", help="Gecko build ID from Socorro")
    source.add_argument("--task-id", help="Taskcluster task with mapping.txt")
    source.add_argument("--aab", type=Path, help="local app bundle")
    source.add_argument("--mapping", type=Path, help="local mapping.txt")
    parser.add_argument("--product", choices=["fenix", "focus"], default="fenix")
    parser.add_argument("--channel", choices=list(CHANNELS))
    parser.add_argument("stacktrace", nargs="?", type=Path)
    args = parser.parse_args()

    if args.build_id:
        if not re.fullmatch(r"\d{14}", args.build_id):
            sys.exit("--build-id is the 14-digit Gecko build ID, e.g. 20261111091117.")
        channels = [args.channel] if args.channel else list(CHANNELS)
        channel, task_id = find_bundle_build(args.build_id, args.product, channels)
        print(f"{args.product}-{channel} build-bundle task: {task_id}", file=sys.stderr)
        mapping = download_mapping(
            task_id, CACHE / f"{args.product}-{args.build_id}-mapping.txt"
        )
    elif args.task_id:
        mapping = download_mapping(args.task_id, CACHE / f"{args.task_id}-mapping.txt")
    elif args.aab:
        mapping = extract_from_aab(args.aab, CACHE / f"{args.aab.stem}-mapping.txt")
    else:
        mapping = args.mapping

    print(f"mapping: {mapping}", file=sys.stderr)
    if args.stacktrace:
        sys.exit(subprocess.call([find_retrace(), str(mapping), str(args.stacktrace)]))


if __name__ == "__main__":
    main()
