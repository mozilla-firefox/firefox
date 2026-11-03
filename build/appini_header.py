# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Parses a given application.ini file and outputs the corresponding
StaticXREAppData structure as a C++ header file"""

import configparser
import sys


def main(output, file):
    config = configparser.RawConfigParser()
    config.read(file)
    flags = set()
    try:
        if config.getint("XRE", "EnableProfileMigrator") == 1:
            flags.add("NS_XRE_ENABLE_PROFILE_MIGRATOR")
    except Exception:
        pass
    try:
        if config.getint("Crash Reporter", "Enabled") == 1:
            flags.add("NS_XRE_ENABLE_CRASH_REPORTER")
    except Exception:
        pass
    appdata = dict(
        ("%s:%s" % (s, o), config.get(s, o))
        for s in config.sections()
        for o in config.options(s)
    )
    appdata["flags"] = " | ".join(sorted(flags)) if flags else "0"
    for key in ("App:vendor", "App:profile"):
        # Set to nullptr when not present or falsy such as an empty string
        appdata[key] = '"%s"' % appdata[key] if appdata.get(key, None) else "nullptr"
    expected = (
        "App:vendor",
        "App:name",
        "App:remotingname",
        "App:version",
        "App:buildid",
        "App:id",
        "Gecko:minversion",
        "Gecko:maxversion",
    )
    missing = [var for var in expected if var not in appdata]
    if missing:
        print("Missing values in %s: %s" % (file, ", ".join(missing)), file=sys.stderr)
        sys.exit(1)

    if "Crash Reporter:serverurl" not in appdata:
        appdata["Crash Reporter:serverurl"] = ""

    if "App:sourcerepository" in appdata and "App:sourcestamp" in appdata:
        repo = appdata["App:sourcerepository"]
        rev_path = "/rev/" if "hg.mozilla.org" in repo else "/commit/"
        appdata["App:sourceurl"] = f'"{repo}{rev_path}{appdata["App:sourcestamp"]}"'
    else:
        appdata["App:sourceurl"] = "nullptr"

    if "AppUpdate:url" not in appdata:
        appdata["AppUpdate:url"] = ""

    if sourcestamp := appdata.get("App:sourcestamp"):
        appdata["App:sourcerevision"] = f'"{sourcestamp}"'
    else:
        appdata["App:sourcerevision"] = "nullptr"

    output.write(
        """#include "mozilla/StaticXREAppData.h"
             static const mozilla::StaticXREAppData sAppData = {
                 %(App:vendor)s,
                 "%(App:name)s",
                 "%(App:remotingname)s",
                 "%(App:version)s",
                 "%(App:buildid)s",
                 "%(App:id)s",
                 nullptr, // copyright
                 %(flags)s,
                 "%(Gecko:minversion)s",
                 "%(Gecko:maxversion)s",
                 "%(Crash Reporter:serverurl)s",
                 %(App:profile)s,
                 nullptr, // UAName
                 %(App:sourceurl)s,
                 %(App:sourcerevision)s,
                 "%(AppUpdate:url)s"
             };"""
        % appdata
    )


if __name__ == "__main__":
    if len(sys.argv) != 1:
        main(sys.stdout, sys.argv[1])
    else:
        print("Usage: %s /path/to/application.ini" % sys.argv[0], file=sys.stderr)
