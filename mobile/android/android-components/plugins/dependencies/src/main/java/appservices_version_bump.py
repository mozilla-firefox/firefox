#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/

# Helper script for mach vendor / updatebot to update the application-services
# version pin in ApplicationServices.kt to the latest available
# application-services nightly build.
#
# This script was adapted from https://github.com/mozilla-mobile/relbot/

import datetime
import logging
import os
import re
from urllib.parse import quote_plus

import requests
from mozbuild.vendor.host_base import BaseHost

log = logging.getLogger(__name__)
logging.basicConfig(
    format="%(asctime)s - %(name)s.%(funcName)s:%(lineno)s - %(levelname)s - %(message)s",  # noqa E501
    level=logging.INFO,
)


def get_contents(path):
    with open(path) as f:
        return f.read()


def write_contents(path, new_content):
    with open(path, "w", newline="\n") as f:
        f.write(new_content)


def get_app_services_version_path():
    """Return the file path to dependencies file"""
    p = "ApplicationServices.kt"
    if os.path.exists(p):
        return p
    return "mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt"


def taskcluster_indexed_artifact_url(index_name, artifact_path):
    artifact_path = quote_plus(artifact_path)
    return (
        "https://firefox-ci-tc.services.mozilla.com/"
        f"api/index/v1/task/{index_name}/artifacts/{artifact_path}"
    )


def validate_as_version(v):
    """Validate that v is in an expected format for an app-services version. Returns v or raises an exception."""

    match = re.match(r"(^\d+)\.\d+$", v)
    if match:
        # Application-services switched to following the 2-component the
        # Firefox version number in v114
        if int(match.group(1)) >= 114:
            return v
    raise Exception(f"Invalid version format {v}")


def validate_as_channel(c):
    """Validate that c is a valid app-services channel."""
    if c in ("staging", "nightly_staging"):
        # These are channels are valid, but only used for preview builds.  We don't have
        # any way of auto-updating them
        raise Exception(f"Can't update app-services channel {c}")
    if c not in ("release", "nightly"):
        raise Exception(f"Invalid app-services channel {c}")
    return c


def get_current_as_version():
    """Return the current nightly app-services version"""
    regex = re.compile(r'val VERSION = "([\d\.]+)"', re.MULTILINE)

    path = get_app_services_version_path()
    src = get_contents(path)
    match = regex.search(src)
    if match:
        return validate_as_version(match[1])
    raise Exception(
        f"Could not find application-services version in {os.path.basename(path)}"
    )


def match_as_channel(src):
    """
    Find the ApplicationServicesChannel channel in the contents of the given
    ApplicationServices.kt file.
    """
    match = re.compile(
        r"val CHANNEL = ApplicationServicesChannel."
        r"(NIGHTLY|NIGHTLY_STAGING|STAGING|RELEASE)",
        re.MULTILINE,
    ).search(src)
    if match:
        return validate_as_channel(match[1].lower())
    raise Exception("Could not match the channel in ApplicationServices.kt")


def get_current_as_channel():
    """Return the current app-services channel"""
    content = get_contents(get_app_services_version_path())
    return match_as_channel(content)


def get_as_nightly_json(version="latest"):
    r = requests.get(
        taskcluster_indexed_artifact_url(
            f"project.application-services.v2.nightly.{version}",
            "public/build/nightly.json",
        )
    )
    r.raise_for_status()
    return r.json()


def compare_as_versions(a, b):
    # Tricky cmp()-style function for application services versions.  Note that
    # this works with both 2-component versions and 3-component ones, Since
    # python compares tuples element by element.
    a = tuple(int(x) for x in validate_as_version(a).split("."))
    b = tuple(int(x) for x in validate_as_version(b).split("."))
    return (a > b) - (a < b)


def update_as_version(old_as_version, new_as_version):
    """Update the VERSION in ApplicationServices.kt"""
    path = get_app_services_version_path()
    current_version_string = f'val VERSION = "{old_as_version}"'
    new_version_string = f'val VERSION = "{new_as_version}"'
    log.info(f"Updating app-services version in {path}")

    content = get_contents(path)
    new_content = content.replace(current_version_string, new_version_string)
    if content == new_content:
        raise Exception(
            "Update to ApplicationServices.kt resulted in no changes: "
            "maybe the file was already up to date?"
        )

    write_contents(path, new_content)


def update_application_services(revision):
    """Find the app-services nightly build version corresponding to revision;
    if it is newer than the current version in ApplicationServices.kt, then
    update ApplicationServices.kt with the newer version number."""
    as_channel = get_current_as_channel()
    log.info(f"Current app-services channel is {as_channel}")
    if as_channel != "nightly":
        raise NotImplementedError(
            "Only the app-services nightly channel is currently supported"
        )

    current_as_version = get_current_as_version()
    log.info(
        f"Current app-services {as_channel.capitalize()} version is {current_as_version}"
    )

    json = get_as_nightly_json(f"revision.{revision}")
    target_as_version = json["version"]
    log.info(
        f"Target app-services {as_channel.capitalize()} version is {target_as_version}"
    )

    if compare_as_versions(current_as_version, target_as_version) >= 0:
        log.warning(
            f"No newer app-services {as_channel.capitalize()} release found. Exiting."
        )
        return

    dry_run = os.getenv("DRY_RUN") == "True"
    if dry_run:
        log.warning("Dry-run so not continuing.")
        return

    update_as_version(
        current_as_version,
        target_as_version,
    )


class ASHost(BaseHost):
    def upstream_tag(self, revision):
        if revision == "HEAD":
            index = "latest"
        else:
            index = f"revision.{revision}"
        json = get_as_nightly_json(index)
        timestamp = json["version"].rsplit(".", 1)[1]
        return (
            json["commit"],
            datetime.datetime.strptime(timestamp, "%Y%m%d%H%M%S").isoformat(),
        )


def main():
    import sys

    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <commit hash>", file=sys.stderr)
        sys.exit(1)

    update_application_services(sys.argv[1])


if __name__ == "__main__":
    main()
