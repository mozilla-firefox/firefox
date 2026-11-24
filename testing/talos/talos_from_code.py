#! /usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# Script name:   talos_from_code.py
# Purpose:       Read from a talos.json file the different files to download for a talos job
# Author(s):     Zambrano Gasparnian, Armen <armenzg@mozilla.com>
# Target:        Python 2.5
#
import json
import os
import re
import sys
import urllib.parse
from optparse import OptionParser


def main():
    """
    This script downloads a talos.json file which indicates which files to download
    for a talos job.
    See a talos.json file for a better understand:
    https://hg.mozilla.org/mozilla-central/raw-file/default/testing/talos/talos.json
    """
    parser = OptionParser()
    parser.add_option(
        "--talos-json-url",
        dest="talos_json_url",
        type="string",
        help="It indicates from where to download the talos.json file.",
    )
    (options, args) = parser.parse_args()

    # 1) check that the url was passed
    if options.talos_json_url is None:
        print("You need to specify --talos-json-url.")
        sys.exit(1)

    # 2) try to download the talos.json file
    try:
        jsonFilename = download_file(options.talos_json_url)
    except Exception as e:
        print("ERROR: We tried to download the talos.json file but something failed.")
        print(f"ERROR: {str(e)}")
        sys.exit(1)

    # 3) download the necessary files
    print(f"INFO: talos.json URL: {options.talos_json_url}")
    try:
        key = "talos.zip"
        entity = get_value(jsonFilename, key)
        if passesRestrictions(options.talos_json_url, entity["url"]):
            # the key is at the same time the filename e.g. talos.zip
            print(
                f"INFO: Downloading {entity['url']} as {os.path.join(entity['path'], key)}"
            )
            download_file(entity["url"], entity["path"], key)
        else:
            print(
                "ERROR: You have tried to download a file "
                + f"from: {entity['url']} "
                + "which is a location different than http://talos-bundles.pvt.build.mozilla.org/"
            )
            print("ERROR: This is only allowed for the certain branches.")
            sys.exit(1)
    except Exception as e:
        print(f"ERROR: {str(e)}")
        sys.exit(1)


def passesRestrictions(talosJsonUrl, fileUrl):
    """
    Only certain branches are exempted from having to host their downloadable files
    in talos-bundles.pvt.build.mozilla.org
    """
    if (
        talosJsonUrl.startswith("http://hg.mozilla.org/try/")
        or talosJsonUrl.startswith("https://hg.mozilla.org/try/")
        or talosJsonUrl.startswith("http://hg.mozilla.org/projects/pine/")
        or talosJsonUrl.startswith("https://hg.mozilla.org/projects/pine/")
        or talosJsonUrl.startswith("http://hg.mozilla.org/projects/ash/")
        or talosJsonUrl.startswith("https://hg.mozilla.org/projects/ash/")
    ):
        return True
    else:
        p = re.compile("^http://talos-bundles.pvt.build.mozilla.org/")
        m = p.match(fileUrl)
        if m is None:
            return False
        return True


def get_filename_from_url(url):
    """
    This returns the filename of the file we're trying to download
    """
    parsed = urllib.parse.urlsplit(url.rstrip("/"))
    if parsed.path != "":
        return parsed.path.rsplit("/", 1)[-1]
    else:
        print(
            f"ERROR: We were trying to download a file from {url} "
            "but the URL seems to be incorrect."
        )
        sys.exit(1)


def download_file(url, path="", saveAs=None):
    """
    It downloads a file from URL to the indicated path
    """
    req = urllib.request.Request(url)
    f = urllib.request.urlopen(req)
    if path != "" and not os.path.isdir(path):
        try:
            os.makedirs(path)
            print(f"INFO: directory {path} created")
        except Exception as e:
            print(f"ERROR: {str(e)}")
            sys.exit(1)
    filename = saveAs if saveAs else get_filename_from_url(url)
    local_file = open(os.path.join(path, filename), "wb")
    local_file.write(f.read())
    local_file.close()
    return filename


def get_value(json_filename, key):
    """
    It loads up a JSON file and returns the value for the given string
    """
    f = open(json_filename)
    return json.load(f)[key]


if __name__ == "__main__":
    main()
