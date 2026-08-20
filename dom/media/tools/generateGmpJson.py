# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import hashlib
import json
import logging
import re
from urllib.parse import urlparse, urlunparse

import requests


def fetch_url_for_cdms(cdms, urlParams, allow_version_mismatch):
    any_version = None
    for cdm in cdms:
        if "fileName" in cdm:
            cdm["fileUrl"] = cdm["fileName"].format_map(urlParams)
            response = requests.get(cdm["fileUrl"], allow_redirects=False)
            if response.status_code != 302:
                raise Exception(
                    "{} unexpected status code {}".format(
                        cdm["target"], response.status_code
                    )
                )

            redirectUrl = response.headers["Location"]
            parsedUrl = urlparse(redirectUrl)
            if parsedUrl.scheme != "https":
                raise Exception(
                    "{} expected https scheme '{}'".format(cdm["target"], redirectUrl)
                )

            sanitizedUrl = urlunparse((
                parsedUrl.scheme,
                parsedUrl.netloc,
                parsedUrl.path,
                None,
                None,
                None,
            ))

            # Note that here we modify the returned URL from the
            # component update service because it returns a preferred
            # server for the caller of the script. This may not match
            # up with what the end users require. Google has requested
            # that we instead replace these results with the
            # edgedl.me.gvt1.com domain/path, which should be location
            # agnostic.
            normalizedUrl = re.sub(
                r"https.+?release2",
                "https://edgedl.me.gvt1.com/edgedl/release2",
                sanitizedUrl,
            )
            if not normalizedUrl:
                raise Exception(
                    "{} cannot normalize '{}'".format(cdm["target"], sanitizedUrl)
                )

            # Because some users are unable to resolve *.gvt1.com
            # URLs, we supply an alternative based on www.google.com.
            # This should resolve with success more frequently.
            mirrorUrl = re.sub(
                r"https.+?release2",
                "https://www.google.com/dl/release2",
                sanitizedUrl,
            )

            version = re.search(r".*?_([\d]+\.[\d]+\.[\d]+\.[\d]+)/", sanitizedUrl)
            if version is None:
                raise Exception(
                    "{} cannot extract version '{}'".format(cdm["target"], sanitizedUrl)
                )
            if any_version is None:
                any_version = version.group(1)
            elif version.group(1) != any_version and not allow_version_mismatch:
                raise Exception(
                    "{} version {} mismatch {}".format(
                        cdm["target"], version.group(1), any_version
                    )
                )
            cdm["fileName"] = normalizedUrl
            if mirrorUrl and mirrorUrl != normalizedUrl:
                cdm["fileNameMirror"] = mirrorUrl
    return any_version


def fetch_data_for_cdms(cdms, urlParams):
    for cdm in cdms:
        if "fileName" in cdm:
            cdm["fileUrl"] = cdm["fileName"].format_map(urlParams)
            response = requests.get(cdm["fileUrl"])
            response.raise_for_status()
            cdm["hashValue"] = hashlib.sha512(response.content).hexdigest()
            if "fileNameMirror" in cdm:
                cdm["mirrorUrl"] = cdm["fileNameMirror"].format_map(urlParams)
                mirrorresponse = requests.get(cdm["mirrorUrl"])
                mirrorresponse.raise_for_status()
                mirrorhash = hashlib.sha512(mirrorresponse.content).hexdigest()
                if cdm["hashValue"] != mirrorhash:
                    raise Exception(
                        "Primary hash {} and mirror hash {} differ",
                        cdm["hashValue"],
                        mirrorhash,
                    )
            cdm["filesize"] = len(response.content)
            if cdm["filesize"] == 0:
                raise Exception("Empty response for {target}".format_map(cdm))


def generate_json_for_cdms(cdms):
    cdm_json = ""
    for cdm in cdms:
        if "alias" in cdm:
            cdm_json += (
                '        "{target}": {{\n'
                + '          "alias": "{alias}"\n'
                + "        }},\n"
            ).format_map(cdm)
        elif "mirrorUrl" in cdm:
            cdm_json += (
                '        "{target}": {{\n'
                + '          "fileUrl": "{fileUrl}",\n'
                + '          "mirrorUrls": [\n'
                + '            "{mirrorUrl}"\n'
                + "          ],\n"
                + '          "filesize": {filesize},\n'
                + '          "hashValue": "{hashValue}"\n'
                + "        }},\n"
            ).format_map(cdm)
        else:
            cdm_json += (
                '        "{target}": {{\n'
                + '          "fileUrl": "{fileUrl}",\n'
                + '          "mirrorUrls": [],\n'
                + '          "filesize": {filesize},\n'
                + '          "hashValue": "{hashValue}"\n'
                + "        }},\n"
            ).format_map(cdm)
    return cdm_json[:-2] + "\n"


def remove_skipped_targets(cdms, skip_targets):
    if not skip_targets:
        return cdms
    revised_cdms = []
    for cdm in cdms:
        if cdm["target"] in skip_targets:
            continue
        if "alias" in cdm and cdm["alias"] in skip_targets:
            continue
        revised_cdms.append(cdm)
    return revised_cdms


def calculate_gmpopenh264_json(
    version: str, version_hash: str, url_base: str, skip_targets
) -> str:
    # fmt: off
    cdms = [
        {"target": "Darwin_aarch64-gcc3", "fileName": "{url_base}/openh264-macosx64-aarch64-{version}.zip"},
        {"target": "Darwin_x86_64-gcc3", "fileName": "{url_base}/openh264-macosx64-{version}.zip"},
        {"target": "Linux_aarch64-gcc3", "fileName": "{url_base}/openh264-linux64-aarch64-{version}.zip"},
        {"target": "Linux_x86-gcc3", "fileName": "{url_base}/openh264-linux32-{version}.zip"},
        {"target": "Linux_x86_64-gcc3", "fileName": "{url_base}/openh264-linux64-{version}.zip"},
        {"target": "Linux_x86_64-gcc3-asan", "alias": "Linux_x86_64-gcc3"},
        {"target": "WINNT_aarch64-msvc-aarch64", "fileName": "{url_base}/openh264-win64-aarch64-{version}.zip"},
        {"target": "WINNT_x86-msvc", "fileName": "{url_base}/openh264-win32-{version}.zip"},
        {"target": "WINNT_x86-msvc-x64", "alias": "WINNT_x86-msvc"},
        {"target": "WINNT_x86-msvc-x86", "alias": "WINNT_x86-msvc"},
        {"target": "WINNT_x86_64-msvc", "fileName": "{url_base}/openh264-win64-{version}.zip"},
        {"target": "WINNT_x86_64-msvc-x64", "alias": "WINNT_x86_64-msvc"},
        {"target": "WINNT_x86_64-msvc-x64-asan", "alias": "WINNT_x86_64-msvc"},
    ]
    # fmt: on
    cdms = remove_skipped_targets(cdms, skip_targets)
    try:
        fetch_data_for_cdms(cdms, {"url_base": url_base, "version": version_hash})
    except Exception as e:
        logging.error("calculate_gmpopenh264_json: could not create JSON due to: %s", e)
        return ""
    else:
        return (
            "{\n"
            + '  "hashFunction": "sha512",\n'
            + f'  "name": "OpenH264-{version}",\n'
            + '  "schema_version": 1000,\n'
            + '  "vendors": {\n'
            + '    "gmp-gmpopenh264": {\n'
            + '      "platforms": {\n'
            + generate_json_for_cdms(cdms)
            + "      },\n"
            + f'      "version": "{version}"\n'
            + "    }\n"
            + "  }\n"
            + "}"
        )


def calculate_chrome_component_json(
    name: str, altname: str, url_base: str, allow_version_mismatch: bool, cdms
) -> str:
    try:
        version = fetch_url_for_cdms(
            cdms, {"url_base": url_base}, allow_version_mismatch
        )
        fetch_data_for_cdms(cdms, {})
    except Exception as e:
        logging.error(
            "calculate_chrome_component_json: could not create JSON due to: %s", e
        )
        return ""
    else:
        return (
            "{\n"
            + '  "hashFunction": "sha512",\n'
            + f'  "name": "{name}-{version}",\n'
            + '  "schema_version": 1000,\n'
            + '  "vendors": {\n'
            + f'    "gmp-{altname}": {{\n'
            + '      "platforms": {\n'
            + generate_json_for_cdms(cdms)
            + "      },\n"
            + f'      "version": "{version}"\n'
            + "    }\n"
            + "  }\n"
            + "}"
        )


def calculate_widevinecdm_component_json(
    url_base: str, allow_version_mismatch: bool, skip_targets
) -> str:
    # fmt: off
    cdms = [
        {"target": "Darwin_aarch64-gcc3", "fileName": "{url_base}&os=mac&arch=arm64&os_arch=arm64"},
        {"target": "Darwin_x86_64-gcc3", "alias": "Darwin_x86_64-gcc3-u-i386-x86_64"},
        {"target": "Darwin_x86_64-gcc3-u-i386-x86_64", "fileName": "{url_base}&os=mac&arch=x64&os_arch=x64"},
        {"target": "Linux_aarch64-gcc3", "fileName": "{url_base}&os=Linux&arch=arm64&os_arch=arm64"},
        {"target": "Linux_x86_64-gcc3", "fileName": "{url_base}&os=Linux&arch=x64&os_arch=x64"},
        {"target": "Linux_x86_64-gcc3-asan", "alias": "Linux_x86_64-gcc3"},
        {"target": "WINNT_aarch64-msvc-aarch64", "fileName": "{url_base}&os=win&arch=arm64&os_arch=arm64"},
        {"target": "WINNT_x86-msvc", "fileName": "{url_base}&os=win&arch=x86&os_arch=x86"},
        {"target": "WINNT_x86-msvc-x64", "alias": "WINNT_x86-msvc"},
        {"target": "WINNT_x86-msvc-x86", "alias": "WINNT_x86-msvc"},
        {"target": "WINNT_x86_64-msvc", "fileName": "{url_base}&os=win&arch=x64&os_arch=x64"},
        {"target": "WINNT_x86_64-msvc-x64", "alias": "WINNT_x86_64-msvc"},
        {"target": "WINNT_x86_64-msvc-x64-asan", "alias": "WINNT_x86_64-msvc"},
    ]
    # fmt: on
    cdms = remove_skipped_targets(cdms, skip_targets)
    return calculate_chrome_component_json(
        "Widevine",
        "widevinecdm",
        url_base.format_map({"guid": "oimompecagnajdejgnnjijobebaeigek"}),
        allow_version_mismatch,
        cdms,
    )


def calculate_widevinecdm_l1_component_json(
    url_base: str, allow_version_mismatch: bool, skip_targets
) -> str:
    # fmt: off
    cdms = [
        {"target": "WINNT_x86_64-msvc", "fileName": "{url_base}&os=win&arch=x64&os_arch=x64"},
        {"target": "WINNT_x86_64-msvc-x64", "alias": "WINNT_x86_64-msvc"},
        {"target": "WINNT_x86_64-msvc-x64-asan", "alias": "WINNT_x86_64-msvc"},
    ]
    # fmt: on
    cdms = remove_skipped_targets(cdms, skip_targets)
    return calculate_chrome_component_json(
        "Widevine-L1",
        "widevinecdm-l1",
        url_base.format_map({"guid": "neifaoindggfcjicffkgpmnlppeffabd"}),
        allow_version_mismatch,
        cdms,
    )


def main():
    examples = """examples:
  python dom/media/tools/generateGmpJson.py --url http://localhost:8080 openh264 2.3.1 0a48f4d2e9be2abb4fb01b4c3be83cf44ce91a6e
  python dom/media/tools/generateGmpJson.py widevine --skip-targets Linux_aarch64-gcc3
  python dom/media/tools/generateGmpJson.py widevine_l1"""

    parser = argparse.ArgumentParser(
        description="Generate JSON for GMP plugin updates",
        epilog=examples,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "plugin",
        help="which plugin: openh264, widevine, widevine_l1",
    )
    parser.add_argument("version", help="version of plugin", nargs="?")
    parser.add_argument("revision", help="revision hash of plugin", nargs="?")
    parser.add_argument("--url", help="override base URL from which to fetch plugins")
    parser.add_argument(
        "--testrequest",
        action="store_true",
        help="request upcoming version for component update service",
    )
    parser.add_argument(
        "--allow-version-mismatch",
        action="store_true",
        help="allow component update service to return different versions for targets",
    )
    parser.add_argument(
        "--skip-targets",
        action="extend",
        nargs="+",
        help="skip including given targets in resulting json",
    )
    args = parser.parse_args()

    if args.plugin == "openh264":
        url_base = "https://ciscobinary.openh264.org"
        if args.version is None or args.revision is None:
            parser.error("openh264 requires version and revision")
    elif args.plugin in ("widevine", "widevine_l1"):
        url_base = "https://update.googleapis.com/service/update2/crx?response=redirect&x=id%3D{guid}%26uc&acceptformat=crx3&updaterversion=999"
        if args.testrequest:
            url_base += "&testrequest=1"
        if args.version is not None or args.revision is not None:
            parser.error("chrome component cannot use version or revision")
    else:
        parser.error("plugin not recognized")

    if args.url is not None:
        url_base = args.url

    if url_base[-1] == "/":
        url_base = url_base[:-1]

    if args.plugin == "openh264":
        json_result = calculate_gmpopenh264_json(
            args.version, args.revision, url_base, args.skip_targets
        )
    elif args.plugin == "widevine":
        json_result = calculate_widevinecdm_component_json(
            url_base, args.allow_version_mismatch, args.skip_targets
        )
    elif args.plugin == "widevine_l1":
        json_result = calculate_widevinecdm_l1_component_json(
            url_base, args.allow_version_mismatch, args.skip_targets
        )

    try:
        json.loads(json_result)
    except json.JSONDecodeError as e:
        logging.error("invalid JSON produced: %s", e)
    else:
        print(json_result)


main()
